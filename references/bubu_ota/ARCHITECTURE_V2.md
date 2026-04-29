# 🐣 Bubu V2 Architecture - Text-Based API Flow

## Overview

A **much simpler and more reliable** approach using text-based APIs instead of complex audio streaming protocols.

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3 (Bubu)                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Microphone Audio                                          │
│       ↓                                                    │
│  ┌─────────────────────────────────────────────────────┐ │
│  │ 1. SPEECH-TO-TEXT (Google Cloud)                    │ │
│  │    audio → text                                     │ │
│  │    "hello bubu"                                     │ │
│  └─────────────────────────────────────────────────────┘ │
│       ↓                                                    │
│  ┌─────────────────────────────────────────────────────┐ │
│  │ 2. GEMINI TEXT API (Google)                         │ │
│  │    "hello bubu" → gemini API → response text       │ │
│  │    "Hi! I'm happy to help you!"                    │ │
│  └─────────────────────────────────────────────────────┘ │
│       ↓                                                    │
│  ┌─────────────────────────────────────────────────────┐ │
│  │ 3. TEXT-TO-SPEECH (Google Cloud)                    │ │
│  │    text → audio                                     │ │
│  │    MP3/WAV bytes                                    │ │
│  └─────────────────────────────────────────────────────┘ │
│       ↓                                                    │
│  Speakers Output + Text Display                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Why This Approach? ✨

| Aspect | Gemini Live API (Old) | Text API (New) |
|--------|----------------------|----------------|
| **Complexity** | Very high (streaming, VAD, protocol) | Simple (HTTP requests) |
| **VAD Issues** | ❌ We struggled with "listening mode" timeout | ✅ No VAD needed - explicit tap to send |
| **Audio Handling** | ❌ Direct PCM streaming, format issues | ✅ Standard Google APIs |
| **Latency** | ~2-3s per round trip | ~2-3s per round trip (same) |
| **Reliability** | ❌ Frequent disconnects (len=0) | ✅ Proven stable |
| **Cost** | Free but complicated | Free tier available |
| **Development Time** | Weeks of debugging | Days to implement |

---

## API Services Required

### 1. **Google Cloud Speech-to-Text** ✅
- **What**: Convert audio (PCM/WAV) → text
- **Cost**: Free tier: 60 minutes/month
- **Input**: 16kHz 16-bit mono PCM
- **Output**: Text transcription

### 2. **Google Gemini Text API** ✅
- **What**: Chat/text completion
- **Model**: `gemini-2.5-flash` (fast, free)
- **Input**: Text prompt
- **Output**: Text response (final answer, no thoughts)

### 3. **Google Cloud Text-to-Speech** ✅
- **What**: Convert text → audio (MP3/WAV)
- **Cost**: Free tier: 1 million characters/month
- **Input**: Text
- **Output**: Audio bytes (MP3 or WAV)

---

## API Keys Required

You'll need these API keys/credentials:

```
1. Google Cloud Project ID: "your-project-id"
2. Google Cloud API Key: "AIzaSy..." (for STT + TTS)
3. Gemini API Key: "sk-proj-..." (for text API)
```

All can be created free at:
- https://console.cloud.google.com
- https://aistudio.google.com

---

## Data Flow

### Boot → Connection Phase
```
1. ESP32 boots
2. Connect WiFi
3. User taps eye
4. ESP32 makes test call to Gemini API
5. Indicator: "..." (loading) → "!!!" (connected)
```

### Audio Input Phase
```
1. User taps eye again
2. Indicator: "?" (ready for audio)
3. Microphone starts recording
4. User speaks
```

### Audio Submission Phase
```
1. User taps eye to send audio
2. Indicator: "..." (processing)
3. ESP32:
   - STT: audio → text (Google Speech-to-Text)
   - Gemini: text → response text (Google Gemini API)
   - TTS: response text → audio (Google Text-to-Speech)
   - Play audio
   - Display text in box
```

