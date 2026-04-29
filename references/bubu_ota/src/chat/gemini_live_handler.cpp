#include "gemini_live_handler.h"
#include "chat_config.h"
#include "chat_audio.h"

#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <atomic>
#include <freertos/semphr.h>

// libb64 from WebSockets library bundle
extern "C" {
#include "libb64/cencode_inc.h"
#include "libb64/cdecode_inc.h"
}

namespace GeminiLiveHandler {

static WebSocketsClient   sWs;
static SemaphoreHandle_t  sMutex       = nullptr;
static std::atomic<bool>  sWsUp        {false};  // TCP + WS handshake done
static std::atomic<bool>  sPendingSetup{false};  // send setup on next loop()
static std::atomic<bool>  sSetupDone   {false};  // setupComplete received
static std::atomic<bool>  sTurnDone    {false};  // turnComplete received
static std::atomic<bool>  sGenDone     {false};  // generationComplete received
static std::atomic<bool>  sFailed      {false};  // socket/server error received
static TurnResult         sTurnResult;
static String             sLastError;
static uint32_t           sLastContentMs = 0;
static uint32_t           sGenDoneMs     = 0;

// ------------------------------------------------------------------
// Base64 helpers
// ------------------------------------------------------------------

// Encode 'len' bytes → heap-allocated null-terminated string (no newlines).
// Caller must free().
static char* b64Encode(const uint8_t* data, size_t len) {
    size_t outMax = ((len + 2) / 3) * 4 + 4;
    char* out = (char*)malloc(outMax);
    if (!out) return nullptr;

    base64_encodestate st;
    base64_init_encodestate(&st);
    int n = base64_encode_block((const char*)data, (int)len, out, &st);
    n += base64_encode_blockend(out + n, &st);

    // libb64 inserts newlines every 76 chars — strip them
    int j = 0;
    for (int i = 0; i < n; i++) {
        if (out[i] != '\n' && out[i] != '\r') out[j++] = out[i];
    }
    out[j] = '\0';
    return out;
}

// Decode base64 string → PSRAM buffer.
// Returns decoded byte count (>0) on success; caller must heap_caps_free(buf).
static int b64Decode(const char* b64, size_t b64len, uint8_t** outBuf) {
    size_t maxOut = (b64len / 4) * 3 + 4;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(maxOut, MALLOC_CAP_SPIRAM);
    if (!buf) return 0;

    base64_decodestate st;
    base64_init_decodestate(&st);
    int decoded = base64_decode_block(b64, (int)b64len, (char*)buf, &st);
    if (decoded <= 0) { heap_caps_free(buf); return 0; }

    *outBuf = buf;
    return decoded;
}

static void clearTurnResult() {
    sTurnResult.responseText = "";
    sTurnResult.functionName = "";
    sTurnResult.functionParam = "";
    sTurnResult.receivedAudio = false;
    sTurnResult.receivedTranscript = false;
}

static void mergeText(String& target, const char* fragment) {
    if (!fragment || !*fragment) return;

    String incoming(fragment);
    if (incoming.length() == 0) return;

    if (target.length() == 0) {
        target = incoming;
        return;
    }
    if (incoming == target) return;
    if (incoming.startsWith(target)) {
        target = incoming;
        return;
    }
    if (target.endsWith(incoming)) return;

    bool needsSpace = !target.endsWith(" ") &&
                      !target.endsWith("\n") &&
                      !incoming.startsWith(" ") &&
                      !incoming.startsWith("\n");
    if (needsSpace) target += ' ';
    target += incoming;
}

static String extractErrorMessage(JsonVariantConst err) {
    const char* message = err["message"] | "";
    if (*message) return String(message);

    const char* status = err["status"] | "";
    const char* details = err["details"][0]["message"] | "";
    if (*status && *details) return String(status) + ": " + details;
    if (*status) return String(status);
    if (*details) return String(details);
    return String("Unknown Gemini Live error");
}

static void setError(const String& message) {
    if (message.length() == 0) return;
    sLastError = message;
    sFailed.store(true);
    Serial.printf("[GeminiLive] Error: %s\n", message.c_str());
}

static bool hasUsableTurnResult() {
    return sTurnResult.receivedTranscript ||
           sTurnResult.receivedAudio ||
           sTurnResult.responseText.length() > 0;
}

static bool postGenerationDrainComplete() {
    if (!sGenDone.load()) return false;
    uint32_t now = millis();
    uint32_t quietSince = sLastContentMs ? (now - sLastContentMs) : 0;
    uint32_t genSince = sGenDoneMs ? (now - sGenDoneMs) : 0;
    return quietSince >= 1200 || genSince >= 2500;
}

static bool mimeTypeLooksLikeAudio(const char* mimeType) {
    return mimeType &&
           (strncmp(mimeType, "audio/pcm", 9) == 0 ||
            strncmp(mimeType, "audio/L16", 9) == 0 ||
            strncmp(mimeType, "audio/raw", 9) == 0);
}

// ------------------------------------------------------------------
// WebSocket event handler (called from within sWs.loop())
// ------------------------------------------------------------------

static void onWsEvent(WStype_t type, uint8_t* payload, size_t len) {
    switch (type) {

    case WStype_CONNECTED:
        sWsUp.store(true);
        sPendingSetup.store(true);  // defer send to loop() — sending from callback gets dropped
        sFailed.store(false);
        sGenDone.store(false);
        sLastContentMs = 0;
        sGenDoneMs = 0;
        Serial.println("[GeminiLive] WebSocket connected");
        break;

    case WStype_DISCONNECTED: {
        uint16_t code = (len >= 2) ? ((payload[0] << 8) | payload[1]) : 0;
        bool hadSetup = sSetupDone.load();
        bool hadTurnComplete = sTurnDone.load();
        bool hadGenerationComplete = sGenDone.load();
        bool hadUsableResult = hasUsableTurnResult();
        Serial.printf("[GeminiLive] Disconnected (close code %u)\n", code);
        sWsUp.store(false);
        sSetupDone.store(false);
        if (!sLastError.length() &&
            (!hadSetup || (!hadTurnComplete && !hadGenerationComplete && !hadUsableResult))) {
            if (code != 0) {
                setError(String("Socket closed (") + code + ")");
            } else {
                setError("Socket closed before reply completed");
            }
        }
        break;
    }

    case WStype_ERROR:
        setError(len > 0 ? String((const char*)payload, len) : String("WebSocket error"));
        break;

    case WStype_BIN:
    case WStype_TEXT: {
        Serial.printf("[GeminiLive] RX(%s,%u): %.*s\n",
                      type == WStype_BIN ? "BIN" : "TXT",
                      (unsigned)len, (int)min(len, (size_t)300), payload);
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload, len);
        if (err) {
            Serial.printf("[GeminiLive] JSON err: %s\n", err.c_str());
            break;
        }

        JsonVariant serverError = doc["error"];
        if (!serverError.isNull()) {
            setError(extractErrorMessage(serverError));
            break;
        }

        // setupComplete
        if (!doc["setupComplete"].isNull()) {
            sSetupDone.store(true);
            Serial.println("[GeminiLive] setupComplete received — ready");
            break;
        }

        JsonVariant toolCall = doc["toolCall"];
        if (!toolCall.isNull()) {
            JsonArray functionCalls = toolCall["functionCalls"].as<JsonArray>();
            for (JsonObject functionCall : functionCalls) {
                const char* name = functionCall["name"] | "";
                if (!*name) continue;

                sTurnResult.functionName = name;
                JsonVariant args = functionCall["args"];
                const char* emotion = args["emotion"] | "";
                if (*emotion) {
                    sTurnResult.functionParam = emotion;
                } else {
                    sTurnResult.functionParam = "";
                    serializeJson(args, sTurnResult.functionParam);
                }

                Serial.printf("[GeminiLive] Tool call: %s %s\n",
                              sTurnResult.functionName.c_str(),
                              sTurnResult.functionParam.c_str());
                break;
            }
            break;
        }

        if (!doc["goAway"].isNull()) {
            setError("Gemini requested session shutdown");
            break;
        }

        // serverContent
        JsonVariant sc = doc["serverContent"];
        if (sc.isNull()) break;
        sLastContentMs = millis();

        if (sc["interrupted"].as<bool>()) {
            Serial.println("[GeminiLive] Generation interrupted");
        }

        if (sc["generationComplete"].as<bool>()) {
            sGenDone.store(true);
            sGenDoneMs = millis();
            Serial.println("[GeminiLive] generationComplete");
        }

        const char* outputText = sc["outputTranscription"]["text"] | "";
        if (*outputText) {
            sTurnResult.receivedTranscript = true;
            mergeText(sTurnResult.responseText, outputText);
            Serial.printf("[GeminiLive] Output transcript: %s\n",
                          sTurnResult.responseText.c_str());
        }

        if (sc["turnComplete"].as<bool>()) {
            Serial.println("[GeminiLive] turnComplete");
            sTurnDone.store(true);
        }

        JsonArray parts = sc["modelTurn"]["parts"].as<JsonArray>();
        for (JsonObject part : parts) {
            if (part["thought"].as<bool>()) continue;

            const char* text = part["text"] | "";
            if (*text && !sTurnResult.receivedTranscript) {
                mergeText(sTurnResult.responseText, text);
            }

            const char* b64data = part["inlineData"]["data"] | "";
            const char* mimeType = part["inlineData"]["mimeType"] | "";
            if (!*b64data) continue;

            if (*mimeType) {
                Serial.printf("[GeminiLive] inlineData mimeType=%s\n", mimeType);
            }

            if (*mimeType && !mimeTypeLooksLikeAudio(mimeType)) {
                Serial.println("[GeminiLive] Non-audio inlineData ignored");
                continue;
            }

            uint8_t* pcm = nullptr;
            int decoded = b64Decode(b64data, strlen(b64data), &pcm);
            if (decoded > 0) {
                Serial.printf("[GeminiLive] AUDIO part: %d bytes → playback\n", decoded);
                PcmChunk chunk = {(int16_t*)pcm, (size_t)decoded};
                ChatAudio::sendToPlayback(chunk);
                sTurnResult.receivedAudio = true;
            }
        }

        // Catch-all: log any response we didn't handle
        if (doc["setupComplete"].isNull() &&
            doc["serverContent"].isNull() &&
            doc["toolCall"].isNull() &&
            doc["goAway"].isNull() &&
            doc["error"].isNull()) {
            Serial.printf("[GeminiLive] Unhandled: %.*s\n", (int)min(len, (size_t)200), payload);
        }
        break;
    }

    default:
        Serial.printf("[GeminiLive] Unhandled WS event type=%d len=%u\n", (int)type, (unsigned)len);
        break;
    }
}

