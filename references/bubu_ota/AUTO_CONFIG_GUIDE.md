# Auto-Configuration System - Complete Guide

## Overview 🎯

Devices now **automatically fetch** their Gemini API key from a secure server on first boot!

### Before (Manual):
```
Flash Device → Serial Cable → Type Commands → Configure → Done
    ❌ Tedious     ❌ Manual     ❌ Error-prone
```

### After (Automatic):
```
Flash Device → Connect WiFi → Auto-Configure → Done ✅
    ✅ Easy        ✅ Fast       ✅ Reliable
```

---

## How It Works

```
┌──────────────┐
│ New Device   │ (Empty NVS, no API key)
└──────┬───────┘
       │
       │ 1. Boots up
       │ 2. Connects to WiFi
       │ 3. Checks: "Do I have an API key?"
       │    → No!
       │
       ▼
┌──────────────┐
│ HTTP Request │
│ GET /key     │
│ Header:      │
│  X-Bubu-Auth │
└──────┬───────┘
       │
       │ HTTPS (secure)
       │
       ▼
┌──────────────────────┐
│ Cloudflare Worker    │ (Your server)
│                      │
│ 1. Check auth token  │
│ 2. Return config:    │
│    - API key         │
│    - Voice preset    │
│    - Settings        │
└──────┬───────────────┘
       │
       │ JSON response
       │
       ▼
┌──────────────┐
│ Device       │
│ 1. Parse     │
│ 2. Save NVS  │
│ 3. Ready! ✅ │
└──────────────┘
```

---

## Setup Steps

### 1. Deploy Cloudflare Worker (5 min)

See: `cloudflare-worker/README.md`

**Quick version:**
```bash
cd cloudflare-worker

# Edit worker.js:
# - Change EXPECTED_AUTH to your secret
# - Add your Gemini API key

npm install -g wrangler
wrangler login
wrangler deploy
```

You'll get a URL like:
```
https://bubu-config.YOUR_USERNAME.workers.dev
```

### 2. Update ESP32 Code (2 min)

Edit `src/config_fetcher.cpp`:

```cpp
// Line 13: Your worker URL
static const char* CONFIG_URL = "https://bubu-config.YOUR_USERNAME.workers.dev/key";

// Line 17: Your auth token (must match worker)
static const char* AUTH_TOKEN = "YOUR_SECRET_HERE";
```

### 3. Build & Flash (2 min)

```bash
pio run -t upload
```

### 4. Test (1 min)

Watch serial monitor:
```
[Loop] WiFi connected, attempting auto-configuration...
[Config] Fetching configuration from server...
[Config] Configuration received:
[Config]   API Key: AIzaSyC...qIs (length: 39)
[Loop] ✅ Auto-configuration successful!
```

### 5. Done! 🎉

Device is configured and ready to use Gemini.

---

## Code Changes Made

### New Files:

1. **`include/config_fetcher.h`** - Auto-config interface
2. **`src/config_fetcher.cpp`** - HTTP fetch implementation
3. **`cloudflare-worker/worker.js`** - Key server
4. **`cloudflare-worker/wrangler.toml`** - Worker config

### Modified Files:

1. **`src/main.cpp`**:
   - Added `#include "config_fetcher.h"`
   - Added auto-fetch on WiFi connect
   - Added status logging

2. **`include/chat_config.h`**:
   - Removed hardcoded API key (already done)
   - Empty default: `String apiKey = ""`

---

## Behavior

### New Device (No API Key):

```
[BOOT] API key not configured, will auto-fetch after WiFi connects
...
[Loop] WiFi connected, attempting auto-configuration...
[Config] Fetching configuration from server...
[Config] Received 145 bytes
[Config] Configuration saved to NVS successfully!
[Loop] ✅ Auto-configuration successful!
```

### Already Configured Device:

```
[BOOT] API key already configured
```

Skips auto-fetch, uses existing NVS config.

### Failed Auto-Config:

```
[Loop] WiFi connected, attempting auto-configuration...
[Config] HTTP error: 401
[Loop] ⚠️  Auto-configuration failed - use serial commands
```

Falls back to manual serial configuration.

---

## Fallback: Serial Commands

If auto-config fails, you can still configure manually:

```bash
chat key YOUR_API_KEY
chat status
```

See: `SETUP_NEW_DEVICE.md`

---

## Security Analysis

### What's Protected:

✅ **Gemini API key** - NOT in firmware binary
✅ **Worker endpoint** - HTTPS encrypted
✅ **Authentication** - Shared secret token
✅ **GitHub safe** - No secrets in firmware

### What's Visible:

