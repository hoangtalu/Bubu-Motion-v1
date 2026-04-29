#include "wifi_provisioning_server.h"
#include "wifi_service.h"
#include <WebServer.h>
#include "logger.h"
DEFINE_MODULE_LOGGER(ProvLog)

namespace {
  WebServer *server = nullptr;
  bool serverRunning = false;

  // HTML for the provisioning UI
  const char* getProvisioningHTML() {
    return R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Bubu WiFi Setup</title>
  <style>
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      margin: 0;
      padding: 20px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
    }
    .container {
      background: white;
      border-radius: 12px;
      padding: 40px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      max-width: 400px;
      width: 100%;
    }
    h1 {
      text-align: center;
      color: #333;
      margin: 0 0 10px 0;
      font-size: 28px;
    }
    .subtitle {
      text-align: center;
      color: #999;
      margin-bottom: 30px;
      font-size: 14px;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      color: #333;
      font-weight: 500;
      margin-bottom: 8px;
      font-size: 14px;
    }
    input[type="text"],
    input[type="password"],
    select {
      width: 100%;
      padding: 12px;
      border: 2px solid #e0e0e0;
      border-radius: 6px;
      font-size: 14px;
      box-sizing: border-box;
      transition: border-color 0.3s;
    }
    input[type="text"]:focus,
    input[type="password"]:focus,
    select:focus {
      outline: none;
      border-color: #667eea;
    }
    .button-group {
      display: flex;
      gap: 10px;
      margin-top: 30px;
    }
    button {
      flex: 1;
      padding: 12px;
      border: none;
      border-radius: 6px;
      font-size: 14px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s;
    }
    .btn-connect {
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
    }
    .btn-connect:hover {
      transform: translateY(-2px);
      box-shadow: 0 10px 20px rgba(102, 126, 234, 0.4);
    }
    .btn-connect:active {
      transform: translateY(0);
    }
    .btn-scan {
      background: #f0f0f0;
      color: #333;
    }
    .btn-scan:hover {
      background: #e0e0e0;
    }
    .status {
      text-align: center;
      margin-top: 20px;
      padding: 12px;
      border-radius: 6px;
      display: none;
    }
    .status.success {
      background: #d4edda;
      color: #155724;
      border: 1px solid #c3e6cb;
    }
    .status.error {
      background: #f8d7da;
      color: #721c24;
      border: 1px solid #f5c6cb;
    }
    .loading {
      display: none;
      text-align: center;
      margin-top: 20px;
    }
    .spinner {
      border: 3px solid #f3f3f3;
      border-top: 3px solid #667eea;
      border-radius: 50%;
      width: 30px;
      height: 30px;
      animation: spin 1s linear infinite;
      margin: 0 auto;
    }
    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }
    .wifi-icon {
      text-align: center;
      font-size: 40px;
      margin-bottom: 20px;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="wifi-icon">📡</div>
    <h1>Bubu Setup</h1>
    <p class="subtitle">Connect to your WiFi network</p>

    <form id="wifiForm" onsubmit="handleSubmit(event)">
      <div class="form-group">
        <label for="ssidSelect">Available Networks:</label>
        <select id="ssidSelect" name="ssid" required>
          <option value="">Loading networks...</option>
        </select>
      </div>

      <div class="form-group">
        <label for="customSsid">Or enter network name manually:</label>
        <input type="text" id="customSsid" placeholder="Network name (SSID)">
      </div>

      <div class="form-group">
        <label for="password">Password (if needed):</label>
        <input type="password" id="password" name="password" placeholder="WiFi password">
      </div>

      <div class="button-group">
        <button type="button" class="btn-scan" onclick="scanNetworks()">🔄 Rescan</button>
        <button type="submit" class="btn-connect">Connect</button>
      </div>

      <div id="status" class="status"></div>
      <div id="loading" class="loading">
        <div class="spinner"></div>
        <p>Connecting...</p>
      </div>
    </form>
  </div>

  <script>
    function showStatus(message, isError = false) {
      const status = document.getElementById('status');
      status.textContent = message;
      status.className = 'status ' + (isError ? 'error' : 'success');
      status.style.display = 'block';
    }

    let _retries = 0;
    function scanNetworks(isRetry) {
      const select = document.getElementById('ssidSelect');
      if (!isRetry) {
        _retries = 0;
        select.innerHTML = '<option value="">Scanning...</option>';
      }

      fetch('/api/scan')
        .then(r => r.json())
        .then(data => {
          // Auto-retry if the device is still scanning or has no results yet
          if ((data.scanning || data.networks.length === 0) && _retries < 8) {
            _retries++;
            select.innerHTML = '<option value="">Waiting for scan\u2026 (' + _retries + ')</option>';
            setTimeout(() => scanNetworks(true), 2000);
            return;
          }
          select.innerHTML = '<option value="">Select a network...</option>';
          data.networks.forEach(net => {
            const option = document.createElement('option');
            option.value = net.ssid;
            option.textContent = net.ssid + (net.rssi ? ' (' + net.rssi + ' dBm)' : '');
            select.appendChild(option);
          });
          if (data.networks.length === 0) {
            showStatus('No networks found. Try rescanning.', true);
          } else {
            showStatus('Networks found: ' + data.networks.length);
          }
        })
        .catch(err => showStatus('Scan failed: ' + err, true));
    }

    function handleSubmit(event) {
      event.preventDefault();

      const customSsid = document.getElementById('customSsid').value.trim();
      const selectSsid = document.getElementById('ssidSelect').value;
      const ssid = customSsid || selectSsid;
      const password = document.getElementById('password').value;

      if (!ssid) {
        showStatus('Please select or enter a network name', true);
        return;
      }

      document.getElementById('loading').style.display = 'block';
      document.getElementById('status').style.display = 'none';

      fetch('/api/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid, password })
      })
      .then(r => r.json())
      .then(data => {
        document.getElementById('loading').style.display = 'none';
        if (data.success) {
          showStatus('✓ Connected! Setup complete.', false);
          setTimeout(() => { window.close(); }, 3000);
        } else {
          showStatus('Connection failed: ' + (data.error || 'Unknown error'), true);
        }
      })
      .catch(err => {
        document.getElementById('loading').style.display = 'none';
        showStatus('Error: ' + err, true);
      });
    }

    // Scan networks on page load
    window.onload = scanNetworks;
  </script>
