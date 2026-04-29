#include "wifi_connect_service.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ssid_manager.h>
#include <wifi_manager.h>

namespace {

constexpr char kTag[] = "WifiConnectService";
constexpr uint32_t kScanTaskStackSize = 4096;
constexpr uint32_t kConnectTaskStackSize = 4096;

struct ConnectTaskContext {
    WifiConnectService* self = nullptr;
    std::string ssid;
    std::string password;
};

}  // namespace

WifiConnectService& WifiConnectService::GetInstance() {
    static WifiConnectService instance;
    return instance;
}

bool WifiConnectService::StartScan() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (scanning_) {
        return false;
    }

    scanning_ = true;
    if (xTaskCreate(&WifiConnectService::ScanTask, "wifi_ui_scan", kScanTaskStackSize, this, 2, nullptr) != pdPASS) {
        scanning_ = false;
        return false;
    }
    return true;
}

bool WifiConnectService::IsScanning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scanning_;
}

uint32_t WifiConnectService::GetScanVersion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scan_version_;
}

std::vector<WifiScanResult> WifiConnectService::GetScanResults() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scan_results_;
}

bool WifiConnectService::ConnectTo(const std::string& ssid, const std::string& password) {
    if (ssid.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (connecting_) {
        ESP_LOGW(kTag, "WiFi connect request ignored while another connection is in progress");
        return false;
    }

    connecting_ = true;
    auto* context = new ConnectTaskContext{this, ssid, password};
    if (xTaskCreate(&WifiConnectService::ConnectTask, "wifi_ui_connect", kConnectTaskStackSize, context, 2, nullptr) != pdPASS) {
        connecting_ = false;
        delete context;
        return false;
    }
    return true;
}

void WifiConnectService::ScanTask(void* arg) {
    auto* self = static_cast<WifiConnectService*>(arg);
    self->RunScan();
    vTaskDelete(nullptr);
}

void WifiConnectService::ConnectTask(void* arg) {
    std::unique_ptr<ConnectTaskContext> context(static_cast<ConnectTaskContext*>(arg));
    if (context != nullptr && context->self != nullptr) {
        context->self->RunConnect(context->ssid, context->password);
        std::lock_guard<std::mutex> lock(context->self->mutex_);
        context->self->connecting_ = false;
    }
    vTaskDelete(nullptr);
}

void WifiConnectService::RunScan() {
    std::vector<WifiScanResult> results;
    esp_netif_t* scan_netif = nullptr;

    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StopConfigAp();
    wifi_manager.StopStation();

    scan_netif = esp_netif_create_default_wifi_sta();
    if (scan_netif == nullptr) {
        ESP_LOGE(kTag, "Failed to create temporary station netif for scan");
        StoreResults(std::move(results));
        return;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        esp_netif_destroy_default_wifi(scan_netif);
        StoreResults(std::move(results));
        return;
    }

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(kTag, "esp_wifi_start failed: %s", esp_err_to_name(err));
        esp_netif_destroy_default_wifi(scan_netif);
        StoreResults(std::move(results));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(150));

    wifi_scan_config_t scan_config = {};
    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
        esp_wifi_stop();
        esp_netif_destroy_default_wifi(scan_netif);
        StoreResults(std::move(results));
        return;
    }

    uint16_t ap_num = 0;
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err == ESP_OK && ap_num > 0) {
        std::vector<wifi_ap_record_t> ap_records(ap_num);
        err = esp_wifi_scan_get_ap_records(&ap_num, ap_records.data());
        if (err == ESP_OK) {
            const auto& known_ssids = SsidManager::GetInstance().GetSsidList();
            std::sort(ap_records.begin(), ap_records.end(), [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
                return a.rssi > b.rssi;
            });

            for (const auto& ap_record : ap_records) {
                const char* ap_ssid = reinterpret_cast<const char*>(ap_record.ssid);
                if (ap_ssid == nullptr || ap_ssid[0] == '\0') {
                    continue;
                }

                auto existing = std::find_if(results.begin(), results.end(), [ap_ssid](const WifiScanResult& item) {
                    return item.ssid == ap_ssid;
                });
                if (existing != results.end()) {
                    continue;
                }

                bool known = std::any_of(known_ssids.begin(), known_ssids.end(), [ap_ssid](const SsidItem& item) {
                    return item.ssid == ap_ssid;
                });
                WifiScanResult result;
                result.ssid = ap_ssid;
                result.rssi = ap_record.rssi;
                result.known = known;
                results.push_back(std::move(result));
            }
        }
    }

    esp_wifi_stop();
    esp_netif_destroy_default_wifi(scan_netif);
    StoreResults(std::move(results));
}

void WifiConnectService::RunConnect(const std::string& ssid, const std::string& password) {
    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StopConfigAp();
    wifi_manager.StopStation();

    SsidManager::GetInstance().AddSsid(ssid, password);
    wifi_manager.StartStation();
}

void WifiConnectService::StoreResults(std::vector<WifiScanResult>&& results) {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_results_ = std::move(results);
    scanning_ = false;
    ++scan_version_;
}
