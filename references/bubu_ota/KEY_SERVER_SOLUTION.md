# Auto-Configuration via Key Server

## The Better Solution 🎯

Instead of manual serial configuration, devices **auto-fetch** their API key from a simple key server on first boot.

---

## Architecture

```
┌─────────────┐
│   Device    │ (Empty NVS, no API key)
└──────┬──────┘
       │ 1. Connect to WiFi
       │ 2. HTTP GET: https://your-server.com/bubu/key
       ▼
┌─────────────┐
│ Key Server  │ (Your simple server)
│ Returns:    │
│ {           │
│  "apiKey":  │
│  "voice":   │
│  "enabled": │
│ }           │
└──────┬──────┘
       │ 3. Device saves to NVS
       ▼
┌─────────────┐
│   Device    │ (Configured! Ready to use)
└─────────────┘
```

---

## Implementation Options

### Option A: Cloudflare Workers (FREE, Easy)

**Pros:**
- ✅ Free tier (100k requests/day)
- ✅ Global CDN
- ✅ HTTPS included
- ✅ Deploy in 2 minutes

**Setup:**

1. Create `worker.js`:
```javascript
export default {
  async fetch(request) {
    // Simple authentication (optional)
    const authHeader = request.headers.get('X-Bubu-Auth');
    if (authHeader !== 'your-secret-device-token') {
      return new Response('Unauthorized', { status: 401 });
    }

    // Return configuration
    const config = {
      apiKey: "YOUR_GEMINI_API_KEY_HERE",
      voice: "Aoede",
      enabled: true,
      audioEnabled: true
    };

    return new Response(JSON.stringify(config), {
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*'
      }
    });
  }
};
```

2. Deploy:
```bash
npx wrangler deploy
# Get URL: https://bubu-config.your-username.workers.dev
```

3. Device fetches from: `https://bubu-config.your-username.workers.dev/key`

---

### Option B: GitHub Gist (FREE, Super Simple)

**Pros:**
- ✅ No server needed
- ✅ Version control
- ✅ Can update anytime
- ✅ Free

**Setup:**

1. Create private GitHub Gist: `bubu-config.json`
```json
{
  "apiKey": "YOUR_GEMINI_API_KEY",
  "voice": "Aoede",
  "enabled": true
}
```

2. Get raw URL: `https://gist.githubusercontent.com/USERNAME/GIST_ID/raw/bubu-config.json`

3. Devices fetch from this URL

**Cons:**
- URL is long and contains gist ID
- Rate limited (60 requests/hour per IP without auth)

---

### Option C: Your Own Simple Server

**Pros:**
- ✅ Full control
- ✅ Can add authentication
- ✅ Can track which devices fetched config

**Python Flask Example:**
```python
from flask import Flask, jsonify, request

app = Flask(__name__)

# Simple token authentication
DEVICE_TOKEN = "your-secret-token-here"

@app.route('/bubu/key', methods=['GET'])
def get_config():
    # Check authentication
    auth = request.headers.get('X-Bubu-Auth')
    if auth != DEVICE_TOKEN:
        return jsonify({"error": "Unauthorized"}), 401

    # Return configuration
    config = {
        "apiKey": "YOUR_GEMINI_API_KEY",
        "voice": "Aoede",
        "enabled": True,
        "audioEnabled": True
    }

    return jsonify(config)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
```

Deploy to:
- Heroku (free tier)
- Railway.app (free tier)
- Your own VPS

---

## ESP32 Implementation

Add to your ESP32 code:

**New File:** `src/config_fetcher.h`
```cpp
#pragma once

#include <Arduino.h>

namespace ConfigFetcher {
    // Fetch configuration from remote server
    // Returns true if successful
    bool fetchAndSave();
}
```

**New File:** `src/config_fetcher.cpp`
```cpp
#include "config_fetcher.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "logger.h"
#include "chat_config.h"

DEFINE_MODULE_LOGGER(ConfigLog)

namespace ConfigFetcher {

// Configuration server endpoint
static const char* CONFIG_URL = "https://bubu-config.your-username.workers.dev/key";
static const char* AUTH_TOKEN = "your-secret-device-token";  // Optional

bool fetchAndSave() {
    ConfigLog::println("[Config] Fetching configuration from server...");

    HTTPClient http;
    http.begin(CONFIG_URL);

    // Optional: Add authentication header
    if (AUTH_TOKEN && AUTH_TOKEN[0] != '\0') {
        http.addHeader("X-Bubu-Auth", AUTH_TOKEN);
    }

    int httpCode = http.GET();

    if (httpCode != 200) {
        ConfigLog::printf("[Config] HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        ConfigLog::printf("[Config] JSON parse error: %s\n", error.c_str());
        return false;
    }

    // Extract configuration
    const char* apiKey = doc["apiKey"];
    const char* voice = doc["voice"] | "Aoede";
    bool enabled = doc["enabled"] | true;
    bool audioEnabled = doc["audioEnabled"] | true;

    if (!apiKey || strlen(apiKey) < 10) {
        ConfigLog::println("[Config] Invalid API key in response");
        return false;
    }

    // Save to NVS
    Preferences prefs;
    prefs.begin("chat", false);
    prefs.putString("apikey", apiKey);
    prefs.putString("voice", voice);
    prefs.putBool("enabled", enabled);
    prefs.putBool("audioEnabled", audioEnabled);
    prefs.end();

    ConfigLog::println("[Config] Configuration saved successfully!");
    ConfigLog::printf("[Config] Voice: %s, Enabled: %d\n", voice, enabled);

    // Reload config into chatConfig
    chatConfig.load();

    return true;
}

}  // namespace ConfigFetcher
```

