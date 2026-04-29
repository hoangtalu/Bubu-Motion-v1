// 🐣 Modified chat_protocol.cpp for Deno Proxy Connection
// Replace the connectTaskFunc section (around lines 692-700) with this:

// ============================================================================
// MODIFIED: connectTaskFunc for Deno Proxy (instead of direct Gemini)
// ============================================================================

static void connectTaskFunc(void* param) {
    Serial.println("[Proxy] WebSocket task started on Core 0");

    // Configuration for proxy
    const char* PROXY_HOST = "localhost";        // LOCAL DEV - change to "bubu-proxy.deno.dev" for production
    const uint16_t PROXY_PORT = 8080;            // LOCAL DEV - change to 443 for production
    const char* PROXY_PATH = "/ws";

    Serial.printf("[Proxy] Connecting to %s:%u%s\n", PROXY_HOST, PROXY_PORT, PROXY_PATH);

    // Add authorization headers
    sWs->addHeader("Authorization", "Bearer bubu-bubu-esp32-s3");
    sWs->addHeader("X-Device-ID", "bubu-esp32-s3");
    sWs->addHeader("X-WiFi-RSSI", String((int)WiFi.RSSI()));

    // Connect to proxy (use HTTP for local dev, HTTPS for production)
    #ifdef PROXY_LOCAL_DEV
        // Local: plain HTTP connection
        sWs->begin(PROXY_HOST, PROXY_PORT, PROXY_PATH);
    #else
        // Production: SSL connection to Deno Deploy
        sWs->beginSSL(PROXY_HOST, PROXY_PORT, PROXY_PATH);
    #endif

    sWs->onEvent(onWsEvent);
    sWs->setReconnectInterval(5000);  // Proxy library will retry
    sWs->enableHeartbeat(15000, 3000, 2);

    // Single unified loop: drive sWs->loop() AND send mic audio
    uint32_t connectDeadline = millis() + 30000;  // 30s initial timeout
    uint32_t audioChunkCount = 0;

    while (sTaskShouldRun) {
        // Keep WebSocket alive
        sWs->loop();
        vTaskDelay(pdMS_TO_TICKS(1));  // Yield immediately after loop

        // Monitor WiFi health every 5s
        monitorWifiHealth();

        // --- Send pending mic audio ---
        if (sSessionReady && ChatAudio::isMicEnabled()) {
            QueueHandle_t q = ChatAudio::getMicQueue();
            if (q) {
                bool gotChunk = false;
                int16_t* chunk = (int16_t*)malloc(AUDIO_CHUNK_SAMPLES * sizeof(int16_t));
                if (chunk && xQueueReceive(q, chunk, 0)) {
                    gotChunk = true;

                    // IMPORTANT: For proxy, just send binary audio
                    // The proxy will handle base64 encoding and sending to Gemini
                    sWs->sendBIN((uint8_t*)chunk, AUDIO_CHUNK_SAMPLES * sizeof(int16_t));

                    audioChunkCount++;
                    if (audioChunkCount % 20 == 1) {
                        Serial.printf("[Proxy] Audio chunk #%u sent (%u bytes)\n",
                                    audioChunkCount, AUDIO_CHUNK_SAMPLES * sizeof(int16_t));
                    }
                }
                if (chunk) free(chunk);
            }
        }

        // Yield periodically to prevent watchdog timeout
        if (audioChunkCount % 2 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    Serial.println("[Proxy] WS task shutting down");
    vTaskDelete(NULL);
}

// ============================================================================
// MODIFIED: onWsEvent to handle proxy responses
// ============================================================================

static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[Proxy] Disconnected (was connected: %d, session ready: %d)\n",
                         sWsConnected, sSessionReady);
            sWsConnected = false;
            sSessionReady = false;
            sSetupSent = false;
            sSawDisconnectEvent = true;
            break;

        case WStype_CONNECTED:
            Serial.println("[Proxy] ✅ Connected to Deno proxy!");
            sWsConnected = true;
            // NOTE: Proxy auto-establishes Gemini connection on first audio
            // No setup JSON needed - proxy handles it!
            sSessionReady = true;  // Ready to send audio immediately
            break;

        case WStype_TEXT: {
            // Parse JSON response from proxy
            JsonDocument doc;
            deserializeJson(doc, payload, length);

            const char* type = doc["type"].as<const char*>();

            if (strcmp(type, "audio") == 0) {
                // Audio response from Gemini (via proxy)
                const char* base64Audio = doc["data"].as<const char*>();
                if (base64Audio) {
                    Serial.printf("[Proxy] Received audio response (%u bytes)\n", strlen(base64Audio));

                    // Decode base64 audio and queue for playback
                    size_t decodedLen = 0;
                    uint8_t* decodedAudio = (uint8_t*)malloc(strlen(base64Audio));

                    if (mbedtls_base64_decode(
                        decodedAudio, strlen(base64Audio), &decodedLen,
                        (const uint8_t*)base64Audio, strlen(base64Audio)) == 0) {

                        // Queue audio for playback
                        if (sPlaybackQueue) {
                            xQueueSend(sPlaybackQueue, decodedAudio, 0);
                        }
                    }
                    free(decodedAudio);
                }
            }
            else if (strcmp(type, "complete") == 0) {
                // Turn complete
                Serial.println("[Proxy] ✅ Response complete, ready for next input");
            }
            else if (strcmp(type, "status") == 0) {
                const char* message = doc["message"].as<const char*>();
                Serial.printf("[Proxy] Status: %s\n", message);
            }
            else if (strcmp(type, "error") == 0) {
                const char* message = doc["message"].as<const char*>();
                Serial.printf("[Proxy] ❌ Error: %s\n", message);
            }
            break;
        }

        case WStype_BIN:
            // Binary audio response from proxy
            Serial.printf("[Proxy] Received binary audio (%u bytes)\n", length);
            if (sPlaybackQueue) {
                xQueueSend(sPlaybackQueue, payload, 0);
            }
            break;

        case WStype_ERROR:
            Serial.printf("[Proxy] WebSocket error\n");
            sWsConnected = false;
            sSessionReady = false;
            break;

        case WStype_PING:
            Serial.println("[Proxy] Ping received");
            break;

        case WStype_PONG:
            Serial.println("[Proxy] Pong received");
            break;

        default:
            Serial.printf("[Proxy] Unknown WebSocket type: %d\n", type);
    }
}

// ============================================================================
// NOTE: Remove buildSetupJson() - Proxy handles setup internally!
// ============================================================================
// The buildSetupJson() function is NO LONGER NEEDED because:
//   - The proxy sends the setup JSON to Gemini automatically
//   - The ESP32 just sends raw audio chunks
//   - The proxy sends responses back as JSON/binary
//
// If you see calls to buildSetupJson() or sendSetup(), remove them!

// ============================================================================
// COMPILATION FLAGS
// ============================================================================
// Compile with: -DPROXY_LOCAL_DEV for local testing
// Or modify PROXY_HOST/PROXY_PORT above for production

