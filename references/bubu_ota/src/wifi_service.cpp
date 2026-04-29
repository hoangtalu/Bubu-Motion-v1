#include "wifi_service.h"
#include <WiFi.h>
#include <Preferences.h>
#include <vector>
#include "logger.h"
#include "wifi_provisioning_server.h"
DEFINE_MODULE_LOGGER(WifiLog)

namespace {
  constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
  constexpr uint32_t PROVISIONING_TIMEOUT_MS = 300000;  // 5 minutes
  constexpr bool WIFI_LOGS = true;

  WifiState state = WifiState::OFF;
  WifiFailureReason lastFailureReason = WifiFailureReason::NONE;
  uint32_t connectStartMs = 0;
  uint32_t provisioningStartMs = 0;
  String ipStr;
  bool autoScanInProgress = false;
  bool uiScanInProgress = false;
  bool scanInitPending = false;       // waiting for WiFi driver to settle before first scan
  uint32_t scanInitPendingMs = 0;
  bool provisioningActive = false;
  bool provisioningComplete = false;
  bool pendingProvisioningAp = false;  // start AP+server after STA scan completes
  uint32_t scanVersion = 0;
  uint32_t lastUiScanEndMs = 0;
  constexpr uint32_t RESCAN_INTERVAL_MS = 5000;
  constexpr uint32_t WIFI_INIT_SETTLE_MS = 150;  // ms to wait after first WiFi.mode(STA) before scan

  std::vector<String> scannedSsids;
  std::vector<int32_t> scannedRssi;
  struct KnownNet { String ssid; String pass; };
  std::vector<KnownNet> known;
  Preferences prefs;

  void rememberNetwork(const String& ssid, const String& pass);
  void storeScanResults(int n);

  void setState(WifiState newState, WifiFailureReason reason = WifiFailureReason::NONE) {
    if (state == newState) return;
    state = newState;
    lastFailureReason = reason;
    if (WIFI_LOGS) {
      switch (state) {
        case WifiState::SCANNING:       WifiLog::println("WiFi: SCANNING"); break;
        case WifiState::CONNECTING:     WifiLog::println("WiFi: CONNECTING"); break;
        case WifiState::CONNECTED:      WifiLog::printf("WiFi: CONNECTED, IP=%s\n", ipStr.c_str()); break;
        case WifiState::PROVISIONING:   WifiLog::println("WiFi: PROVISIONING (HTTP @ 192.168.4.1)"); break;
        case WifiState::FAILED: {
          const char* reasonStr = "Unknown";
          switch (reason) {
            case WifiFailureReason::NO_KNOWN_NETWORK_FOUND:  reasonStr = "No known network found"; break;
            case WifiFailureReason::PROVISIONING_TIMEOUT:    reasonStr = "Provisioning timeout"; break;
            case WifiFailureReason::CONNECTION_TIMEOUT:      reasonStr = "Connection timeout"; break;
            case WifiFailureReason::INVALID_CREDENTIALS:     reasonStr = "Invalid credentials"; break;
            default: break;
          }
          WifiLog::printf("WiFi: FAILED (%s)\n", reasonStr);
          break;
        }
        default: break;
      }
    }
  }

