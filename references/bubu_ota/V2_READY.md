# 🐣 Bubu V2 — Ready to Build

## What Changed (V2 vs V1)

| Before (V1) | After (V2) |
|-------------|------------|
| Gemini Live API (WebSocket) | Direct REST HTTP calls |
| Audio streamed to Gemini | Audio → Google STT → text |
| Gemini replies with audio | Gemini replies with text |
| Complex VAD/streaming | Push-to-talk, explicit tap |

---

## New Files

```
src/chat/api_handler.h/.cpp    - HTTPS POST wrapper (all APIs)
src/chat/stt_handler.h/.cpp    - Google Cloud Speech-to-Text
src/chat/gemini_handler.h/.cpp - Gemini text API (multi-turn)
src/chat/tts_handler.h/.cpp    - Google Cloud Text-to-Speech
```

## Modified Files

```
src/chat/chat_system.h/.cpp    - New state machine
src/chat/chat_audio.h/.cpp     - Owns playback queue (no WebSocket dep)
src/chat/chat_protocol.h/.cpp  - Gutted to empty stubs
src/display_system.cpp         - Updated indicators + long-press endSession
include/chat_config.h          - Updated for V2 (backward-compatible)
```

---

## How to Build

```bash
platformio run --environment bubu_s3_n16r8
```

---

## Set Your API Key (Serial Monitor at 115200)

Once flashed, open serial monitor and type:
```
chat key AIzaSy...YOUR_KEY_HERE...
```

This same key is used for:
- Google Cloud Speech-to-Text
- Gemini text API
- Google Cloud Text-to-Speech

To verify it's set:
```
chat status
```

---

## User Flow

| Action | State | Indicator |
|--------|-------|-----------|
| Boot | DISCONNECTED | (none) |
| Tap eye | CONNECTING → IDLE | `...` → `!!!` |
| Tap eye again | LISTENING | `?` |
| Speak, then tap eye | WAITING | `...` |
| Bubu answers | SPEAKING | `???` |
| Tap anywhere | (close text box) | `???` |
| Tap eye again | LISTENING (next turn) | `?` |
| Long-tap eye | DISCONNECTED | (none) |

---

## If It Doesn't Work

1. **`!!!` doesn't appear** — API key wrong, or Google Cloud STT/TTS APIs not enabled
2. **`...` spins forever** — WiFi issue or no internet
3. **"Didn't catch that"** — Speak louder/closer, or check STT language (set to `en-US`)
4. **Text shows but no audio** — TTS failed; check key has TTS API enabled
5. **Set key again**: `chat key YOUR_KEY` in serial monitor

---

## Cost (free tier)

For ~100 voice interactions/month: **$0**
- STT: 60 min free/month
- TTS: 1M chars free/month
- Gemini: free tier unlimited
