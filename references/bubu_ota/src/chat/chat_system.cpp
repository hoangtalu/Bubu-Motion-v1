#include "chat_system.h"
#include "chat_audio.h"
#include "chat_config.h"
#include "chat_screen.h"
#include "gemini_live_handler.h"
#include "display_system.h"
#include "voice_detector.h"
#include "wifi_service.h"
#include "care_system.h"
#include "../audio/audio_buffer.h"
#include "../audio/pitch_shifter.h"
#include "../crash_monitor.h"

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <cstring>

// Global chat config instance (loaded from NVS)
ChatConfig chatConfig;

namespace ChatSystem {

// ------------------------------------------------------------------
// State
// ------------------------------------------------------------------
static ChatState    sState        = CHAT_DISABLED;
static bool         sTasksCreated = false;

// Processing task handle (created per-turn, deletes itself)
static TaskHandle_t sProcessTask  = nullptr;
static volatile bool sAbortProcess = false;
static volatile bool sStopRequested = false;
static bool sStartListeningAfterConnect = false;

// Timestamp when beginConnect() was called (for timeout detection)
static uint32_t sConnectStart = 0;

// ------------------------------------------------------------------
// Deferred mood system (apply mood at session end, not during chat)
// ------------------------------------------------------------------
static int sMoodIncrease = 0;       // Accumulated mood from emotions in current session
static uint32_t sLastEmotionMs = 0; // Timestamp of last emotion call

// ------------------------------------------------------------------
// Mocking voice system (random delayed playback of recorded audio)
// ------------------------------------------------------------------
static uint32_t sMockingTimerStart = 0;    // Start time of current mocking delay
static uint32_t sMockingDelayMs = 0;       // Random delay: 30-60 minutes in ms
static bool sMockingScheduled = false;     // True when mocking is scheduled
static uint32_t sMockingPlaybackStart = 0; // When mocking playback started
static uint32_t sMockingPlaybackDurMs = 0; // Expected playback duration in ms
static bool sMockingPlaying = false;       // True while mocking audio is playing
static constexpr float DEFAULT_PITCH_FACTOR = 1.5f;  // 1.5x pitch for mocking effect

// ------------------------------------------------------------------
// Forward declarations
// ------------------------------------------------------------------
static void processingTaskFunc(void* param);
static void startProcessing();
static void triggerMocking();
static void scheduleMocking();
static bool drainMicQueueToGemini(bool logEmpty = false);
static void enterListeningState();

// ------------------------------------------------------------------
// Helper: Map emotion string to EyeEmotion enum
// ------------------------------------------------------------------
static EyeEmotion emotionStringToEnum(const char* emotion) {
    if (!emotion) return EYE_EMO_IDLE;

    if (strcmp(emotion, "happy") == 0)    return EYE_EMO_HAPPY1;
    if (strcmp(emotion, "excited") == 0)  return EYE_EMO_EXCITED;
    if (strcmp(emotion, "love") == 0)     return EYE_EMO_LOVE;
    if (strcmp(emotion, "sad") == 0)      return EYE_EMO_SAD1;
    if (strcmp(emotion, "worried") == 0)  return EYE_EMO_WORRIED1;
    if (strcmp(emotion, "angry") == 0)    return EYE_EMO_ANGRY1;
    if (strcmp(emotion, "curious") == 0)  return EYE_EMO_CURIOUS;
    if (strcmp(emotion, "tired") == 0)    return EYE_EMO_TIRED;
    if (strcmp(emotion, "idle") == 0)     return EYE_EMO_IDLE;

    return EYE_EMO_IDLE;
}

// ------------------------------------------------------------------
// Helper: Execute set_emotion function call (defer mood to session end)
// ------------------------------------------------------------------
static void executeSetEmotion(const char* emotionParam) {
    if (!emotionParam || !*emotionParam) {
        return;
    }

    // Yield before emotion to let IDLE task run
    vTaskDelay(1);
    esp_task_wdt_reset();

    EyeEmotion emotion = emotionStringToEnum(emotionParam);
    DisplaySystem_setEmotion(emotion);

    // Yield after emotion execution
    vTaskDelay(1);
    esp_task_wdt_reset();

    // Track mood increase for session-end application (avoid NVS writes during chat)
    sMoodIncrease += 10;
    sLastEmotionMs = millis();

    Serial.printf("[Proc] Emotion set to %s, mood +10 (deferred, session total: +%d)\n",
                  emotionParam, sMoodIncrease);
}

// ------------------------------------------------------------------
// Helper: Apply accumulated mood when session ends
// ------------------------------------------------------------------
static void applySessionMood() {
    if (sMoodIncrease > 0) {
        Serial.printf("[Chat] Session end: applying +%d mood\n", sMoodIncrease);
        CareSystem::addMood(sMoodIncrease);
        sMoodIncrease = 0;
        sLastEmotionMs = 0;
    }
}

// ------------------------------------------------------------------
// Helper: Schedule mocking with random delay (30-60 minutes)
// ------------------------------------------------------------------
static void scheduleMocking() {
    // Random delay: 30 minutes (1800000 ms) to 1 hour (3600000 ms)
    sMockingDelayMs = random(1800000, 3600000);
    sMockingTimerStart = millis();
    sMockingScheduled = true;

    Serial.printf("[Mocking] Scheduled with %.1f min delay\n", sMockingDelayMs / 60000.0f);
}

// ------------------------------------------------------------------
// Helper: Trigger mocking playback (execute when timer expires)
// ------------------------------------------------------------------
static void triggerMocking() {
    if (sMockingPlaying) return;  // Don't overlap if still playing

    int16_t* buffer = AudioBuffer::getBuffer();
    size_t sampleCount = AudioBuffer::getSampleCount();

    if (!buffer || sampleCount == 0) {
        Serial.println("[Mocking] No audio buffer to play");
        sMockingScheduled = false;
        return;
    }

    // Allocate output buffer for pitch-shifted audio
    size_t outputSampleCount = PitchShifter::getOutputSampleCount(sampleCount, DEFAULT_PITCH_FACTOR);
    int16_t* outputBuffer = static_cast<int16_t*>(
        heap_caps_malloc(outputSampleCount * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!outputBuffer) {
        Serial.println("[Mocking] Failed to allocate output buffer for pitch-shift");
        sMockingScheduled = false;
        return;
    }

    // Apply pitch-shift
    CrashMonitor::setContext("pitchShift mocking");
    size_t shiftedSampleCount = PitchShifter::pitchShiftAudio(
        buffer, sampleCount, DEFAULT_PITCH_FACTOR, outputBuffer, outputSampleCount);
    CrashMonitor::clearContext();

    if (shiftedSampleCount == 0) {
        Serial.println("[Mocking] Pitch-shift failed");
        heap_caps_free(outputBuffer);
        sMockingScheduled = false;
        return;
    }

    // Send to playback queue (ChatAudio::sendToPlayback() takes ownership of buffer)
    PcmChunk chunk;
    chunk.pcm = outputBuffer;
    chunk.bytes = shiftedSampleCount * 2;

    // Calculate playback duration (add 500ms buffer for audio flush)
    sMockingPlaybackDurMs = (shiftedSampleCount * 1000UL / 16000UL) + 500;
    sMockingPlaybackStart = millis();
    sMockingPlaying = true;

    Serial.printf("[Mocking] Playing back: %u samples @ %.1fx pitch (~%u ms)\n",
                  (unsigned)shiftedSampleCount, DEFAULT_PITCH_FACTOR,
                  (unsigned)sMockingPlaybackDurMs);

    ChatAudio::sendToPlayback(chunk);

    sMockingScheduled = false;
}

// ------------------------------------------------------------------
// Helper: Limit response text to maximum 4 lines
// ------------------------------------------------------------------
static String limitResponseLines(const String& text, int maxLines = 4) {
    if (text.length() == 0) return text;

    int lineCount = 0;
    size_t pos = 0;
    size_t lastNewline = 0;

    // Count newlines and find position of maxLines-th newline
    while (pos < text.length() && lineCount < maxLines) {
        if (text[pos] == '\n') {
            lineCount++;
            if (lineCount < maxLines) {
                lastNewline = pos;
            }
        }
        pos++;
    }

    // If we found the cutoff point, return truncated text
    if (lineCount >= maxLines && pos <= text.length()) {
        return text.substring(0, pos);
    }

    // If text has fewer lines than max, return as-is
    return text;
}

// ------------------------------------------------------------------
// begin / update
// ------------------------------------------------------------------

void begin() {
    chatConfig.load();
    ChatAudio::begin();
    ChatAudio::startPlaybackTask();  // Create playback task (needed for mocking audio)
    AudioBuffer::begin();            // Initialize audio buffer for mocking

    if (chatConfig.enabled) {
        sState = CHAT_DISCONNECTED;
        Serial.println("[Chat] V2 ready - tap eye to connect");
    } else {
        sState = CHAT_DISABLED;
        Serial.println("[Chat] Disabled");
    }
}

void update() {
    // Drive WebSocket I/O — only while a processing turn is in flight.
    // Keep a live Gemini session pumped across IDLE/LISTENING so setup is sent
    // once per session, not once per utterance.
    if (sState == CHAT_WAITING || sState == CHAT_CONNECTING || GeminiLiveHandler::isConnected()) {
        GeminiLiveHandler::loop();
    }

    if (sState == CHAT_CONNECTING) {
        if (GeminiLiveHandler::isConnected()) {
            sConnectStart = 0;
            if (sStartListeningAfterConnect) {
                sStartListeningAfterConnect = false;
                Serial.println("[Chat] Gemini ready -> start listening");
                enterListeningState();
            } else {
                sState = CHAT_IDLE;
                Serial.println("[Chat] Gemini ready -> IDLE");
                if (ChatScreen::isVisible()) ChatScreen::showText("Ready");
            }
        } else if (GeminiLiveHandler::hasError()) {
            String errorText = GeminiLiveHandler::getLastError();
            Serial.printf("[Chat] Connect failed: %s\n", errorText.c_str());
            if (ChatScreen::isVisible()) {
                ChatScreen::showText(errorText.length() ? errorText.c_str() : "Connection failed");
            }
            sStartListeningAfterConnect = false;
            GeminiLiveHandler::disconnect();
            sState = CHAT_DISCONNECTED;
        } else if (sConnectStart != 0 && millis() - sConnectStart > 15000) {
            Serial.println("[Chat] Connect timeout");
            if (ChatScreen::isVisible()) ChatScreen::showText("Connection timeout");
            sStartListeningAfterConnect = false;
            GeminiLiveHandler::disconnect();
            sState = CHAT_DISCONNECTED;
        }
    }

    if ((sState == CHAT_IDLE || sState == CHAT_LISTENING) && GeminiLiveHandler::hasError()) {
        String errorText = GeminiLiveHandler::getLastError();
        Serial.printf("[Chat] Session socket failed: %s\n", errorText.c_str());
        GeminiLiveHandler::disconnect();
        sState = CHAT_DISCONNECTED;
    }

    // Check if mood timeout (30 seconds since last emotion with no new activity)
    if (sMoodIncrease > 0 && sState == CHAT_IDLE) {
        uint32_t timeSinceLastEmotion = millis() - sLastEmotionMs;
        if (timeSinceLastEmotion > 30000) {  // 30 second timeout
            applySessionMood();
        }
    }

    // Check if mocking timer has expired
    if (sMockingScheduled && !sMockingPlaying) {
        uint32_t elapsed = millis() - sMockingTimerStart;
        if (elapsed >= sMockingDelayMs) {
            triggerMocking();
        }
    }

    // Check if mocking playback has finished - release I2S back to sound system
    if (sMockingPlaying) {
        uint32_t elapsed = millis() - sMockingPlaybackStart;
        if (elapsed >= sMockingPlaybackDurMs) {
            ChatAudio::stopPlayback();
            sMockingPlaying = false;
            Serial.println("[Mocking] Playback complete, I2S released");
        }
    }
}

ChatState getState() { return sState; }

// ------------------------------------------------------------------
// Eye-tap handlers
// ------------------------------------------------------------------

void connectFromMenu() {
    if (sState != CHAT_DISCONNECTED && sState != CHAT_DISABLED) return;

    if (wifiGetState() != WifiState::CONNECTED) {
        Serial.println("[Chat] No WiFi");
        if (ChatScreen::isVisible()) ChatScreen::showText("No WiFi connection");
        return;
    }

    if (!chatConfig.isConfigured()) {
        Serial.println("[Chat] No API key - set via: chat key <YOUR_KEY>");
        if (ChatScreen::isVisible()) ChatScreen::showText("No API key set");
        return;
    }

    // Create mic task once
    if (!sTasksCreated) {
        ChatAudio::startMicTask();
        sTasksCreated = true;
    }

    // Start Live API WebSocket connection (non-blocking)
    sStartListeningAfterConnect = false;
    sConnectStart = millis();
    GeminiLiveHandler::beginConnect();
    sState = CHAT_CONNECTING;
    Serial.println("[Chat] Connecting to Gemini Live API...");
    if (ChatScreen::isVisible()) ChatScreen::showText("Connecting...");
}

static void enterListeningState() {
    sState = CHAT_LISTENING;
    VoiceDetector::setChatMode(true);
    ChatAudio::enableMic();
    AudioBuffer::startCapture();
    sStopRequested = false;

    if (ChatScreen::isVisible()) ChatScreen::clearText();
    Serial.println("[Chat] Listening...");

    startProcessing();
}

void startListening() {
    if (sState == CHAT_DISABLED) return;
    if (sState != CHAT_IDLE && sState != CHAT_DISCONNECTED) return;

    // Fail fast if prerequisites are missing (user gets immediate feedback)
    if (wifiGetState() != WifiState::CONNECTED) {
        Serial.println("[Chat] No WiFi");
        ChatScreen::showText("No WiFi connection");
        return;
    }
    if (!chatConfig.isConfigured()) {
        Serial.println("[Chat] No API key - set via: chat key <YOUR_KEY>");
        ChatScreen::showText("No API key set");
        return;
    }

    // Create mic task once (idempotent)
    if (!sTasksCreated) {
        ChatAudio::startMicTask();
        sTasksCreated = true;
    }

    // Stop any mocking playback in progress before starting new chat
    if (sMockingPlaying) {
        ChatAudio::stopPlayback();
        sMockingPlaying = false;
        Serial.println("[Chat] Mocking interrupted by new chat session");
    }

    if (!GeminiLiveHandler::isConnected()) {
        sStartListeningAfterConnect = true;
        sConnectStart = millis();
        GeminiLiveHandler::beginConnect();
        sState = CHAT_CONNECTING;
        Serial.println("[Chat] Connecting before listening...");
        if (ChatScreen::isVisible()) ChatScreen::showText("Connecting...");
        return;
    }

    enterListeningState();
}

void stopListening() {
    if (sState != CHAT_LISTENING) return;

    ChatAudio::disableMic();
    AudioBuffer::stopCapture();
    sStopRequested = true;
    sState = CHAT_WAITING;
    Serial.println("[Chat] Processing audio...");
}

void interrupt() {
    if (sState == CHAT_WAITING) {
        sAbortProcess = true;
        Serial.println("[Chat] Interrupt: aborting processing");
    }

    sStartListeningAfterConnect = false;
    sState = CHAT_IDLE;
    if (!ChatScreen::isVisible()) {
        VoiceDetector::setChatMode(false);
        DisplaySystem_setEmotion(EYE_EMO_IDLE);
    }
}

void endSession() {
    sAbortProcess = true;
    sStartListeningAfterConnect = false;
    ChatAudio::disableMic();
    GeminiLiveHandler::disconnect();  // End Live API session (clears Gemini history)
    VoiceDetector::setChatMode(false);

    // Apply any accumulated mood from emotions during this session
    applySessionMood();

    sState = CHAT_DISCONNECTED;
    Serial.println("[Chat] Session ended");
    if (!ChatScreen::isVisible()) DisplaySystem_setEmotion(EYE_EMO_IDLE);
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void enable() {
    chatConfig.enabled = true;
    chatConfig.save();
    sState = CHAT_DISCONNECTED;

    if (!sTasksCreated) {
        ChatAudio::startMicTask();
        sTasksCreated = true;
    }
    Serial.println("[Chat] Enabled");
}

void disable() {
    chatConfig.enabled = false;
    chatConfig.save();
    sAbortProcess = true;
    sStartListeningAfterConnect = false;
    ChatAudio::disableMic();
    VoiceDetector::setChatMode(false);
    sState = CHAT_DISABLED;
    Serial.println("[Chat] Disabled");
}

void exitChatScreen() {
    ChatAudio::disableMic();
    sStartListeningAfterConnect = false;
    VoiceDetector::setChatMode(false);
    if (sState == CHAT_LISTENING || sState == CHAT_WAITING) {
        sState = CHAT_IDLE;
    }
    DisplaySystem_setEmotion(EYE_EMO_IDLE);
}

bool isEnabled() { return sState != CHAT_DISABLED; }

bool isActive() {
    return sState == CHAT_LISTENING || sState == CHAT_WAITING;
}

// ------------------------------------------------------------------
// Legacy callbacks
// ------------------------------------------------------------------

void onSetupComplete() {
    sState = CHAT_IDLE;
    Serial.println("[Chat] Gemini ready -> IDLE");
}

void onGeminiAudioStart() {}
void onTurnComplete()     {}

void onDisconnected() {
    ChatAudio::disableMic();
    sState = CHAT_DISCONNECTED;
}

void onVolume(uint8_t vol) {
    chatConfig.volume = vol;
    ChatAudio::setVolume(vol);
}

void sendText(const char* text) {
    Serial.printf("[Chat] sendText (legacy): %s\n", text ? text : "(null)");
}

void testMockingNow() {
    Serial.println("[Chat] TEST: Triggering mocking playback now");
    triggerMocking();
}

// ------------------------------------------------------------------
// Processing task: drain mic buffer -> Gemini native audio -> show text
// ------------------------------------------------------------------

static void startProcessing() {
    if (sProcessTask) return;
    sAbortProcess = false;
    xTaskCreatePinnedToCore(
        processingTaskFunc, "chat_proc",
        1024 * 24,   // 24 KB stack
        nullptr, 2, &sProcessTask, 0);
}

static bool drainMicQueueToGemini(bool logEmpty) {
    QueueHandle_t micQ = ChatAudio::getMicQueue();
    PcmChunk chunk;
    bool sentAny = false;

    while (micQ && xQueueReceive(micQ, &chunk, 0) == pdTRUE) {
        if (sAbortProcess) {
            if (chunk.pcm) heap_caps_free(chunk.pcm);
            break;
        }
        if (!chunk.pcm || chunk.bytes == 0) continue;

        if (!GeminiLiveHandler::sendAudioChunk(reinterpret_cast<const uint8_t*>(chunk.pcm), chunk.bytes)) {
            heap_caps_free(chunk.pcm);
            return false;
        }
        heap_caps_free(chunk.pcm);
        sentAny = true;
    }

    if (!sentAny && logEmpty) {
        Serial.println("[Proc] No queued mic chunks to stream");
    }
    return sentAny;
}

static void processingTaskFunc(void* param) {
    bool turnStarted = false;
    bool inputClosed = false;
    uint32_t settleDeadline = 0;
    String errorText;

    if (!GeminiLiveHandler::isConnected()) {
        Serial.println("[Proc] Gemini Live turn");
        GeminiLiveHandler::beginConnect();
        {
            uint32_t deadline = millis() + 15000;
            while (!GeminiLiveHandler::isConnected() &&
                   !GeminiLiveHandler::hasError() &&
                   millis() < deadline) {
                GeminiLiveHandler::loop();
                vTaskDelay(pdMS_TO_TICKS(10));
                esp_task_wdt_reset();
            }
        }
        if (!GeminiLiveHandler::isConnected()) {
            errorText = GeminiLiveHandler::getLastError();
            if (!errorText.length()) errorText = "Connect timeout";
            Serial.printf("[Proc] Connect failed: %s\n", errorText.c_str());
            ChatScreen::showText(errorText.c_str());
            GeminiLiveHandler::disconnect();
            sState = CHAT_DISCONNECTED;
            sProcessTask = nullptr;
            vTaskDelete(nullptr);
            return;
        }
    } else {
        Serial.println("[Proc] Reusing Gemini Live session");
    }

    while (!sAbortProcess) {
        GeminiLiveHandler::loop();

        if (GeminiLiveHandler::hasError()) {
            break;
        }

        if (!inputClosed) {
            QueueHandle_t micQ = ChatAudio::getMicQueue();
            if (!turnStarted && micQ && uxQueueMessagesWaiting(micQ) > 0) {
                GeminiLiveHandler::beginAudioTurn();
                turnStarted = true;
            }

            if (!drainMicQueueToGemini()) {
                // No chunks currently ready; just keep looping.
            }

            if (sStopRequested) {
                if (!micQ || uxQueueMessagesWaiting(micQ) == 0) {
                    if (!turnStarted) {
                        errorText = "No audio captured";
                        break;
                    }
                    if (!GeminiLiveHandler::endAudioTurn()) {
                        errorText = GeminiLiveHandler::getLastError();
                        if (!errorText.length()) errorText = "Failed to close audio turn";
                        break;
                    }
                    inputClosed = true;
                    settleDeadline = millis() + 30000;
                }
            }
        } else if (GeminiLiveHandler::isTurnSettled()) {
            break;
        } else if (settleDeadline != 0 && millis() > settleDeadline) {
            if (!errorText.length()) errorText = "Turn timeout";
            break;
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!errorText.length()) {
        errorText = GeminiLiveHandler::getLastError();
    }
    GeminiLiveHandler::TurnResult turnResult = GeminiLiveHandler::getTurnResult();
    bool ok = !sAbortProcess && GeminiLiveHandler::didTurnSucceed();

    if (!turnStarted || (!turnResult.receivedAudio && turnResult.responseText.length() == 0)) {
        if (!errorText.length()) {
            errorText = "No audio captured";
        }
    }

    if (!ok) {
        if (!sAbortProcess) {
            Serial.printf("[Proc] Gemini Live turn failed: %s\n", errorText.c_str());
            ChatScreen::showText(errorText.length() ? errorText.c_str()
                                                    : "Didn't catch that - try again");
        }
        GeminiLiveHandler::disconnect();
        sState = CHAT_DISCONNECTED;
        sProcessTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // ---- 4. Show response text FIRST (before emotion changes display state) ----
    String responseText = turnResult.responseText;
    String functionName = turnResult.functionName;
    String functionParam = turnResult.functionParam;

    if (responseText.length() > 0) {
        String limitedText = limitResponseLines(responseText, 4);
        ChatScreen::showText(limitedText.c_str());
        vTaskDelay(1);
    }

    // ---- 5. Handle function calls AFTER text is rendered ----
    if (functionName.length() > 0) {
        Serial.printf("[Proc] Processing function call: %s\n", functionName.c_str());

        if (functionName == "set_emotion") {
            esp_task_wdt_reset();
            executeSetEmotion(functionParam.c_str());
            vTaskDelay(1);
        }
    }

    // Final yield
    vTaskDelay(1);

    // Schedule random mocking playback for later (30-60 minutes)
    scheduleMocking();

    sState = CHAT_IDLE;
    Serial.println("[Proc] Done -> IDLE");

    sStopRequested = false;
    sProcessTask = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace ChatSystem
