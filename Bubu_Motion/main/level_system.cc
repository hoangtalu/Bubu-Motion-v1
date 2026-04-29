#include "level_system.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "LevelSystem";

static constexpr int      BASE_XP          = 50;
static constexpr int      STEP_XP          = 25;
static constexpr uint32_t SAVE_INTERVAL_MS = 30000;  // flush XP every 30s max

static inline uint32_t Millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

namespace LevelSystem {

static int      current_level_ = 1;
static int      current_xp_    = 0;
static bool     dirty_         = false;
static uint32_t last_save_ms_  = 0;

static void SaveState() {
    Settings s("bubu-level", true);
    s.SetInt("level", current_level_);
    s.SetInt("xp",    current_xp_);
    ESP_LOGI(TAG, "Saved: level=%d xp=%d", current_level_, current_xp_);
}

static void LoadState() {
    Settings s("bubu-level", false);
    current_level_ = s.GetInt("level", 1);
    current_xp_    = s.GetInt("xp",    0);
    if (current_level_ < 1) current_level_ = 1;
    if (current_xp_    < 0) current_xp_    = 0;
}

static void CheckLevelUp() {
    int required = GetXPForNextLevel();
    while (current_xp_ >= required) {
        current_level_++;
        current_xp_ -= required;
        required = GetXPForNextLevel();
        ESP_LOGI(TAG, "LEVEL UP! Level %d", current_level_);
    }
    // Level-up: save immediately; otherwise defer to Tick()
    if (current_xp_ == 0) {
        SaveState();
        dirty_        = false;
        last_save_ms_ = Millis();
    } else {
        dirty_ = true;
    }
}

void Begin() {
    LoadState();
    last_save_ms_ = Millis();
    ESP_LOGI(TAG, "LevelSystem ready: level=%d xp=%d/%d",
             current_level_, current_xp_, GetXPForNextLevel());
}

void AddXP(int amount) {
    if (amount <= 0) return;
    current_xp_ += amount;
    ESP_LOGI(TAG, "+%d XP → %d/%d", amount, current_xp_, GetXPForNextLevel());
    CheckLevelUp();
}

void Tick() {
    if (!dirty_) return;
    uint32_t now = Millis();
    if ((now - last_save_ms_) >= SAVE_INTERVAL_MS) {
        SaveState();
        dirty_        = false;
        last_save_ms_ = now;
    }
}

int GetLevel()          { return current_level_; }
int GetXP()             { return current_xp_; }
int GetXPForNextLevel() { return BASE_XP + (current_level_ * STEP_XP); }

bool IsUnlocked(FeatureID feature) {
    int lv = current_level_;
    switch (feature) {
        case EMO_SAD1:          return lv >= 1;
        case EMO_HAPPY1:        return lv >= 1;
        case EMO_EXCITED:       return lv >= 2;
        case IDLE_JITTER:       return lv >= 3;
        case EMO_ANGRY1:        return lv >= 4;
        case IDLE_GIGGLE:       return lv >= 5;
        case LEGACY_EMO_CYCLOP: return lv >= 5;
        case LEGACY_EMO_LOVE:   return lv >= 6;
        case LEGACY_EMO_DRUNK:  return lv >= 7;
        case IDLE_JUDGING:      return lv >= 10;
        case IDLE_SPEED_FAST:   return lv >= 12;
        case EMO_LOVE:          return false;
        default:                return false;
    }
}

} // namespace LevelSystem