**Update:** `src/main.cpp`
```cpp
#include "config_fetcher.h"

void setup() {
    // ... existing setup code ...

    ChatSystem::begin();
    chatConfig.load();

    // Auto-fetch config if API key is missing
    if (!chatConfig.isConfigured()) {
        MainLog::println("[BOOT] API key not configured, fetching from server...");

        // Wait for WiFi connection
        if (wifiGetState() == WifiState::CONNECTED) {
            if (ConfigFetcher::fetchAndSave()) {
                MainLog::println("[BOOT] Auto-configuration successful!");
            } else {
                MainLog::println("[BOOT] Auto-configuration failed - use serial commands");
            }
        } else {
            MainLog::println("[BOOT] WiFi not connected - will try after WiFi connects");
        }
    }

    // ... rest of setup ...
}

void loop() {
    // ... existing loop code ...

    // Try to auto-configure once WiFi connects (if not already configured)
    static bool autoConfigAttempted = false;
    if (!autoConfigAttempted && !chatConfig.isConfigured() &&
        wifiGetState() == WifiState::CONNECTED) {
        autoConfigAttempted = true;
        MainLog::println("[Loop] WiFi connected, attempting auto-configuration...");
        if (ConfigFetcher::fetchAndSave()) {
            MainLog::println("[Loop] Auto-configuration successful!");
        }
    }
}
```

---

## Security Considerations

### Option 1: No Authentication (Public Endpoint)
```cpp
static const char* CONFIG_URL = "https://your-server.com/bubu/key";
static const char* AUTH_TOKEN = "";  // No auth
```

**Pros:** Simple, works immediately
**Cons:** Anyone who finds the URL can get your API key

---

### Option 2: Shared Secret Token
```cpp
static const char* CONFIG_URL = "https://your-server.com/bubu/key";
static const char* AUTH_TOKEN = "bubu-secret-2024";  // Shared token
```

**Pros:** Basic protection
**Cons:** Token is in firmware (but harder to find than API key directly)

---

### Option 3: Device-Specific Tokens (Advanced)
```cpp
// Use MAC address as device ID
String deviceId = WiFi.macAddress();
http.addHeader("X-Device-ID", deviceId);
```

Server validates against whitelist of known device MACs.

---

### Option 4: Time-Limited Tokens (Most Secure)
Server generates short-lived tokens, devices fetch with initial setup token.

---

## Comparison

| Method | Complexity | Cost | Security | Maintenance |
|--------|-----------|------|----------|-------------|
| **Serial Commands** | Low | Free | High | Manual |
| **Cloudflare Worker** | Low | Free | Medium | Easy |
| **GitHub Gist** | Very Low | Free | Low | Easy |
| **Own Server** | Medium | Low | High | Medium |

---

## Recommended Approach

**For You (Single Developer):**
→ **Cloudflare Workers** with shared secret token

**Workflow:**
1. Deploy Cloudflare Worker with API key
2. Compile firmware with Worker URL + auth token
3. Upload firmware to GitHub (URL + token visible, but OK)
4. Devices auto-configure on first WiFi connection
5. To rotate API key: Update worker, devices re-fetch on next boot check

---

## Migration Path

### Phase 1: Keep Serial Commands
- Add auto-fetch feature
- Falls back to serial if fetch fails
- Best of both worlds

### Phase 2: Auto-Fetch Only
- Remove serial command method
- All devices auto-configure
- Simpler UX

---

## Implementation Priority

1. **Quick Win (5 min):** Deploy Cloudflare Worker
2. **Add to ESP32 (30 min):** Implement `config_fetcher.cpp`
3. **Test (10 min):** Flash new device, watch it auto-configure
4. **Deploy:** Upload firmware to GitHub

---

## Example Cloudflare Worker (Copy-Paste Ready)

```javascript
export default {
  async fetch(request) {
    const url = new URL(request.url);

    // Only respond to /key endpoint
    if (url.pathname !== '/key') {
      return new Response('Not Found', { status: 404 });
    }

    // Optional: Check authentication
    const auth = request.headers.get('X-Bubu-Auth');
    const expectedAuth = 'bubu-secret-2024';  // Change this!

    if (auth !== expectedAuth) {
      return new Response(JSON.stringify({
        error: 'Unauthorized'
      }), {
        status: 401,
        headers: { 'Content-Type': 'application/json' }
      });
    }

    // Return configuration
    const config = {
      apiKey: "YOUR_GEMINI_API_KEY_HERE",  // Your actual key
      voice: "Aoede",
      enabled: true,
      audioEnabled: true,
      version: "1.0.0"  // Optional: track config version
    };

    return new Response(JSON.stringify(config), {
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
        'Cache-Control': 'no-cache'
      }
    });
  }
};
```

Deploy:
```bash
npm create cloudflare@latest bubu-config
cd bubu-config
# Paste worker code above into src/index.js
npx wrangler deploy
```

---

## Testing

1. Deploy worker, get URL
2. Test with curl:
```bash
curl -H "X-Bubu-Auth: bubu-secret-2024" \
  https://bubu-config.your-username.workers.dev/key
```

3. Should return:
```json
{
  "apiKey": "AIzaSy...",
  "voice": "Aoede",
  "enabled": true,
  "audioEnabled": true,
  "version": "1.0.0"
}
```

4. Flash device, watch serial for auto-config

---

## Next Steps?

Want me to implement:
1. **Cloudflare Worker setup** (I'll guide you)
2. **ESP32 auto-fetch code** (I'll write it)
3. **Both** (full implementation)

Which approach do you prefer?