// ------------------------------------------------------------------
// Internal init (idempotent)
// ------------------------------------------------------------------

static void init() {
    if (sMutex) return;
    sMutex = xSemaphoreCreateMutex();
    sWs.onEvent(onWsEvent);
    sWs.setReconnectInterval(60000);  // Don't spam reconnects
}

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

void beginConnect() {
    init();
    // Tear down any previous attempt before starting fresh
    xSemaphoreTake(sMutex, portMAX_DELAY);
    sWs.disconnect();
    clearTurnResult();
    sLastError = "";
    xSemaphoreGive(sMutex);
    sPendingSetup.store(false);
    sSetupDone.store(false);
    sTurnDone.store(false);
    sGenDone.store(false);
    sWsUp.store(false);
    sFailed.store(false);

    String path = String("/ws/google.ai.generativelanguage.v1beta.GenerativeService"
                         ".BidiGenerateContent?key=") + chatConfig.apiKey;

    xSemaphoreTake(sMutex, portMAX_DELAY);
    sWs.setExtraHeaders("");
    sWs.beginSSL(GEMINI_API_HOST, GOOGLE_API_PORT, path.c_str(), "", "");
    xSemaphoreGive(sMutex);

    Serial.println("[GeminiLive] Connecting to Live API...");
}

bool isConnected() {
    return sSetupDone.load();
}

