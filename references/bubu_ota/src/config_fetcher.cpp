#include "config_fetcher.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "logger.h"
#include "chat_config.h"

DEFINE_MODULE_LOGGER(ConfigLog)

namespace ConfigFetcher {

// Configuration is now provisioned via NVS at factory time
// Gist fetching disabled - API key is encrypted in device NVS
// Remote fetching no longer needed for base version
//
// static const char* CONFIG_URL = "https://gist.githubusercontent.com/...";  // DISABLED
// static const char* AUTH_TOKEN = "";  // DISABLED

bool hasLocalConfig() {
    Preferences prefs;
    prefs.begin("chat", true);
    String existingKey = prefs.getString("apikey", "");
    prefs.end();
    return !existingKey.isEmpty() && existingKey.length() >= 10;
}

bool shouldFetchConfig() {
    // Always allow one refresh per boot (main loop enforces one-shot),
    // so key rotations on the server propagate without reflashing.
    return true;
}

bool fetchAndSave() {
    // BASE VERSION: Configuration is provisioned in NVS at factory time
    // No remote fetching needed - API key is secure in encrypted NVS
    ConfigLog::println("[Config] Using NVS-provisioned config (factory provisioning)");
    ConfigLog::println("[Config] Remote config fetch is DISABLED");
    ConfigLog::println("[Config] Gist endpoint removed for security (no network exposure)");
    return true;  // Configuration already in NVS from provisioning
}

}  // namespace ConfigFetcher
