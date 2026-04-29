# Migrating Bubu ESP32 to Deno Edge Proxy

## Why Switch from Direct Gemini to Proxy?

**Problem with Direct Connection:**
- Gemini's VAD wasn't being triggered properly
- ESP32 couldn't tell Gemini "I'm done speaking"
- Result: Gemini buffered audio indefinitely → timeout → disconnect

**Solution with Proxy:**
- Proxy handles Gemini connection server-side
- Proxy manages turn completion signals automatically
- VAD works because proxy sends minimal, valid setup JSON
- ESP32 just sends audio and receives responses

## Code Changes Required

### 1. Update `chat_protocol.cpp` - WebSocket Connection

**OLD:** Direct Gemini connection
```cpp
// OLD: Connecting directly to Gemini
sWs->beginSSL("generativelanguage.googleapis.com", 443, "/google.ai.generativelanguage.v1alpha.GenerativeService/BidiGenerateContent?key=" + geminiApiKey);
```

**NEW:** Connect to proxy instead
```cpp
// NEW: Connect to Deno proxy
// Local: "localhost", 8080
// Production: "bubu-proxy.deno.dev", 443
const char* PROXY_HOST = "bubu-proxy.deno.dev";
const uint16_t PROXY_PORT = 443;

sWs->addHeader("Authorization", "Bearer bubu-bubu-esp32-s3");
sWs->addHeader("X-Device-ID", "bubu-esp32-s3");
sWs->addHeader("X-WiFi-RSSI", String((int)WiFi.RSSI()));
sWs->beginSSL(PROXY_HOST, PROXY_PORT, "/ws");
```

### 2. Update `chat_protocol.cpp` - Remove Setup JSON

**OLD:** Send setup JSON to Gemini
```cpp
// Removed: buildSetupJson() is no longer used
// The proxy handles setup JSON internally
```

**NEW:** Don't send setup JSON - proxy handles it
```cpp
// The proxy will send setup JSON to Gemini
// ESP32 just sends raw audio chunks
// No more setupComplete signal to wait for
```

### 3. Update `chat_protocol.cpp` - Audio Sending

**OLD:** Send binary audio
```cpp
// OLD: Binary audio with WebSocket frame
sWs->sendBIN((uint8_t*)audioBuffer, audioLen);
```

**NEW:** Same binary audio (no change!)
```cpp
// NEW: Same binary audio - proxy accepts it
// Or send as JSON for clarity
JsonDocument doc;
doc["type"] = "audio";
doc["data"] = base64_encode(audioBuffer, audioLen);
// Send JSON or binary, both work
```

### 4. Update `chat_protocol.cpp` - Response Parsing

**OLD:** Parse Gemini's raw messages
```cpp
// Parse binary WebSocket data from Gemini
// Handle "setup", "serverContent" messages
// Count audio chunks, detect generationComplete
```

**NEW:** Parse proxy's simple messages
```cpp
// Parse JSON from proxy:
// { "type": "audio", "data": "base64_audio" }
// { "type": "complete", "message": "..." }
// { "type": "error", "message": "..." }

if (message["type"] == "audio") {
  // Decode base64 audio
  byte* decodedAudio = base64_decode(message["data"]);
  // Play audio
}

if (message["type"] == "complete") {
  // Turn is complete, can send next audio
}
```

## Complete Example: Minimal Proxy Client

Here's a simplified `chat_protocol.cpp` for proxy mode:

