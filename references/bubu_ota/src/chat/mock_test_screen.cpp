#include "mock_test_screen.h"
#include "chat_audio.h"
#include "chat_system.h"
#include "menu_system.h"
#include "voice_detector.h"
#include "../audio/audio_buffer.h"
#include "../audio/pitch_shifter.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/queue.h>

LV_FONT_DECLARE(lv_font_montserrat_vn_20);

namespace MockTestScreen {

// UI objects
static lv_obj_t* sScreenContainer = nullptr;
static lv_obj_t* sCircle = nullptr;

// State machine
static MockState sMockState = MOCK_WHITE;
static bool sIsOpen = false;

// Recording timer — auto-stops after RECORD_DURATION_MS
static uint32_t sRecordingStartMs = 0;
static constexpr uint32_t RECORD_DURATION_MS = 3000;

// Timing for playback completion detection
static uint32_t sMockingPlaybackStart = 0;
static uint32_t sMockingPlaybackDurMs = 0;
static constexpr float DEFAULT_PITCH_FACTOR = 1.5f;

// Screen dimensions
static constexpr int16_t SCREEN_SIZE = 240;
static constexpr int16_t CIRCLE_RADIUS = 50;
static constexpr int16_t CIRCLE_X = SCREEN_SIZE / 2 - CIRCLE_RADIUS;
static constexpr int16_t CIRCLE_Y = SCREEN_SIZE / 2 - CIRCLE_RADIUS;
static constexpr int16_t CIRCLE_DIAM = CIRCLE_RADIUS * 2;

// --- Helpers ---

static lv_color_t getCircleColor() {
    switch (sMockState) {
        case MOCK_WHITE:  return lv_color_white();
        case MOCK_YELLOW: return lv_color_make(255, 255, 0);
        case MOCK_GREEN:  return lv_color_make(0, 255, 0);
        default:          return lv_color_white();
    }
}

static void updateCircleColor() {
    if (!sCircle) return;
    lv_obj_set_style_bg_color(sCircle, getCircleColor(), 0);
}

static bool isPointInsideCircle(uint16_t x, uint16_t y) {
    if (!sCircle) return false;
    lv_area_t area;
    lv_obj_get_coords(sCircle, &area);
    int32_t centerX = (area.x1 + area.x2) / 2;
    int32_t centerY = (area.y1 + area.y2) / 2;
    int32_t dx = (int32_t)x - centerX;
    int32_t dy = (int32_t)y - centerY;
    return (dx * dx + dy * dy) <= (CIRCLE_RADIUS * CIRCLE_RADIUS);
}

// Drain any pending mic chunks into AudioBuffer, then stop capture and start playback.
// MUST drain BEFORE calling stopCapture() — addSamples() gates on sCapturing.
static void stopRecordingAndPlay() {
    // 1. Final queue drain while capture is still active
    QueueHandle_t micQueue = ChatAudio::getMicQueue();
    PcmChunk drainChunk;
    while (xQueueReceive(micQueue, &drainChunk, 0) == pdTRUE) {
        if (drainChunk.pcm && drainChunk.bytes > 0) {
            AudioBuffer::addSamples(drainChunk.pcm, drainChunk.bytes / sizeof(int16_t));
            heap_caps_free(drainChunk.pcm);
        }
    }

    // 2. Now safe to stop capture and mic
    AudioBuffer::stopCapture();
    ChatAudio::disableMic();
    VoiceDetector::setChatMode(false);

    // 3. Check we actually have audio
    int16_t* buffer = AudioBuffer::getBuffer();
    size_t sampleCount = AudioBuffer::getSampleCount();

    if (!buffer || sampleCount == 0) {
        Serial.println("[MockTestScreen] No audio recorded");
        sMockState = MOCK_WHITE;
        updateCircleColor();
        return;
    }

    // 4. Allocate output buffer for pitch-shifted audio
    size_t outputSampleCount = PitchShifter::getOutputSampleCount(sampleCount, DEFAULT_PITCH_FACTOR);
    int16_t* outputBuffer = static_cast<int16_t*>(
        heap_caps_malloc(outputSampleCount * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!outputBuffer) {
        Serial.println("[MockTestScreen] Failed to allocate output buffer");
        sMockState = MOCK_WHITE;
        updateCircleColor();
        return;
    }

    // 5. Apply pitch-shift
    size_t processedSamples = PitchShifter::pitchShiftAudio(
        buffer, sampleCount,
        DEFAULT_PITCH_FACTOR,
        outputBuffer, outputSampleCount);

    if (processedSamples == 0) {
        Serial.println("[MockTestScreen] Pitch-shift failed");
        heap_caps_free(outputBuffer);
        sMockState = MOCK_WHITE;
        updateCircleColor();
        return;
    }

    // 6a. Diagnostic: log RMS to confirm mic amplitude
    {
        int64_t rmsSum = 0;
        size_t checkSamples = (processedSamples < 8000) ? processedSamples : 8000;
        for (size_t i = 0; i < checkSamples; i++) {
            rmsSum += (int32_t)outputBuffer[i] * outputBuffer[i];
        }
        uint32_t rms = (uint32_t)sqrtf((float)rmsSum / checkSamples);
        Serial.printf("[MockTestScreen] Post-shift RMS=%u | samples=%zu\n", rms, processedSamples);
    }

    // 6b. Amplify: INMP441 captures at ~RMS 100-300/32767; boost so voice is audible
    {
        const float gain = 8.0f;
        for (size_t i = 0; i < processedSamples; i++) {
            float val = outputBuffer[i] * gain;
            if (val > 32767.0f)       val = 32767.0f;
            else if (val < -32768.0f) val = -32768.0f;
            outputBuffer[i] = (int16_t)val;
        }
    }

    // 7. Transition to GREEN and start playback
    sMockingPlaybackDurMs = (processedSamples * 1000) / 16000;
    sMockingPlaybackStart = millis();
    sMockState = MOCK_GREEN;
    updateCircleColor();

    size_t chunkSizeBytes = 4096;
    size_t bytesRemaining = processedSamples * 2;
    uint8_t* ptr = (uint8_t*)outputBuffer;

    while (bytesRemaining > 0) {
        size_t chunkBytes = (bytesRemaining > chunkSizeBytes) ? chunkSizeBytes : bytesRemaining;

        uint8_t* chunkData = (uint8_t*)heap_caps_malloc(chunkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!chunkData) {
            Serial.println("[MockTestScreen] Failed to allocate chunk");
            break;
        }

        memcpy(chunkData, ptr, chunkBytes);

        PcmChunk chunk;
        chunk.pcm = (int16_t*)chunkData;
        chunk.bytes = chunkBytes;
        ChatAudio::sendToPlayback(chunk);

        bytesRemaining -= chunkBytes;
        ptr += chunkBytes;

        vTaskDelay(1);
        esp_task_wdt_reset();
    }

    heap_caps_free(outputBuffer);
    Serial.printf("[MockTestScreen] Playback started: %zu ms duration\n", sMockingPlaybackDurMs);
}

// --- Public API ---

void begin() {
    if (sScreenContainer) return;

    sScreenContainer = lv_obj_create(lv_screen_active());
    lv_obj_set_size(sScreenContainer, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_pos(sScreenContainer, 0, 0);
    lv_obj_set_style_bg_color(sScreenContainer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sScreenContainer, 255, 0);
    lv_obj_set_style_border_width(sScreenContainer, 0, 0);
    lv_obj_set_style_pad_all(sScreenContainer, 0, 0);
    lv_obj_clear_flag(sScreenContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sScreenContainer, LV_OBJ_FLAG_HIDDEN);

    sCircle = lv_obj_create(sScreenContainer);
    lv_obj_set_size(sCircle, CIRCLE_DIAM, CIRCLE_DIAM);
    lv_obj_set_pos(sCircle, CIRCLE_X, CIRCLE_Y);
    lv_obj_set_style_radius(sCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sCircle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sCircle, 255, 0);
    lv_obj_set_style_border_width(sCircle, 0, 0);
    lv_obj_set_style_pad_all(sCircle, 0, 0);
    lv_obj_clear_flag(sCircle, LV_OBJ_FLAG_SCROLLABLE);

    Serial.println("[MockTestScreen] Initialized");
}

void open() {
    VoiceDetector::begin();
    VoiceDetector::enable();

    if (!sScreenContainer) begin();

    lv_obj_move_foreground(sScreenContainer);
    lv_obj_clear_flag(sScreenContainer, LV_OBJ_FLAG_HIDDEN);
    sIsOpen = true;
    sMockState = MOCK_WHITE;
    updateCircleColor();
    AudioBuffer::clear();

    Serial.println("[MockTestScreen] Opened");
}

void close() {
    if (!sScreenContainer) return;

    if (sMockState == MOCK_YELLOW) {
        AudioBuffer::stopCapture();
        ChatAudio::disableMic();
        VoiceDetector::setChatMode(false);
    } else if (sMockState == MOCK_GREEN) {
        ChatAudio::stopPlayback();
    }

    VoiceDetector::disable();

    lv_obj_add_flag(sScreenContainer, LV_OBJ_FLAG_HIDDEN);
    sIsOpen = false;
    sMockState = MOCK_WHITE;

    MenuSystem::open();
    Serial.println("[MockTestScreen] Closed, returning to menu");
}

bool isOpen() {
    return sIsOpen;
}

void update() {
    if (!sIsOpen) return;

    if (sMockState == MOCK_YELLOW) {
        // Continuously drain mic queue into AudioBuffer while recording
        QueueHandle_t micQueue = ChatAudio::getMicQueue();
        PcmChunk chunk;
        while (xQueueReceive(micQueue, &chunk, 0) == pdTRUE) {
            if (chunk.pcm && chunk.bytes > 0) {
                AudioBuffer::addSamples(chunk.pcm, chunk.bytes / sizeof(int16_t));
                heap_caps_free(chunk.pcm);
            }
        }

        // Auto-stop after RECORD_DURATION_MS (3 seconds)
        if (millis() - sRecordingStartMs >= RECORD_DURATION_MS) {
            Serial.println("[MockTestScreen] 3s recording complete, starting playback");
            stopRecordingAndPlay();
        }
    }

    // Check if playback has completed — auto-close so user returns to menu
    if (sMockState == MOCK_GREEN && sMockingPlaybackDurMs > 0) {
        uint32_t elapsedMs = millis() - sMockingPlaybackStart;
        if (elapsedMs >= sMockingPlaybackDurMs + 500) {
            Serial.println("[MockTestScreen] Playback complete, closing");
            close();
        }
    }
}

bool handleTap(uint16_t x, uint16_t y) {
    if (!sIsOpen) return false;

    // Tap outside circle = exit
    if (!isPointInsideCircle(x, y)) {
        Serial.println("[MockTestScreen] Tapped outside circle, closing");
        close();
        return true;
    }

    Serial.printf("[MockTestScreen] Circle tapped, state=%d\n", (int)sMockState);

    switch (sMockState) {
        case MOCK_WHITE:
            // WHITE → YELLOW: Start 3-second recording
            Serial.println("[MockTestScreen] Starting 3s recording (WHITE → YELLOW)");
            AudioBuffer::clear();
            VoiceDetector::setChatMode(true);
            ChatAudio::startMicTask();
            ChatAudio::enableMic();
            AudioBuffer::startCapture();
            sRecordingStartMs = millis();
            sMockState = MOCK_YELLOW;
            updateCircleColor();
            return true;

        case MOCK_YELLOW:
            // Ignore taps during recording — auto-stops after 3s
            return true;

        case MOCK_GREEN:
            // Ignore taps during playback — auto-returns to WHITE
            return true;
    }

    return false;
}

MockState getState() {
    return sMockState;
}

}  // namespace MockTestScreen
