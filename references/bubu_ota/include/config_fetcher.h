#pragma once

#include <Arduino.h>

namespace ConfigFetcher {
    // Fetch configuration from remote server
    // Returns true if successful
    bool fetchAndSave();

    // True when local NVS has a plausible API key
    bool hasLocalConfig();

    // Check if we should attempt to fetch config
    // (one attempt per boot, caller-controlled)
    bool shouldFetchConfig();
}
