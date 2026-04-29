# 🐣 Bubu V2: Setup Guide

## Phase 1: Google Cloud Setup

### Step 1.1: Create Google Cloud Project
1. Go to https://console.cloud.google.com/
2. Click "Select a Project" → "New Project"
3. Name it: `bubu-esp32`
4. Create it

### Step 1.2: Enable Required APIs
In Google Cloud Console, search and enable:

1. **Speech-to-Text API**
   - Search: "Cloud Speech-to-Text"
   - Click "Enable"

2. **Text-to-Speech API**
   - Search: "Cloud Text-to-Speech"
   - Click "Enable"

### Step 1.3: Create API Key
1. Go to: "Credentials" (left menu)
2. Click: "+ Create Credentials" → "API Key"
3. Copy the API key (you'll need this)
4. Restrict it to Google Cloud APIs (optional but recommended)

**Save this as:** `GOOGLE_CLOUD_API_KEY`

### Step 1.4: Get Gemini API Key
1. Go to: https://aistudio.google.com/apikey
2. Click: "+ Create API Key"
3. Select project: `bubu-esp32`
4. Copy the key

**Save this as:** `GEMINI_API_KEY`

---

## Phase 2: Configure ESP32

### Step 2.1: Store API Keys Securely

Create a new file: `src/chat/chat_config.h`

```cpp
#pragma once

// Google Cloud APIs
#define GOOGLE_CLOUD_API_KEY "AIzaSy..."  // Replace with your key
#define GOOGLE_CLOUD_PROJECT_ID "bubu-esp32"

// Gemini API
#define GEMINI_API_KEY "sk-proj-..."  // Replace with your key

// Speech-to-Text config
#define STT_LANGUAGE_CODE "en-US"
#define STT_SAMPLE_RATE 16000
#define STT_MAX_DURATION_MS 10000  // 10 seconds max

// Text-to-Speech config
#define TTS_LANGUAGE_CODE "en-US"
#define TTS_VOICE_NAME "en-US-Neural2-C"  // Options: A, B, C (different voices)
#define TTS_AUDIO_ENCODING "MP3"

// Gemini config
#define GEMINI_MODEL "gemini-2.5-flash"
#define GEMINI_TEMPERATURE 0.7
#define GEMINI_MAX_TOKENS 256
```

### Step 2.2: Required Libraries

In `platformio.ini`, add:

```ini
[bubu_s3_n16r8]
...
lib_deps =
    ArduinoJson@^7.0.0
    HTTPClient
    ...existing libs...
```

Most are already included with ESP32 Arduino core.

### Step 2.3: WiFi Connection
Make sure WiFi is stable. In `src/wifi_service.cpp`, ensure:
- WiFi connects on boot
- WiFi stays connected during operation
- RSSI > -75 dBm for reliable API calls

---

## Phase 3: Understanding the New Workflow

### Boot Sequence

```
1. ESP32 boots
2. WiFi connects
3. Display shows idle state (no indicator)
4. Ready for user tap
```

### Tap Eye → Connect Phase

```
User action: TAP EYE
    ↓
ESP32 makes test call to Gemini API
    ↓
Indicator: "..." (loading, 2-3 seconds)
    ↓
If success: Indicator: "!!!" (connected)
If fail: Indicator: "xxx" (error)
    ↓
Ready for next action
```

### Tap Eye Again → Start Listening

```
User action: TAP EYE (while connected)
    ↓
Microphone starts recording
Indicator: "?" (ready for audio)
    ↓
User can now speak
```

### Tap Eye to Send → Process

```
User action: TAP EYE (while recording)
    ↓
Microphone stops
Indicator: "..." (processing)
    ↓
ESP32 processes:
  1. STT: audio → text (500-2000ms)
  2. Gemini: text → response (1-3s)
  3. TTS: text → audio (1-2s)
    ↓
Indicator: "..." changes to "!!!" when ready
    ↓
Audio plays automatically
Text appears in text box
Indicator: "???" (ready for next turn)
```

### Tap Anywhere → Close Text Box

```
User action: TAP (anywhere else)
    ↓
Text box closes
Indicator: "???" (ready for next message)
    ↓
Can go back to "Tap Eye to record" step
```

### Long Tap Eye → End Session

```
User action: LONG TAP EYE
    ↓
Session ends
All indicators disappear
    ↓
Back to idle state
    ↓
User can tap eye again to start new session
```

---

## Phase 4: Required Changes to Code

### Files to Create
- `src/chat/api_handler.cpp/h` - HTTP API calls
- `src/chat/stt_handler.cpp/h` - Speech-to-Text
- `src/chat/gemini_handler.cpp/h` - Gemini text API
- `src/chat/tts_handler.cpp/h` - Text-to-Speech

### Files to Modify
- `src/chat/chat_system.cpp` - New state machine
- `src/main.cpp` - Add button handlers
- `src/display_system.cpp` - New indicator system ("?", "...", "!!!")

### Remove
- `src/chat/chat_protocol.cpp` - Not needed anymore (was for Gemini Live API)

---

## Phase 5: Testing Checklist

Before moving to implementation, verify:

- [ ] Google Cloud project created
- [ ] APIs enabled (Speech-to-Text, Text-to-Speech)
- [ ] API key created
- [ ] Gemini API key created
- [ ] WiFi works on ESP32
- [ ] Can make HTTPS requests from ESP32
- [ ] JSON parsing works (ArduinoJson)
- [ ] Microphone audio capture working
- [ ] Speaker audio playback working
- [ ] Display/indicator system ready
- [ ] Button/touch input working

---

## Security Notes

⚠️ **IMPORTANT**: Never commit API keys to git!

Instead:
1. Store keys in `src/chat/chat_config.h` (add to `.gitignore`)
2. Or use environment variables during build
3. Or use SPIFFS to store keys encrypted on device

```
.gitignore should include:
src/chat/chat_config.h
credentials/
secrets/
```

---

## Cost Breakdown

For 100 interactions/month (conservative estimate):

| Service | Free Tier | Usage | Cost |
|---------|-----------|-------|------|
| Speech-to-Text | 60 min | 100 × 10s = 1000s = 16.7 min | $0 (under free tier) |
| Text-to-Speech | 1M chars | 100 × 200 chars = 20K chars | $0 (under free tier) |
| Gemini | Free | 100 requests | $0 (free tier) |
| **Total** | | | **$0/month** |

For 1000 interactions/month (heavy use):

| Service | Free Tier | Usage | Cost |
|---------|-----------|-------|------|
| Speech-to-Text | 60 min | 1000 × 10s = 10000s = 166.7 min | ~$0.60 |
| Text-to-Speech | 1M chars | 1000 × 200 = 200K chars | $0.001 |
| Gemini | Free | 1000 requests | $0 (free tier) |
| **Total** | | | **~$0.61/month** |

---

## What's Different from V1

| Aspect | V1 (Gemini Live API) | V2 (Text APIs) |
|--------|---------------------|----------------|
| Protocol | WebSocket + binary | REST + JSON |
| Audio handling | Direct streaming | Upload as base64 |
| Text response | Via audio stream | JSON text field |
| Latency | 2-3s | 2-3s |
| Complexity | ⭐⭐⭐⭐⭐ | ⭐ |
| Reliability | ❌ Issues | ✅ Proven |
| Libraries | WebSocketsClient + complex | HTTPClient + ArduinoJson |

---

## Next Steps

1. ✅ **Read this guide** ← You are here
2. ✅ **Create Google Cloud project**
3. ✅ **Get API keys**
4. ✅ **Design code structure** (src/chat/api_handler.cpp, etc.)
5. ✅ **Implement HTTP handlers**
6. ✅ **Update state machine**
7. ✅ **Test each API separately**
8. ✅ **Integrate and test end-to-end**
9. ✅ **Optimize and deploy**

---

**Ready to start Phase 1?** Let me know once you have the API keys! 🚀