  // Check if provisioning has completed by monitoring WiFi connection status
  void checkProvisioningStatus() {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED && provisioningActive) {
      // WiFi connected after provisioning started
      provisioningComplete = true;
      if (WIFI_LOGS) {
        WifiLog::printf("Provisioning successful, WiFi connected: %s\n", WiFi.SSID().c_str());
      }
    }
  }

  void rememberNetwork(const String& ssid, const String& pass) {
    for (auto it = known.begin(); it != known.end(); ++it) {
      if (it->ssid == ssid) { known.erase(it); break; }
    }
    known.insert(known.begin(), {ssid, pass});
    if (known.size() > 2) known.resize(2);
    if (prefs.begin("wifi", false)) {
      prefs.putString("ssid0", known.size() > 0 ? known[0].ssid : "");
      prefs.putString("pass0", known.size() > 0 ? known[0].pass : "");
      prefs.putString("ssid1", known.size() > 1 ? known[1].ssid : "");
      prefs.putString("pass1", known.size() > 1 ? known[1].pass : "");
      prefs.end();
    }
  }

  void storeScanResults(int n) {
    scannedSsids.clear();
    scannedRssi.clear();
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      int32_t rssi = WiFi.RSSI(i);
      bool merged = false;
      for (size_t j = 0; j < scannedSsids.size(); ++j) {
        if (scannedSsids[j] == ssid) {
          if (rssi > scannedRssi[j]) scannedRssi[j] = rssi;
          merged = true;
          break;
        }
      }
      if (!merged) {
        scannedSsids.push_back(ssid);
        scannedRssi.push_back(rssi);
      }
    }
    // Sort by RSSI descending
    for (size_t i = 0; i < scannedRssi.size(); ++i) {
      size_t best = i;
      for (size_t j = i + 1; j < scannedRssi.size(); ++j) {
        if (scannedRssi[j] > scannedRssi[best]) best = j;
      }
      if (best != i) {
        std::swap(scannedRssi[i], scannedRssi[best]);
        std::swap(scannedSsids[i], scannedSsids[best]);
      }
    }
    scanVersion++;
  }
}  // namespace

void wifiInit() {
  state = WifiState::OFF;
  lastFailureReason = WifiFailureReason::NONE;
  connectStartMs = 0;
  provisioningStartMs = 0;
  ipStr = "";
  autoScanInProgress = false;
  uiScanInProgress = false;
  provisioningActive = false;
  provisioningComplete = false;
  pendingProvisioningAp = false;
  scanVersion = 0;
  known.clear();
  if (prefs.begin("wifi", true)) {
    String s0 = prefs.getString("ssid0", "");
    String p0 = prefs.getString("pass0", "");
    String s1 = prefs.getString("ssid1", "");
    String p1 = prefs.getString("pass1", "");
    if (s0.length()) known.push_back({s0, p0});
    if (s1.length()) known.push_back({s1, p1});
    prefs.end();
  }
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
}

void wifiStop() {
  // Cancel any in-progress scan BEFORE any mode changes — changing WiFi mode
  // while a scan is running causes a LoadProhibited crash (null deref in driver).
  WiFi.scanDelete();
  autoScanInProgress = false;
  uiScanInProgress = false;
  scanInitPending = false;
  pendingProvisioningAp = false;
  wifiStopProvisioning();          // safe now: no scan running when mode changes
  WiFi.disconnect(false, false);   // disconnect from AP, keep radio on
  ipStr = "";
  lastUiScanEndMs = 0;
  scannedSsids.clear();
  scannedRssi.clear();
  scanVersion++;
  setState(WifiState::OFF);
}