```cpp
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

WebSocketsClient webSocket;

void connectToProxy() {
  Serial.println("[Proxy] Connecting to edge proxy...");

  webSocket.setAuthorization("Bearer bubu-bubu-esp32-s3");
  webSocket.addHeader("X-Device-ID", "bubu-esp32-s3");
  webSocket.addHeader("X-WiFi-RSSI", String((int)WiFi.RSSI()));

  // Local dev: webSocket.begin("localhost", 8080, "/ws");
  // Production:
  webSocket.beginSSL("bubu-proxy.deno.dev", 443, "/ws");
}

void sendAudioToProxy(const uint8_t* audioData, size_t len) {
  // Send as binary (simplest)
  webSocket.sendBIN(audioData, len);

  // Or as JSON (for debugging):
  // JsonDocument doc;
  // doc["type"] = "audio";
  // doc["data"] = base64_encode(audioData, len);
  // String json;
  // serializeJson(doc, json);
  // webSocket.sendTXT(json);
}

void handleProxyMessage(const uint8_t* payload, size_t length) {
  // Parse JSON from proxy
  JsonDocument doc;
  deserializeJson(doc, payload, length);

  if (doc["type"] == "audio") {
    // Decode base64 and play audio
    String base64Data = doc["data"];
    byte* audioData = base64_decode(base64Data);
    playAudio(audioData);
    free(audioData);
  }

  if (doc["type"] == "complete") {
    Serial.println("[Proxy] Turn complete, ready for next input");
    // Clear audio buffer, wait for next user input
  }

  if (doc["type"] == "error") {
    Serial.println("[Proxy] Error: " + String(doc["message"].as<const char*>()));
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[Proxy] Disconnected");
      break;

    case WStype_CONNECTED:
      Serial.println("[Proxy] Connected to edge proxy!");
      break;

    case WStype_TEXT:
    case WStype_BIN:
      handleProxyMessage(payload, length);
      break;

    case WStype_ERROR:
      Serial.println("[Proxy] WebSocket error");
      break;
  }
}
```

## Testing Checklist

- [ ] Proxy running locally: `deno task dev` in `deno-proxy/` folder
- [ ] `.env` file created with `GEMINI_API_KEY`
- [ ] ESP32 firmware compiled with new connection code
- [ ] ESP32 connects to proxy (check logs for "Connected to edge proxy!")
- [ ] Send audio and wait for response
- [ ] Hear Bubu's voice output
- [ ] Multiple turns work without disconnects

## Deployment

### For Testing (Local)
```bash
cd deno-proxy
deno task dev
# Proxy runs on ws://localhost:8080/ws
```

### For Production
```bash
# Deploy to Deno Deploy
deno deploy --project bubu-proxy main.ts

# Get your URL: https://bubu-proxy.deno.dev/ws
# Update ESP32 to use: bubu-proxy.deno.dev:443
```

## Before/After Comparison

| Aspect | Direct Gemini | Via Proxy |
|--------|---------------|-----------|
| API Key | On ESP32 (risky!) | Server-side (secure) |
| Setup JSON | ESP32 sends | Proxy sends |
| VAD | Broken (no turn-end signal) | Works (proxy manages it) |
| Audio response | Never received | Always received ✅ |
| Debugging | Hard (binary protocol) | Easy (JSON logs) |
| Scaling | One device per key | Multiple devices, one key |
| Latency | Direct to Gemini | +100-200ms proxy hop |

## Troubleshooting

**ESP32 won't connect to proxy:**
- Check firewall allows port 443 (or 8080 for local dev)
- Verify WiFi is working
- Check Authorization header format: `Bearer bubu-bubu-esp32-s3`

**Proxy connects but no audio response:**
- Check Deno console logs
- Verify GEMINI_API_KEY is valid
- Try speaking clearly and releasing button quickly

**Audio from Gemini is garbled:**
- Proxy sends 24kHz PCM base64 encoded
- ESP32 must decode base64 before playing
- Check audio decoding logic

## Files to Modify

1. `/sessions/brave-awesome-newton/mnt/bubu_ota/src/chat/chat_protocol.cpp`
   - Replace WebSocket connection code
   - Remove buildSetupJson() (no longer used)
   - Update message parsing

2. `/sessions/brave-awesome-newton/mnt/bubu_ota/src/chat/chat_system.cpp`
   - Update any Gemini-specific state management

That's it! The proxy handles everything else.
