#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct WifiScanResult {
    std::string ssid;
    int rssi = 0;
    bool known = false;
};

class WifiConnectService {
public:
    static WifiConnectService& GetInstance();

    bool StartScan();
    bool IsScanning() const;
    uint32_t GetScanVersion() const;
    std::vector<WifiScanResult> GetScanResults() const;

    bool ConnectTo(const std::string& ssid, const std::string& password);

    WifiConnectService(const WifiConnectService&) = delete;
    WifiConnectService& operator=(const WifiConnectService&) = delete;

private:
    WifiConnectService() = default;
    ~WifiConnectService() = default;

    static void ScanTask(void* arg);
    static void ConnectTask(void* arg);
    void RunScan();
    void RunConnect(const std::string& ssid, const std::string& password);
    void StoreResults(std::vector<WifiScanResult>&& results);

    mutable std::mutex mutex_;
    bool scanning_ = false;
    bool connecting_ = false;
    uint32_t scan_version_ = 0;
    std::vector<WifiScanResult> scan_results_;
};