⚠️ **Worker URL** - In firmware (OK - it's public endpoint)
⚠️ **Auth token** - In firmware (OK - harder to extract than API key)

### Threat Model:

**Scenario 1: Someone finds your worker URL**
- ❌ Can't get API key (needs auth token)
- ✅ Auth token is in firmware but harder to find

**Scenario 2: Someone extracts firmware binary**
- ❌ Can get worker URL + auth token
- ❌ Can then fetch API key
- ✅ BUT: Much harder than if key was directly in firmware
- ✅ AND: You can rotate keys easily

**Scenario 3: GitHub leaked secrets**
- ✅ API key NOT in source code
- ✅ API key NOT in firmware binary
- ✅ No GitHub warnings!

**Trade-off:**
- More secure than hardcoded key in firmware
- Less secure than manual per-device configuration
- **Best balance** for ease of use + security

---

## Production Recommendations

### For Personal Use (You):
→ Use Cloudflare Worker with shared auth token
→ Perfect balance of ease + security

### For Sale/Distribution:
→ Option A: Ship without config, buyer sets their own API key
→ Option B: Use this system with per-device tokens (advanced)

### For High Security:
→ Manual configuration via serial
→ No auto-fetch
→ Each device gets unique API key

---

## Rotating API Keys

### When to Rotate:
- Suspected token leak
- Regular security practice (every 6 months)
- Changing Gemini accounts

### How to Rotate:

1. **Update Worker:**
   ```javascript
   // worker.js
   apiKey: "NEW_API_KEY_HERE"
   ```

2. **Deploy:**
   ```bash
   wrangler deploy
   ```

3. **Devices auto-update:**
   - Clear NVS on devices: `chat key ""`
   - Reboot → auto-fetch new key
   - Or wait for periodic re-fetch (if implemented)

---

## Monitoring

### View Worker Logs:

```bash
wrangler tail
```

Shows real-time requests:
```
[Config] Request from: Bubu-ESP32/1.0
```

### Check Usage:

Cloudflare Dashboard:
- Request count per day
- Success/error rates
- Bandwidth used

### Alert on Errors:

Worker returns errors if:
- ❌ Wrong auth token (401)
- ❌ API key not set (500)
- ❌ Unknown endpoint (404)

---

## Troubleshooting

### Auto-config not working?

**Check WiFi:**
```
[Loop] WiFi connected, attempting auto-configuration...
```

If you don't see this:
- WiFi not connected
- Connect to WiFi via menu

**Check URL:**
```
[Config] Requesting: https://bubu-config.YOUR_USERNAME.workers.dev/key
```

Verify URL is correct.

**Check Auth:**
```
[Config] HTTP error: 401
```

Auth token mismatch:
- ESP32: `AUTH_TOKEN` in `config_fetcher.cpp`
- Worker: `EXPECTED_AUTH` in `worker.js`

**Check Response:**
```
[Config] JSON parse error: ...
```

Worker not returning valid JSON:
- Test with curl
- Check worker logs

### Worker not deployed?

```bash
wrangler whoami  # Check login
wrangler deploy  # Re-deploy
```

### Still not working?

**Manual override:**
```bash
chat key YOUR_API_KEY
chat status
```

---

## Migration from Old System

### Old Devices (Hardcoded Key):
1. Flash new firmware
2. Device keeps NVS config (key preserved)
3. ✅ No action needed!

### New Devices:
1. Flash firmware
2. Connect WiFi
3. ✅ Auto-configures!

### Testing:
1. Flash firmware
2. Clear NVS: Serial `chat key ""`
3. Reboot
4. Watch auto-config

---

## Cost & Limits

### Cloudflare Workers Free Tier:
- ✅ **100,000 requests/day**
- ✅ **Enough for 5,000+ devices** (20 fetches each)
- ✅ **Global CDN** (fast worldwide)
- ✅ **No credit card required**

### Scaling:
- 1 device = 1 request on first boot
- Re-fetch on NVS clear or update
- Typical: <10 requests per device lifetime

### Cost Example:
- 100 devices × 5 requests each = 500 requests
- Way under free tier limit
- **Cost: $0/month**

---

## Advanced Features

### Add Device Tracking:

```javascript
// worker.js
const deviceId = request.headers.get('X-Device-ID');
console.log(`Device ${deviceId} configured`);
```

### Per-Device Keys:

```javascript
// worker.js
const configs = {
  'device1': { apiKey: 'key1', voice: 'Aoede' },
  'device2': { apiKey: 'key2', voice: 'Charon' }
};
```

### Rate Limiting:

```javascript
// worker.js
// Limit to 10 requests per hour per IP
```

### Periodic Re-fetch:

```cpp
// config_fetcher.cpp
bool shouldFetchConfig() {
  // Re-fetch every 30 days
  static uint32_t lastFetch = 0;
  return (millis() - lastFetch > 30 * 24 * 60 * 60 * 1000);
}
```

---

## Comparison with Alternatives

| Method | Ease | Security | Cost | Maintenance |
|--------|------|----------|------|-------------|
| **Hardcoded (old)** | ⭐⭐⭐⭐⭐ | ❌ Bad | Free | None |
| **Serial Manual** | ⭐⭐ | ⭐⭐⭐⭐⭐ | Free | High |
| **Auto-Fetch (this)** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | Free | Low |
| **GitHub Gist** | ⭐⭐⭐⭐ | ⭐⭐ | Free | Low |
| **Own Server** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | $5/mo | Medium |

**Winner:** Auto-Fetch with Cloudflare Worker ✅

---

## Summary

### What You Get:

✅ **Auto-configuration** - Devices configure themselves
✅ **No GitHub warnings** - API key not in source
✅ **Easy key rotation** - Update worker, done
✅ **Fallback support** - Serial commands still work
✅ **Free hosting** - Cloudflare free tier
✅ **Global CDN** - Fast worldwide

### What Changed:

- ✅ Added auto-fetch on WiFi connect
- ✅ Created Cloudflare Worker
- ✅ Removed hardcoded API key
- ✅ Added fallback to serial

### What's the Same:

- ✅ Serial commands still work
- ✅ NVS storage unchanged
- ✅ OTA updates work
- ✅ All features intact

---

## Next Steps

1. ✅ **Deploy worker** - Follow `cloudflare-worker/README.md`
2. ✅ **Update ESP32** - Set worker URL in `config_fetcher.cpp`
3. ✅ **Build firmware** - `pio run`
4. ✅ **Test** - Flash and watch auto-config
5. 🎉 **Upload to GitHub** - No secrets, no warnings!

---

**Your devices will auto-configure on first boot!** 🚀

No more manual serial commands!
No more API keys in firmware!
No more GitHub warnings!
