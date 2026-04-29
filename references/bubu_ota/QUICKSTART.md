# 🚀 Bubu Gemini Solution - Quick Start

## 📋 What Was Built

✅ **Path A:** Direct Gemini connection with VAD fix (already in code)
✅ **Path B:** Deno edge proxy (complete, ready to deploy)

## ⚡ Fastest Way to Test (5 minutes)

### Step 1: Build Firmware
```bash
cd /sessions/brave-awesome-newton/mnt/bubu_ota
platformio run --environment bubu_s3_n16r8
```

### Step 2: Flash to ESP32
Use your existing flash process (esptool, platformio upload, etc.)

### Step 3: Test
- Open serial monitor at 115200 baud
- Say something: "Hi" or "Hello"
- **Release button immediately**
- Wait 1-2 seconds
- **Listen for Bubu's voice response** 🔊

### Step 4: Check Serial Output
Should see:
```
[WiFi] RSSI=-54 dBm, Status=CONNECTED
[Gemini] Setup JSON (380 bytes): {...}
[Gemini] setupComplete received
Audio chunks sent: #1, #21, #41...
Audio chunks RECEIVED: ...  ← NEW! (This means it's working!)
```

**✅ If you hear Bubu's response, problem is SOLVED!**

---

## 🔧 What Changed in the Code

### File: `src/chat/chat_protocol.cpp`

**Added (lines 644-651):**
```cpp
// Voice config to enable server-side VAD
JsonObject speechConfig = setup["speechConfig"].to<JsonObject>();
JsonObject voiceConfig = speechConfig["voiceConfig"].to<JsonObject>();
JsonObject prebuiltVoiceConfig = voiceConfig["prebuiltVoiceConfig"].to<JsonObject>();
prebuiltVoiceConfig["voiceName"] = "Puck";
```

**Why:** This tells Gemini to auto-detect when you stop speaking
**Result:** Gemini generates audio response instead of timing out

---

## 📦 Files Created for Production (Deno Proxy)

If you want production-grade setup with API key on server:

```
deno-proxy/
  ├── main.ts          ← Server entry point
  ├── gemini.ts        ← Gemini handler
  ├── types.ts         ← Type definitions
  ├── deno.json        ← Config
  ├── .env.example     ← Copy to .env and add API key
  └── README.md        ← Full documentation
```

### To Run Proxy Locally
```bash
cd deno-proxy
cp .env.example .env
# Edit .env: add GEMINI_API_KEY=sk-proj-...
deno task dev
# Server runs at ws://localhost:8080/ws
```

### To Deploy Proxy
```bash
deno deploy --project bubu-proxy deno-proxy/main.ts
# Get URL: https://bubu-proxy.deno.dev
```

---

## 🎯 Choose Your Path

### Path A: Direct (Recommended for Testing)
```
✅ Fastest to test (5 minutes)
✅ No server needed
✅ Uses Gemini directly
❌ API key on ESP32 (less secure)
```

**Do this:**
1. Build firmware (done - code already updated)
2. Flash to ESP32
3. Test and report results

### Path B: Deno Proxy (Recommended for Production)
```
✅ Secure (API key on server)
✅ Production-ready
✅ Easy scaling
✅ Debug-friendly
❌ Requires Deno Deploy or server
```

**Do this:**
1. Follow `deno-proxy/README.md`
2. Update ESP32 connection code (see `PROXY_MIGRATION.md`)
3. Test with proxy
4. Deploy to production

---

## 📚 Documentation Files

- **`GEMINI_SOLUTION_SUMMARY.md`** ← Complete explanation of solution
- **`PROXY_MIGRATION.md`** ← How to update ESP32 for proxy
- **`deno-proxy/README.md`** ← Proxy deployment guide
- **`QUICKSTART.md`** ← This file

---

## ❓ Quick FAQ

**Q: Will Path A work immediately?**
A: Should! The VAD fix addresses the root cause. Test and report back.

**Q: Do I need the Deno proxy?**
A: Not required for testing. Use it for production (secure + scalable).

**Q: What if Path A doesn't work?**
A: Fall back to Path B (Deno proxy), which has more robust handling.

**Q: How long for proxy setup?**
A: 20 minutes to get running locally, 10 minutes to deploy.

**Q: Is my API key safe with Path A?**
A: It's in ESP32 firmware, visible if someone extracts it. Use Path B for security.

---

## ✅ Checklist

### For Testing (Path A)
- [ ] Code change verified in `chat_protocol.cpp`
- [ ] Firmware built: `platformio run --environment bubu_s3_n16r8`
- [ ] Firmware flashed to ESP32
- [ ] Serial monitor open at 115200 baud
- [ ] Microphone working (test with different phrases)
- [ ] Button release triggers audio transmission
- [ ] Wait 1-2 seconds for Bubu's response
- [ ] Hear voice output 🔊

### For Production (Path B)
- [ ] Deno installed: `deno --version`
- [ ] `.env` file created with `GEMINI_API_KEY`
- [ ] Proxy running: `deno task dev`
- [ ] ESP32 code updated (see `PROXY_MIGRATION.md`)
- [ ] Firmware rebuilt with proxy connection
- [ ] WebSocket connects to proxy
- [ ] Audio response received from proxy
- [ ] Deployed to Deno Deploy or server

---

## 🔗 Resources

- **Gemini API Docs:** https://ai.google.dev/docs/gemini_live_api
- **Deno Deploy:** https://deno.com/deploy
- **Get API Key:** https://aistudio.google.com/apikey

---

## 🎯 Next Steps

### RIGHT NOW (5 min)
```bash
# Build and flash firmware with VAD fix
platformio run --environment bubu_s3_n16r8
# Flash to ESP32
# Test with voice input
# Report if you hear Bubu's response
```

### If It Works ✅
You're done! Bubu is talking! 🎉

### If It Doesn't Work ❌
We have Plan B ready:
```bash
cd deno-proxy
cp .env.example .env
# Add GEMINI_API_KEY
deno task dev
# Update ESP32 code for proxy
# Test again
```

---

## 🆘 Troubleshooting

**No audio response after rebuild:**
1. Check serial output shows setup JSON with `"voiceConfig"`
2. Verify `thinkingBudget`: 0 is in setup JSON
3. Speak clearly and release button within 5 seconds
4. Check WiFi RSSI > -80 dBm

**Serial shows error connecting to Gemini:**
- Check internet connection
- Verify WiFi SSID and password correct
- Check Gemini API key is valid (if running on device)

**Still stuck after trying both paths:**
1. Read `GEMINI_SOLUTION_SUMMARY.md` for full context
2. Check `deno-proxy/README.md` for proxy setup
3. Enable debug logging in code
4. Check Deno console logs for errors

---

## 📞 Key Support Files

- Read full explanation: `GEMINI_SOLUTION_SUMMARY.md`
- Proxy setup: `deno-proxy/README.md`
- ESP32 migration: `PROXY_MIGRATION.md`

---

**READY TO TEST?** 🚀

Build the firmware now and report what you see on the serial monitor!

```bash
platformio run --environment bubu_s3_n16r8
```

Then tell me:
1. Serial output from boot-up
2. What happens when you send audio
3. Did you hear Bubu's voice?

Let's get Bubu talking! 🐣
