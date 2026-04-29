# 🐣 Bubu V2: Complete Implementation Plan

## Executive Summary

We're **completely redesigning** Bubu's Gemini integration around a **text-based API flow** instead of the problematic Gemini Live API.

```
Audio (mic)
    ↓ [Google STT]
Text ("hello bubu")
    ↓ [Gemini API]
Response Text ("Hi!")
    ↓ [Google TTS]
Audio (speakers)
```

**Result**: Simple, stable, proven architecture that works reliably.

---

## What You Need to Do NOW

### 1️⃣ Get API Keys (30 minutes)

Follow **SETUP_V2_GUIDE.md** Phase 1-2:

**Google Cloud:**
- Create project: `bubu-esp32`
- Enable APIs: Speech-to-Text, Text-to-Speech
- Create API Key → `GOOGLE_CLOUD_API_KEY`

**Gemini:**
- Go to: https://aistudio.google.com/apikey
- Create key → `GEMINI_API_KEY`

Once you have these 2 keys, we proceed to code.

### 2️⃣ Tell Me You're Ready

Just confirm:
- [ ] Google Cloud project created
- [ ] API key obtained (`AIzaSy...`)
- [ ] Gemini key obtained (`sk-proj-...`)

---

## Architecture Overview

### Current State ❌
```
ESP32 → Gemini Live API (WebSocket)
         ↓
      (Complex streaming, VAD issues, timeouts)
         ↓
      ❌ Disconnects frequently
      ❌ Audio response unreliable
      ❌ Protocol very complex
```

### New State ✅
```
ESP32 → Google STT API (HTTP)
    ↓
    └→ Gemini Text API (HTTP)
    ↓
    └→ Google TTS API (HTTP)
    ↓
    ✅ Simple HTTP requests
    ✅ Proven stable APIs
    ✅ Easy to debug
    ✅ Free tier covers most uses
```

---

## Implementation Roadmap

### Phase A: Setup (You do this)
- [ ] Create Google Cloud project
- [ ] Get API keys
- [ ] Add keys to ESP32 config

### Phase B: Core APIs (I will code)
- [ ] **api_handler.cpp** - HTTP request/response wrapper
- [ ] **stt_handler.cpp** - Speech-to-Text integration
- [ ] **gemini_handler.cpp** - Gemini text API integration
- [ ] **tts_handler.cpp** - Text-to-Speech integration

### Phase C: State Machine (I will code)
- [ ] Update **chat_system.cpp** for new workflow
- [ ] New states: IDLE → CONNECTING → LISTENING → PROCESSING → PLAYING → IDLE
- [ ] Eye tap handling
- [ ] Long tap handling

### Phase D: UI/Display (I will code)
- [ ] Indicator system: "?" (listening) → "..." (processing) → "!!!" (connected)
- [ ] Text box display
- [ ] Integration with existing display

### Phase E: Integration & Testing (We both do this)
- [ ] Wire everything together
- [ ] Test STT → Gemini → TTS pipeline
- [ ] Test full user workflow
- [ ] Optimize and refine

---

## Code Structure

```
src/chat/
├── chat_system.cpp/h       (state machine - MODIFIED)
├── api_handler.cpp/h        (NEW - HTTP wrapper)
├── stt_handler.cpp/h        (NEW - Speech-to-Text)
├── gemini_handler.cpp/h     (NEW - Gemini text API)
├── tts_handler.cpp/h        (NEW - Text-to-Speech)
├── display_system.cpp/h     (indicator system - MODIFIED)
└── chat_config.h            (NEW - API keys and config)
```

---

## Workflow State Machine

```
┌─────────────────────────────────────────────────────────────┐
│                         IDLE                                │
│                  (no indicator)                             │
│            User taps eye to connect                         │
└────────────┬────────────────────────────────────────────────┘
             │
             ↓
┌─────────────────────────────────────────────────────────────┐
│                      CONNECTING                             │
│                 Indicator: "..."                            │
│         Test Gemini API connection                          │
└────┬────────────────────────────┬───────────────────────────┘
     │                            │
  Success                       Fail
     │                            │
     ↓                            ↓
┌──────────────────┐    ┌──────────────────┐
│    CONNECTED     │    │       ERROR      │
│  Indicator: "!!!"│    │  Indicator: "xxx"│
└───────┬──────────┘    └────────┬─────────┘
        │                        │
        │ User taps eye          │ User taps eye again
        │                        │
        ↓                        ↓
┌──────────────────┐    ┌──────────────────┐
│    LISTENING     │    └─→ CONNECTING    │
│  Indicator: "?"  │
└───────┬──────────┘
        │ (Recording)
        │ User taps eye to send
        ↓
┌──────────────────────────────────────────────────┐
│           PROCESSING                             │
│    Indicator: "..." (loading)                   │
│  1. STT: audio → text                           │
│  2. Gemini: text → response                     │
│  3. TTS: response → audio                       │
└───────┬──────────────────────────────────────────┘
        │
        ↓
┌──────────────────────────────────────────────────┐
│            PLAYING                               │
│    Indicator: "???" (next turn ready)           │
│  • Audio playing                                │
│  • Text displayed in box                        │
│  • User can tap to close text or tap eye again  │
└─────┬──────────────┬──────────────────────────┬──┘
      │              │                          │
  Tap anywhere   Tap eye     Long tap eye      (auto-return)
  (close text)  (next turn)   (stop session)    after audio
      │              │                          │
      ↓              ↓                          ↓
  Back to        LISTENING              IDLE (disconnected)
  PLAYING             │
                      ↓
                  PROCESSING
```