void wifiUpdate() {
  // Handle provisioning server requests
  if (provisioningActive && provisioningServerIsRunning()) {
    provisioningServerUpdate();
  }

  // Cold-start settle: first WiFi.mode(STA) needs ~150ms before scan is reliable
  if (scanInitPending) {
    if (millis() - scanInitPendingMs >= WIFI_INIT_SETTLE_MS) {
      scanInitPending = false;
      wifiScanStart();  // now actually start the scan
    }
    return;
  }

  if (autoScanInProgress) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    autoScanInProgress = false;
    if (WIFI_LOGS) WifiLog::printf("WiFi: Auto-connect scan complete, found %d networks\n", n);

    String chosenSsid, chosenPass;
    if (n > 0) {
      for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (WIFI_LOGS) WifiLog::printf("  - %s (RSSI: %d)\n", ssid.c_str(), WiFi.RSSI(i));
        for (const auto& k : known) {
          if (ssid == k.ssid) {
            chosenSsid = k.ssid;
            chosenPass = k.pass;
            if (WIFI_LOGS) WifiLog::printf("  -> MATCH FOUND: %s\n", chosenSsid.c_str());
            break;
          }
        }
        if (chosenSsid.length()) break;
      }
    }
    if (n > 0) storeScanResults(n);  // populate UI list so WiFi menu shows found networks
    WiFi.scanDelete();

    if (chosenSsid.length() == 0) {
      // No known network found - offer provisioning as fallback
      setState(WifiState::FAILED, WifiFailureReason::NO_KNOWN_NETWORK_FOUND);
      if (WIFI_LOGS) WifiLog::println("WiFi: No known network found, provisioning recommended");
      return;
    }

    if (WIFI_LOGS) WifiLog::printf("WiFi: Connecting to %s...\n", chosenSsid.c_str());
    WiFi.begin(chosenSsid.c_str(), chosenPass.c_str());
    connectStartMs = millis();
    setState(WifiState::CONNECTING);
    return;
  }

  if (uiScanInProgress) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    uiScanInProgress = false;
    if (WIFI_LOGS) WifiLog::printf("WiFi: UI scan complete, found %d networks\n", n);

    if (n > 0) {
      storeScanResults(n);
      if (WIFI_LOGS) WifiLog::printf("WiFi: Stored %zu results\n", scannedSsids.size());
    } else if (n == 0) {
      if (WIFI_LOGS) WifiLog::println("WiFi: Scan found no networks");
      scannedSsids.clear();
      scannedRssi.clear();
      scanVersion++;
    } else if (n == WIFI_SCAN_FAILED) {
      if (WIFI_LOGS) WifiLog::println("WiFi: Scan failed");
      scannedSsids.clear();
      scannedRssi.clear();
      scanVersion++;
    }
    WiFi.scanDelete();
    lastUiScanEndMs = millis();

    // Deferred AP start: provisioning was waiting for STA-mode scan to finish
    // before switching to AP_STA. This gives reliable scan results because the
    // radio was in STA-only mode (not locked to a single AP channel).
    if (pendingProvisioningAp) {
      pendingProvisioningAp = false;
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("Bubu-Setup");
      provisioningServerInit();
      if (WIFI_LOGS) WifiLog::println("WiFi: provisioning AP + server started");
    }
    return;
  }

  // Keep re-scanning while user is on connect screen
  if (state == WifiState::SCANNING && !uiScanInProgress && !autoScanInProgress) {
    if (millis() - lastUiScanEndMs >= RESCAN_INTERVAL_MS) {
      wifiScanStart();
    }
  }

  if (state == WifiState::PROVISIONING) {
    checkProvisioningStatus();

    if (provisioningComplete) {
      // Provisioning finished, move to connecting state
      provisioningComplete = false;
      provisioningActive = false;
      setState(WifiState::CONNECTING);
      connectStartMs = millis();
      return;
    }
    // Timeout for provisioning
    if (millis() - provisioningStartMs > PROVISIONING_TIMEOUT_MS) {
      if (WIFI_LOGS) WifiLog::println("WiFi: provisioning timed out");
      setState(WifiState::FAILED, WifiFailureReason::PROVISIONING_TIMEOUT);
      wifiStopProvisioning();
      return;
    }
  }

  if (state == WifiState::CONNECTING) {
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) {
      ipStr = WiFi.localIP().toString();
      wifiStopProvisioning();  // drop SoftAP after successful connection
      setState(WifiState::CONNECTED);
    } else if (millis() - connectStartMs > CONNECT_TIMEOUT_MS) {
      if (WIFI_LOGS) WifiLog::println("WiFi: connect timed out");
      setState(WifiState::FAILED, WifiFailureReason::CONNECTION_TIMEOUT);
    }
  }
}

WifiState wifiGetState() {
  return state;
}

WifiFailureReason wifiGetFailureReason() {
  return lastFailureReason;
}

const char* wifiGetIp() {
  return (state == WifiState::CONNECTED) ? ipStr.c_str() : "";
}

void wifiScanStart() {
  if (uiScanInProgress || autoScanInProgress || scanInitPending) return;
  if (state == WifiState::OFF || state == WifiState::FAILED) {
    setState(WifiState::SCANNING);
  }

  // On cold start (WiFi driver never initialized), the radio needs ~150ms to
  // settle after WiFi.mode(STA) before a scan will find anything. The official
  // Arduino ESP32 WiFiScan example does: mode(STA) → disconnect() → delay(100).
  // We handle this non-blockingly with a settle timer.
  bool coldStart = (WiFi.getMode() == WIFI_MODE_NULL);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.scanDelete();

  if (coldStart) {
    WiFi.disconnect(false, false);  // trigger proper STA stack init
    scanInitPending = true;
    scanInitPendingMs = millis();
    if (WIFI_LOGS) WifiLog::println("WiFi: STA init, settling before scan...");
    return;
  }

  int res = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false);

  if (res == WIFI_SCAN_FAILED) {
    if (WIFI_LOGS) WifiLog::println("WiFi: Scan failed to start");
    scannedSsids.clear();
    scannedRssi.clear();
    scanVersion++;
    return;
  }

  if (WIFI_LOGS) WifiLog::println("WiFi: UI scan started");
  uiScanInProgress = true;
}