### Response Display Phase
```
1. Response text shown in text box
2. Indicator: "???" (ready for next turn)
3. Bubu speaks via TTS audio
4. User can:
   - Tap anywhere to close text box
   - Tap eye again to send next message
   - Long tap eye to end session
```

---

## ESP32 HTTP API Calls

### Call 1: Speech-to-Text
```
POST https://speech.googleapis.com/v1/speech:recognize?key=YOUR_API_KEY
Content-Type: application/json

{
  "config": {
    "encoding": "LINEAR16",
    "sampleRateHertz": 16000,
    "languageCode": "en-US"
  },
  "audio": {
    "content": "base64_encoded_audio_bytes"
  }
}

Response:
{
  "results": [{
    "alternatives": [{
      "transcript": "hello bubu"
    }]
  }]
}
```

### Call 2: Gemini Text API
```
POST https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=YOUR_API_KEY
Content-Type: application/json

{
  "contents": [{
    "role": "user",
    "parts": [{
      "text": "hello bubu"
    }]
  }],
  "generationConfig": {
    "temperature": 0.7,
    "maxOutputTokens": 256
  }
}

Response:
{
  "candidates": [{
    "content": {
      "parts": [{
        "text": "Hi! I'm happy to help you!"
      }]
    }
  }]
}
```

### Call 3: Text-to-Speech
```
POST https://texttospeech.googleapis.com/v1/text:synthesize?key=YOUR_API_KEY
Content-Type: application/json

{
  "input": {
    "text": "Hi! I'm happy to help you!"
  },
  "voice": {
    "languageCode": "en-US",
    "name": "en-US-Neural2-C"
  },
  "audioConfig": {
    "audioEncoding": "MP3"
  }
}

Response:
{
  "audioContent": "//NExAAjvgLDAAVAAKAAUAAAASUGQBAADAAAAA..."
}
```

---

## Libraries Needed (ESP32)

```
ArduinoJson          - JSON parsing
HTTPClient           - HTTP requests
WiFi                 - WiFi connection
SD.h / SPIFFS        - Storage for audio cache (optional)
I2S                  - Audio I/O
Base64 encoding      - For audio payload encoding
```

---

## Advantages of This Approach

✅ **Simple**: Just HTTP requests, no complex protocols
✅ **Reliable**: Google APIs are stable and well-tested
✅ **Fast**: Same latency as Gemini Live (2-3s)
✅ **Flexible**: Can change STT/TTS/Model independently
✅ **Debuggable**: Can test each step separately
✅ **Free**: Free tiers for all three services
✅ **Proven**: Millions of apps use these APIs
✅ **Scalable**: Easy to add more features later

---

## Next Steps

1. ✅ Set up Google Cloud project
2. ✅ Get API keys for STT, TTS, Gemini
3. ✅ Create ESP32 HTTP client for each API
4. ✅ Update UI state machine for new workflow
5. ✅ Implement audio recording/playback
6. ✅ Test STT → Gemini → TTS pipeline
7. ✅ Integrate with Bubu's display system

---

## Cost Estimates (Monthly)

| Service | Free Tier | Per Unit | Est. Cost (100 uses/month) |
|---------|-----------|----------|---------------------------|
| Speech-to-Text | 60 min | $0.006 per 15s | ~$0.12 |
| Text-to-Speech | 1M chars | $0.000004 per char | ~$0.02 |
| Gemini Text API | Free tier | $0 (free) | $0 |
| **Total** | ✅ All free for hobbyist use | | ~$0.14 |

---

## Success Criteria

- [x] Architecture is simple and clear
- [ ] API keys are configured
- [ ] ESP32 can make HTTPS requests to Google APIs
- [ ] STT converts audio → text correctly
- [ ] Gemini API returns sensible responses
- [ ] TTS converts response text → audio
- [ ] UI indicators work correctly
- [ ] Audio input/output flows smoothly
- [ ] Multiple turns work without disconnect
- [ ] Long-tap stops session cleanly