</body>
</html>
    )rawhtml";
  }

  void handleRoot() {
    if (!server) return;
    server->send(200, "text/html", getProvisioningHTML());
  }

  void handleScan() {
    if (!server) return;

    // Use cached scan results from wifi_service — never do a blocking WiFi.scanNetworks() here
    // as it would freeze the entire main loop (LVGL display, sensors, etc.) for 2-5 seconds.
    // The provisioning page should trigger a scan via the wifi_service before showing the list.
    String json = "{\"networks\":[";
    size_t count = wifiGetScanCount();
    bool first = true;

    for (size_t i = 0; i < count && i < 20; i++) {
      const char* ssid = wifiGetScanSsid(i);
      if (!ssid || ssid[0] == '\0') continue;
      if (!first) json += ",";
      // Escape quotes in SSID just in case
      String ssidStr = String(ssid);
      ssidStr.replace("\"", "\\\"");
      json += "{\"ssid\":\"" + ssidStr + "\",\"rssi\":" + String(wifiGetScanRssi(i)) + "}";
      first = false;
    }

    json += "],\"scanning\":";
    json += wifiIsScanning() ? "true" : "false";
    json += "}";
    server->send(200, "application/json", json);
  }

  void handleConnect() {
    if (!server) return;

    if (!server->hasArg("plain")) {
      server->send(400, "application/json", "{\"success\":false,\"error\":\"No data\"}");
      return;
    }

    String body = server->arg("plain");

    // Simple JSON parsing (no ArduinoJson dependency)
    int ssidStart = body.indexOf("\"ssid\":\"") + 8;
    int ssidEnd = body.indexOf("\"", ssidStart);
    String ssid = body.substring(ssidStart, ssidEnd);

    int passStart = body.indexOf("\"password\":\"") + 12;
    int passEnd = body.indexOf("\"", passStart);
    String password = "";
    if (passStart > 12 && passEnd > passStart) {
      password = body.substring(passStart, passEnd);
    }

    ProvLog::printf("Provisioning: SSID=%s, Pass=%s\n", ssid.c_str(), password.length() ? "***" : "(none)");

    // Call the WiFi service to connect
    bool success = wifiConnectTo(ssid.c_str(), password.c_str(), true);

    if (success) {
      server->send(200, "application/json", "{\"success\":true}");
    } else {
      server->send(200, "application/json", "{\"success\":false,\"error\":\"Invalid SSID\"}");
    }
  }

  void handleNotFound() {
    if (!server) return;
    server->send(404, "text/plain", "Not Found");
  }
}

void provisioningServerInit() {
  if (serverRunning) return;

  server = new WebServer(80);

  server->on("/", HTTP_GET, handleRoot);
  server->on("/api/scan", HTTP_GET, handleScan);
  server->on("/api/connect", HTTP_POST, handleConnect);
  server->onNotFound(handleNotFound);

  server->begin();
  serverRunning = true;

  ProvLog::println("Provisioning web server started on http://192.168.4.1");
}

void provisioningServerUpdate() {
  if (serverRunning && server) {
    server->handleClient();
  }
}

void provisioningServerStop() {
  if (!serverRunning || !server) return;

  server->stop();
  delete server;
  server = nullptr;
  serverRunning = false;

  ProvLog::println("Provisioning web server stopped");
}

bool provisioningServerIsRunning() {
  return serverRunning;
}