bool isGenerationComplete() {
    return sGenDone.load();
}

bool hasError() {
    return sFailed.load();
}

String getLastError() {
    if (!sMutex) return sLastError;
    String copy;
    xSemaphoreTake(sMutex, portMAX_DELAY);
    copy = sLastError;
    xSemaphoreGive(sMutex);
    return copy;
}

void disconnect() {
    if (!sMutex) return;
    xSemaphoreTake(sMutex, portMAX_DELAY);
    sWs.disconnect();
    xSemaphoreGive(sMutex);
    sWsUp.store(false);
    sSetupDone.store(false);
    sTurnDone.store(false);
    sGenDone.store(false);
    Serial.println("[GeminiLive] Disconnected");
}

void loop() {
    if (!sMutex) return;
    // Non-blocking: skip if mutex is busy (e.g. sendAudioTurn is active)
    if (xSemaphoreTake(sMutex, 0) == pdTRUE) {
        sWs.loop();
        xSemaphoreGive(sMutex);
    }

    // Deferred setup send — MUST happen outside the WS event callback.
    // Calling sendTXT() from inside WStype_CONNECTED is re-entrant and the
    // frame gets silently dropped by WebSocketsClient. So we set sPendingSetup
    // in the callback and do the actual send here, on the next loop() tick.
    if (sPendingSetup.load()) {
        sPendingSetup.store(false);

        JsonDocument doc;
        doc["setup"]["model"] = GEMINI_LIVE_MODEL;
        doc["setup"]["generationConfig"]["maxOutputTokens"] = GEMINI_LIVE_MAX_OUTPUT_TOKENS;
        doc["setup"]["generationConfig"]["responseModalities"][0] = "AUDIO";
        doc["setup"]["outputAudioTranscription"].to<JsonObject>();
        // Note: thinkingConfig is NOT a valid Live API field — omit it
        doc["setup"]["generationConfig"]["speechConfig"]
            ["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = "Puck";
        doc["setup"]["systemInstruction"]["parts"][0]["text"] = BUBU_SYSTEM_PROMPT;

        String setupStr;
        serializeJson(doc, setupStr);
        Serial.printf("[GeminiLive] Sending setup (%u bytes): %s\n",
                      (unsigned)setupStr.length(), setupStr.c_str());

        xSemaphoreTake(sMutex, portMAX_DELAY);
        sWs.sendTXT(setupStr);
        xSemaphoreGive(sMutex);
        Serial.println("[GeminiLive] Setup sent");
    }
}

void beginAudioTurn() {
    if (!sSetupDone.load()) {
        Serial.println("[GeminiLive] beginAudioTurn: not connected");
        return;
    }

    sTurnDone.store(false);
    sGenDone.store(false);
    sFailed.store(false);
    sLastContentMs = 0;
    sGenDoneMs = 0;
    xSemaphoreTake(sMutex, portMAX_DELAY);
    clearTurnResult();
    sLastError = "";
    xSemaphoreGive(sMutex);
}

bool sendAudioChunk(const uint8_t* pcm, size_t bytes) {
    if (!sSetupDone.load()) {
        Serial.println("[GeminiLive] sendAudioChunk: not connected");
        return false;
    }
    if (!pcm || bytes == 0) {
        return true;
    }

    // Send PCM in smaller chunks to reduce burst pressure on the socket.
    const size_t CHUNK = 4096;
    int chunksSent = 0;

    for (size_t offset = 0; offset < bytes; offset += CHUNK) {
        size_t sz = (bytes - offset < CHUNK) ? (bytes - offset) : CHUNK;

        char* b64 = b64Encode(pcm + offset, sz);
        if (!b64) {
            Serial.println("[GeminiLive] b64 encode OOM");
            break;
        }

        // Build JSON inline — avoids ArduinoJson overhead for large strings
        String msg;
        msg.reserve(strlen(b64) + 84);
        msg = F("{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"");
        msg += b64;
        msg += F("\"}}}");
        free(b64);

        xSemaphoreTake(sMutex, portMAX_DELAY);
        sWs.sendTXT(msg);
        xSemaphoreGive(sMutex);
        chunksSent++;

        vTaskDelay(pdMS_TO_TICKS(10));  // yield between chunks
    }

    Serial.printf("[GeminiLive] Sent %d audio chunks\n", chunksSent);
    return true;
}

bool endAudioTurn() {
    if (!sSetupDone.load()) {
        Serial.println("[GeminiLive] endAudioTurn: not connected");
        return false;
    }
    // audioStreamEnd tells the Live API that this utterance is complete.
    xSemaphoreTake(sMutex, portMAX_DELAY);
    sWs.sendTXT("{\"realtimeInput\":{\"audioStreamEnd\":true}}");
    xSemaphoreGive(sMutex);
    Serial.println("[GeminiLive] audioStreamEnd sent");
    return true;
}

bool isTurnSettled() {
    if (sFailed.load()) return true;
    if (sTurnDone.load()) return true;
    if (!sWsUp.load() && hasUsableTurnResult()) return true;
    if (postGenerationDrainComplete()) {
        Serial.println("[GeminiLive] Post-generation drain complete");
        return true;
    }
    return false;
}

bool didTurnSucceed() {
    TurnResult result = getTurnResult();

    if (sFailed.load() && (result.receivedTranscript || result.responseText.length() > 0)) {
        Serial.println("[GeminiLive] Socket closed after text response; treating as success");
        return true;
    }

    if (sFailed.load()) {
        if (!sLastError.length()) {
            sLastError = "Turn failed before completion";
        }
        Serial.println("[GeminiLive] Turn failed before completion");
        return false;
    }
    if (!sTurnDone.load() && !sGenDone.load() && sWsUp.load()) {
        return false;
    }
    if (!sTurnDone.load() && !sGenDone.load()) {
        if (result.receivedTranscript || result.responseText.length() > 0) {
            Serial.println("[GeminiLive] Text response received without turnComplete; treating as success");
            return true;
        }
        if (!sLastError.length()) {
            sLastError = "Turn timeout";
        }
        Serial.printf("[GeminiLive] Turn timeout — %s\n", sLastError.c_str());
        return false;
    }
    if (!result.receivedAudio && result.responseText.length() == 0) {
        if (!sLastError.length()) {
            sLastError = "Empty Gemini response";
        }
        Serial.printf("[GeminiLive] Empty response — %s\n", sLastError.c_str());
        return false;
    }
    return true;
}

bool sendAudioTurn(const uint8_t* pcm, size_t bytes) {
    beginAudioTurn();
    if (!sendAudioChunk(pcm, bytes)) {
        return false;
    }
    if (!endAudioTurn()) {
        return false;
    }

    uint32_t deadline = millis() + 30000;
    while (!isTurnSettled() && millis() < deadline) {
        xSemaphoreTake(sMutex, portMAX_DELAY);
        sWs.loop();
        xSemaphoreGive(sMutex);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!isTurnSettled() && !sLastError.length()) {
        sLastError = "Turn timeout";
    }
    return didTurnSucceed();
}

TurnResult getTurnResult() {
    if (!sMutex) return sTurnResult;
    TurnResult copy;
    xSemaphoreTake(sMutex, portMAX_DELAY);
    copy = sTurnResult;
    xSemaphoreGive(sMutex);
    return copy;
}

}  // namespace GeminiLiveHandler
