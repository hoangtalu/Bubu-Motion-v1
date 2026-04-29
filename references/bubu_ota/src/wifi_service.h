#pragma once
#include <Arduino.h>

enum class WifiState {
    OFF,
    SCANNING,
    CONNECTING,
    CONNECTED,
    FAILED,
    PROVISIONING      // New: waiting for HTTP provisioning
};

enum class WifiFailureReason {
    NONE,
    NO_KNOWN_NETWORK_FOUND,
    PROVISIONING_TIMEOUT,
    CONNECTION_TIMEOUT,
    INVALID_CREDENTIALS
};

void wifiInit();                     // initialize internal state (does NOT connect)
void wifiAutoConnectKnown();         // boot-time: scan + connect to known SSID if visible
void wifiStop();                     // disconnect Wi-Fi
void wifiUpdate();                   // non-blocking state update (call in loop)
WifiState wifiGetState();            // current Wi-Fi state
const char* wifiGetIp();             // returns IP string or ""
WifiFailureReason wifiGetFailureReason();

void wifiScanStart();                // start async scan for nearby SSIDs
bool wifiIsScanning();               // true while scan is running
size_t wifiGetScanCount();           // number of scan results
const char* wifiGetScanSsid(size_t idx);
int32_t wifiGetScanRssi(size_t idx);
uint32_t wifiGetScanVersion();       // increments when scan results refresh
bool wifiConnectTo(const char* ssid, const char* pass, bool save);

// Provisioning methods (HTTP-based via SoftAP)
// When provisioning starts, device creates open AP: "Bubu-Setup" (no password)
// User connects to AP and accesses: http://192.168.4.1
bool wifiIsProvisioning();            // true while provisioning is active
void wifiStartProvisioning();         // initiate HTTP/SoftAP provisioning
void wifiStopProvisioning();          // cancel provisioning
bool wifiHasProvisioningCredentials(); // true if provisioning received new credentials
