# Setting Up a New Device

## Problem Solved: API Key Security

**Issue:** Previously, the Gemini API key was hardcoded in `chat_config.h`, which caused:
1. ❌ GitHub secret detection warnings when uploading OTA firmware
2. ❌ API key exposed in compiled binary
3. ❌ New devices wouldn't work (no API key in their NVS storage)

**Solution:** API key is now **only stored in NVS** (device flash storage), not in source code.

---

## Setup Process for New Devices

### Method 1: Serial Commands (Recommended)

After flashing firmware to a new device, configure it via serial monitor:

```bash
# Connect to serial monitor (115200 baud)

# 1. Set Gemini API key
chat key YOUR_API_KEY_HERE

# 2. Verify configuration
chat status

# 3. Optional: Configure voice
chat voice Aoede

# 4. Optional: Set audio mode
chat audio on
```

**Example:**
```
chat key AIzaSyA3KhHQwt8eWs_sMFArwfZw507fTwYGRK8
[Chat] API key saved

chat status
[Chat] enabled=1 audio=1 key=(set) voice=Aoede state=0
```

---

### Method 2: Programmatic Setup (Advanced)

For factory programming, you can pre-configure NVS before shipping:

```cpp
// One-time setup code (add to setup() temporarily)
#include <Preferences.h>

void setupChatConfig() {
  Preferences prefs;
  prefs.begin("chat", false);  // read-write
  prefs.putString("apikey", "YOUR_API_KEY_HERE");
  prefs.putString("voice", "Aoede");
  prefs.putBool("enabled", true);
  prefs.putBool("audioEnabled", true);
  prefs.end();
  Serial.println("Chat config initialized!");
}
```

Then remove this code after first boot.

---

## Serial Commands Reference

### Chat Configuration:

| Command | Description | Example |
|---------|-------------|---------|
| `chat key <KEY>` | Set Gemini API key | `chat key AIzaSyC...` |
| `chat voice <NAME>` | Set voice preset | `chat voice Aoede` |
| `chat audio on` | Enable audio mode | `chat audio on` |
| `chat audio off` | Text-only mode | `chat audio off` |
| `chat on` | Enable chat system | `chat on` |
| `chat off` | Disable chat system | `chat off` |
| `chat status` | Show current config | `chat status` |
| `chat text <MSG>` | Send text message | `chat text hello` |
| `chat say <MSG>` | Alias for text | `chat say hi` |

### Voice Presets:
- **Aoede** - Default, balanced
- **Charon** - Deep, authoritative
- **Fenrir** - Energetic, playful
- **Kore** - Calm, soothing
- **Puck** - Mischievous, quick

---

## OTA Update Process

### GitHub Release Setup:

1. **Build firmware:**
   ```bash
   pio run
   ```

2. **Upload to GitHub Releases:**
   - Upload `.pio/build/bubu_s3_n16r8/firmware.bin`
   - ✅ **No API key in binary!**
   - ✅ **No GitHub secret warnings!**

3. **Devices auto-update:**
   - OTA system checks for new firmware
   - Downloads and installs automatically
   - **Keeps existing NVS configuration** (API key preserved)

---

## First-Time Device Setup Checklist

For each new device:

- [ ] Flash firmware via USB
- [ ] Connect to serial monitor (115200 baud)
- [ ] Run `chat key YOUR_API_KEY_HERE`
- [ ] Run `chat status` to verify
- [ ] Connect to WiFi (menu or auto-connect)
- [ ] Test chat by saying "hello"
- [ ] Verify voice response works

---

## Troubleshooting

### "Chat not working on new device"
**Symptom:** Chat icon shows but no response
**Cause:** API key not configured in NVS
**Fix:** Run `chat key YOUR_KEY` via serial

### "GitHub warns about leaked secret"
**Symptom:** GitHub detects API key in uploaded firmware
**Cause:** Old firmware with hardcoded key
**Fix:** Rebuild firmware with latest code (key removed)

### "Device loses API key after OTA update"
**Symptom:** Chat stops working after OTA
**Cause:** OTA firmware shouldn't affect NVS, but check if it was erased
**Fix:** Re-run `chat key YOUR_KEY` via serial

### "How to verify API key is set?"
Run `chat status` - should show `key=(set)` not `key=(not set)`

---

## Security Best Practices

### ✅ DO:
- Store API key in NVS only
- Use serial commands to configure new devices
- Keep API key in `.gitignore` if using config files
- Rotate API key if accidentally exposed

### ❌ DON'T:
- Hardcode API key in source code
- Commit API key to version control
- Share firmware binaries with embedded keys
- Include API key in GitHub Issues/PRs

---

## NVS Storage Details

### Where is the API key stored?

**Location:** ESP32-S3 NVS partition (flash memory)
**Namespace:** `chat`
**Key:** `apikey`
**Persistence:** Survives firmware updates, power cycles

### To completely reset configuration:

```cpp
// Via serial or temporary code
Preferences prefs;
prefs.begin("chat", false);
prefs.clear();  // Erase all chat config
prefs.end();
```

Or use ESP32 erase tool:
```bash
esptool.py --port /dev/ttyUSB0 erase_region 0x9000 0x5000
```

---

## Multi-Device Management

### Option 1: Same API Key (Shared Account)
- Configure all devices with same API key
- All devices share same Gemini quota
- Simpler management

### Option 2: Different API Keys (Per-Device)
- Each device gets unique API key
- Separate quotas and tracking
- Better for production/sales

### Recommended for Production:
1. Flash base firmware (no API key)
2. Ship to customer
3. Customer configures via serial or app
4. Keeps API key private to end user

---

## Configuration Backup

### Export Configuration (for backup):

```cpp
// Read and display all settings
Preferences prefs;
prefs.begin("chat", true);
Serial.print("API Key: ");
Serial.println(prefs.getString("apikey", ""));
Serial.print("Voice: ");
Serial.println(prefs.getString("voice", ""));
prefs.end();
```

### Import Configuration (restore):

Use serial commands listed above.

---

## Development Workflow

### For Your Own Devices:
1. Keep your API key in a **local file** (not committed)
2. After flashing, run setup script:
   ```bash
   echo "chat key $(cat ~/.bubu_api_key)" > /dev/ttyUSB0
   ```
3. Or use platformio upload flags to run post-flash script

### For Distribution:
1. Build clean firmware (no keys)
2. Upload to GitHub Releases
3. Provide setup instructions to end users
4. Users configure their own API keys

---

## Summary

### What Changed:
- ❌ **Removed** hardcoded API key from `chat_config.h`
- ✅ **Added** empty string as default
- ✅ **NVS storage** is the only source of API key
- ✅ **Serial commands** for easy configuration

### Impact:
- ✅ No more GitHub secret warnings
- ✅ Safe to upload firmware binaries publicly
- ✅ Each device independently configured
- ✅ API key never exposed in source code

### Setup Command:
```bash
chat key YOUR_GEMINI_API_KEY_HERE
```

**That's it!** Device is ready to use Gemini. 🎉

---

## Quick Start (TL;DR)

```bash
# 1. Flash firmware
pio run -t upload

# 2. Connect serial (115200 baud)

# 3. Configure API key
chat key AIzaSyA3KhHQwt8eWs_sMFArwfZw507fTwYGRK8

# 4. Verify
chat status

# 5. Done! Chat should work now.
```
