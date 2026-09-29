#include "screen_manager.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include <esp_log.h>

static const char* TAG = "ScreenManager";

namespace ScreenManager {

namespace {

// One row per screen; the whole behaviour of the device in one place.
// Everything a screen does not claim here is switched off while it is active.
//
//                             fps static  imu  chrome mischf care  voices
constexpr ScreenPolicy kBoot      { 30, false, false, true,  false, false, true  };
constexpr ScreenPolicy kHatching  { 30, false, false, false, false, false, true  };
constexpr ScreenPolicy kMain      { 30, false, true,  true,  true,  true,  true  };
constexpr ScreenPolicy kEyeSeq    { 30, false, false, true,  true,  false, true  };
constexpr ScreenPolicy kPanel     {  0, false, false, false, false, false, false };
constexpr ScreenPolicy kImuPanel  {  0, false, true,  false, false, false, false };
constexpr ScreenPolicy kSleep     {  4, true,  false, false, false, false, false };

// Indexed by ScreenId. Order must match the enum.
constexpr ScreenPolicy kScreenPolicies[] = {
    kBoot,        // Boot
    kHatching,    // Hatching
    kMain,        // Main
    kEyeSeq,      // Feeding        -- scripted sequence on the eye canvas
    kEyeSeq,      // Bathing        -- ditto
    kPanel,       // Clock
    kSleep,       // Sleep
    kPanel,       // Menu
    kPanel,       // Care
    kPanel,       // Connect
    kPanel,       // Keyboard
    kPanel,       // Settings
    kPanel,       // Notes
    kPanel,       // NoteDetail
    kPanel,       // Reminders
    kPanel,       // ReminderDetail
    kPanel,       // Volume
    kPanel,       // Stats
    kPanel,       // GamesList
    kPanel,       // Level
    kPanel,       // Fortune
    kPanel,       // GreenEyeGame
    kPanel,       // CheckerGame
    kPanel,       // QuickTapGame
    kPanel,       // SnakeGame
    kImuPanel,    // TiltMazeGame  -- panel renderer plus accelerometer input
    kImuPanel,    // TrafficRunnerGame -- panel renderer plus accelerometer input
    kPanel,       // Pomodoro
    kPanel,       // Celebration    -- level-up GIF owns the screen
};

static_assert(sizeof(kScreenPolicies) / sizeof(kScreenPolicies[0]) ==
                  static_cast<size_t>(ScreenId::Count),
              "kScreenPolicies must have one entry per ScreenId");

const char* const kScreenNames[] = {
    "boot",     "hatching",       "main",    "feeding", "bathing",
    "clock",    "sleep",          "menu",    "care",    "connect",
    "keyboard", "settings",       "notes",   "note_detail", "reminders",
    "reminder_detail", "volume",  "stats", "games_list",
    "level",    "fortune",        "green_eye_game", "checker_game",
    "quick_tap_game", "snake_game", "tilt_maze_game", "traffic_runner_game",
    "pomodoro", "celebration",
};

static_assert(sizeof(kScreenNames) / sizeof(kScreenNames[0]) ==
                  static_cast<size_t>(ScreenId::Count),
              "kScreenNames must have one entry per ScreenId");

// Read on the IMU timer task and the LVGL task; written on the LVGL task.
// Kept atomic so Policy() never has to take the listener mutex.
std::atomic<ScreenId> current_screen_{ScreenId::Boot};

std::vector<std::pair<int, ScreenCallback>> listeners_;
int next_listener_id_ = 0;
std::mutex mutex_;

void NotifyScreenChange(ScreenId old_screen, ScreenId new_screen) {
    std::vector<ScreenCallback> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_copy.reserve(listeners_.size());
        for (const auto& [id, cb] : listeners_) {
            callbacks_copy.push_back(cb);
        }
    }

    for (const auto& cb : callbacks_copy) {
        cb(old_screen, new_screen);
    }
}

}  // namespace

ScreenId Current() {
    return current_screen_.load(std::memory_order_relaxed);
}

ScreenPolicy PolicyFor(ScreenId screen) {
    const auto index = static_cast<size_t>(screen);
    if (index >= static_cast<size_t>(ScreenId::Count)) {
        return kMain;
    }
    return kScreenPolicies[index];
}

ScreenPolicy Policy() {
    return PolicyFor(Current());
}

const char* GetScreenName(ScreenId screen) {
    const auto index = static_cast<size_t>(screen);
    if (index >= static_cast<size_t>(ScreenId::Count)) {
        return "invalid";
    }
    return kScreenNames[index];
}

void Set(ScreenId screen) {
    if (screen >= ScreenId::Count) {
        ESP_LOGW(TAG, "Ignoring invalid screen id %u", static_cast<unsigned>(screen));
        return;
    }

    const ScreenId old_screen = current_screen_.load(std::memory_order_relaxed);
    if (old_screen == screen) {
        return;
    }

    current_screen_.store(screen, std::memory_order_relaxed);

    const ScreenPolicy policy = PolicyFor(screen);
    ESP_LOGI(TAG, "screen: %s -> %s [eye_fps=%u imu=%d]",
             GetScreenName(old_screen), GetScreenName(screen),
             static_cast<unsigned>(policy.eye_fps), policy.imu ? 1 : 0);

    NotifyScreenChange(old_screen, screen);
}

int AddListener(ScreenCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    int id = next_listener_id_++;
    listeners_.emplace_back(id, std::move(callback));
    return id;
}

void RemoveListener(int listener_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
            [listener_id](const auto& p) { return p.first == listener_id; }),
        listeners_.end());
}

}  // namespace ScreenManager
