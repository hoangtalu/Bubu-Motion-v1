#include "time_sync.h"

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>

#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>

#define TAG "TimeSync"

namespace TimeSync {
namespace {

// Vietnam, UTC+7, no DST. POSIX sign is inverted: "ICT-7" means UTC+7.
constexpr const char* kTimezone = "ICT-7";

// SNTP re-syncs every CONFIG_LWIP_SNTP_UPDATE_DELAY (1 h). Past twice that,
// SNTP is treated as unreachable and server_time is allowed to set the clock.
constexpr int64_t kSntpFreshUs = 2LL * 60 * 60 * 1000000;

std::atomic<bool> s_sntp_started{false};
std::atomic<int64_t> s_last_sntp_sync_us{0};

// Runs on the lwIP tcpip task: keep it short.
void OnSntpSync(struct timeval* tv) {
    s_last_sntp_sync_us.store(esp_timer_get_time());
    ESP_LOGI(TAG, "SNTP synced: %u", static_cast<unsigned>(tv->tv_sec));
}

bool SntpIsFresh() {
    const int64_t last = s_last_sntp_sync_us.load();
    return last != 0 && esp_timer_get_time() - last < kSntpFreshUs;
}

}  // namespace

void Begin() {
    setenv("TZ", kTimezone, 1);
    tzset();
}

void StartSntp() {
    if (s_sntp_started.exchange(true)) {
        return;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com"));
    config.wait_for_sync = false;
    config.sync_cb = OnSntpSync;
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed: %d", err);
        s_sntp_started.store(false);
        return;
    }
    ESP_LOGI(TAG, "SNTP started");
}

void ApplyServerTime(double epoch_ms) {
    if (SntpIsFresh()) {
        return;
    }
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(epoch_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>(static_cast<int64_t>(epoch_ms) % 1000) * 1000;
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "Clock set from server_time: %u", static_cast<unsigned>(tv.tv_sec));
}

bool IsSntpSynced() {
    return s_last_sntp_sync_us.load() != 0;
}

}  // namespace TimeSync