---

## API Calls (Exact Flows)

### Call Sequence: User Speaks → Response

```
1. USER TAPS EYE (while LISTENING)
   └─→ Stop recording, get PCM audio bytes

2. CALL: Speech-to-Text
   POST /speech:recognize
   Input: audio bytes (base64)
   Output: "what the user said"
   Time: 500-2000ms

3. CALL: Gemini Text API
   POST /generateContent
   Input: "what the user said"
   Output: "Gemini's response text"
   Time: 1-3 seconds

4. CALL: Text-to-Speech
   POST /text:synthesize
   Input: "Gemini's response text"
   Output: audio bytes (MP3)
   Time: 1-2 seconds

5. PLAYBACK
   └─→ Play audio via speakers
   └─→ Display text in text box
   └─→ Set indicator "???" (ready for next turn)
```

**Total time**: 3-7 seconds (acceptable)

---

## Error Handling

### Network Error → Connection Lost
```
Any API call fails → Dialog: "Connection lost"
→ User can tap eye again to reconnect
→ State returns to IDLE
```

### STT Failed (no speech detected)
```
STT returns empty result → Dialog: "Didn't catch that"
→ Stay in LISTENING mode
→ User can try again
```

### Gemini Error
```
Gemini returns error → Dialog: "I'm confused"
→ Return to LISTENING
→ User can try different question
```

### TTS Error
```
TTS fails → Dialog: "Can't speak right now"
→ Display text only (don't crash)
→ Return to LISTENING
```

---

## Testing Strategy

### Unit Tests (Each API independently)
1. **Test STT alone**
   - Record audio → send to Google STT → verify text output

2. **Test Gemini alone**
   - Send text → get response → verify it's sensible

3. **Test TTS alone**
   - Send text → get audio → verify audio plays

### Integration Test (Full pipeline)
```
Record: "hello bubu"
  ↓ (STT)
Get: "hello bubu"
  ↓ (Gemini)
Get: "Hi there!"
  ↓ (TTS)
Get: audio bytes
  ↓ (Play)
Hear: Bubu's voice
Display: "Hi there!"
```

### User Workflow Test
```
1. Tap eye → see "!!!" (connected)
2. Tap eye → see "?" (listening)
3. Speak: "hello bubu"
4. Tap eye → see "..." (processing)
5. Wait → hear response
6. See text in box
7. Indicator: "???" (ready again)
8. Tap screen → text closes
9. Long tap eye → disconnect
10. Indicator gone
```

---

## Success Criteria ✅

When you can do this **reliably, 10 times in a row**:

1. **Connect**: Tap eye → "!!!" appears (Gemini reachable)
2. **Listen**: Tap eye → "?" appears (recording starts)
3. **Speak**: Say something clearly (10+ seconds)
4. **Send**: Tap eye → "..." appears (processing)
5. **Hear**: Wait 3-7 seconds → **Hear Bubu's voice response**
6. **Read**: See text box with response
7. **Reset**: Tap screen → box closes, "???" shows
8. **Repeat**: Can do steps 2-6 again multiple times
9. **Stop**: Long tap eye → all indicators disappear
10. **Verify**: No crashes, all steps smooth

---

## Timeline Estimate

| Phase | Task | Effort | Timeline |
|-------|------|--------|----------|
| A | Get API keys | 30 min | Today |
| B | Code API handlers | 2-3 hrs | 1-2 days |
| C | Update state machine | 1-2 hrs | 1 day |
| D | UI/indicator system | 1 hr | 1 day |
| E | Integration & test | 2-3 hrs | 1-2 days |
| **TOTAL** | | **7-10 hours** | **1 week** |

---

## Files Modified/Created Summary

### New Files (5)
- `api_handler.cpp/h` - HTTP wrapper for all APIs
- `stt_handler.cpp/h` - STT integration
- `gemini_handler.cpp/h` - Gemini text API
- `tts_handler.cpp/h` - TTS integration
- `chat_config.h` - API keys and config

### Modified Files (3)
- `chat_system.cpp` - State machine redesign
- `display_system.cpp` - New indicator system
- `main.cpp` - Eye tap and long-tap handlers

### Removed Files (1)
- `chat_protocol.cpp` - No longer needed (Gemini Live API code)

---

## Why This Works

✅ **Simple**: HTTP APIs, standard JSON, no weird protocols
✅ **Proven**: Google APIs used by millions of apps
✅ **Reliable**: No more VAD timeouts or disconnects
✅ **Fast**: 3-7 seconds is acceptable for real-time conversation
✅ **Free**: All services have generous free tiers
✅ **Debuggable**: Can test each API independently
✅ **Scalable**: Easy to add features (emotions, memory, etc.)
✅ **Maintainable**: Clear separation of concerns

---

## Your Next Action

1. **Read**: `SETUP_V2_GUIDE.md`
2. **Create**: Google Cloud project
3. **Get**: API keys
4. **Tell me**: "I have the keys" with:
   - Google API key (first 20 chars): `AIzaSy...`
   - Gemini key (first 20 chars): `sk-proj-...`

Once you confirm, I'll start coding the API handlers! 🚀

---

**This is the right approach.** Let's build something that actually works! 🐣