bool wifiIsScanning() {
  return uiScanInProgress || scanInitPending;
}

size_t wifiGetScanCount() {
  return scannedSsids.size();
}

const char* wifiGetScanSsid(size_t idx) {
  if (idx >= scannedSsids.size()) return "";
  return scannedSsids[idx].c_str();
}

int32_t wifiGetScanRssi(size_t idx) {
  if (idx >= scannedRssi.size()) return -1000;
  return scannedRssi[idx];
}

uint32_t wifiGetScanVersion() {
  return scanVersion;
}

bool wifiConnectTo(const char* ssid, const char* pass, bool save) {
  if (!ssid || ssid[0] == '\0') return false;
  WiFi.scanDelete();               // cancel scan before any mode changes
  uiScanInProgress = false;
  autoScanInProgress = false;
  pendingProvisioningAp = false;
  WiFi.disconnect(false, false);   // keep radio on, just leave the current AP
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass ? pass : "");
  connectStartMs = millis();
  setState(WifiState::CONNECTING);
  if (save) rememberNetwork(String(ssid), pass ? String(pass) : "");
  return true;
}

void wifiAutoConnectKnown() {
  if (state == WifiState::CONNECTING || state == WifiState::CONNECTED) return;
  if (known.empty()) return;
  if (autoScanInProgress || uiScanInProgress) return;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.scanDelete();

  // Active scan (passive=false) is more reliable at finding nearby networks
  int n = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false);
  if (n == WIFI_SCAN_FAILED) {
    if (WIFI_LOGS) WifiLog::println("WiFi: Auto-connect scan failed");
    setState(WifiState::FAILED, WifiFailureReason::NO_KNOWN_NETWORK_FOUND);
    return;
  }

  if (WIFI_LOGS) WifiLog::println("WiFi: Auto-connect scan started");
  autoScanInProgress = true;
}

bool wifiIsProvisioning() {
  return provisioningActive;
}

void wifiStartProvisioning() {
  if (state == WifiState::PROVISIONING || provisioningActive) return;
  if (state == WifiState::CONNECTED) return;

  provisioningActive = true;
  provisioningComplete = false;
  provisioningStartMs = millis();
  setState(WifiState::PROVISIONING);

  // Scan in STA-only mode first — scanning in AP_STA mode locks the radio to
  // the AP channel and misses most networks. We defer starting the SoftAP until
  // the scan finishes (pendingProvisioningAp), then switch to AP_STA mode.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (!uiScanInProgress && !autoScanInProgress) {
    WiFi.scanDelete();
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false);
    uiScanInProgress = true;
    if (WIFI_LOGS) WifiLog::println("WiFi: provisioning STA scan started");
    pendingProvisioningAp = true;  // wifiUpdate() starts AP+server after scan
  } else {
    // A scan is already running — start the AP immediately with whatever we have
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Bubu-Setup");
    provisioningServerInit();
    if (WIFI_LOGS) WifiLog::println("WiFi: provisioning AP started (scan already running)");
  }
}

void wifiStopProvisioning() {
  if (!provisioningActive) return;
  provisioningActive = false;
  provisioningComplete = false;
  pendingProvisioningAp = false;  // cancel deferred AP start if scan not yet done

  // Stop the provisioning web server
  provisioningServerStop();

  // Disable SoftAP and return to STA mode.
  // scanDelete() must come BEFORE mode() — changing mode while a scan is
  // in progress causes a LoadProhibited crash in the ESP32 WiFi driver.
  WiFi.scanDelete();
  WiFi.softAPdisconnect(true);  // true = turn off AP
  WiFi.mode(WIFI_STA);

  if (WIFI_LOGS) WifiLog::println("WiFi: provisioning stopped");
}

bool wifiHasProvisioningCredentials() {
  return provisioningComplete;
}
