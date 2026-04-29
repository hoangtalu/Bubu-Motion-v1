#include "care_system.h"
#include "level_system.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "CareSystem";

// Stat range
static constexpr int STAT_MIN = 0;
static constexpr int STAT_MAX = 100;

// "Needs attention" band
static constexpr int ATTENTION_MIN = 20;
static constexpr int ATTENTION_MAX = 39;

// Decay schedule: minutes per -1 point
static constexpr uint32_t HUNGER_DECAY_MIN      = 6;
static constexpr uint32_t MOOD_DECAY_MIN        = 8;
static constexpr uint32_t ENERGY_DECAY_MIN      = 5;
static constexpr uint32_t CLEANLINESS_DECAY_MIN = 10;

static constexpr uint32_t SAVE_INTERVAL_MS  = 3UL * 60UL * 1000UL;  // save every 3 min
static constexpr uint32_t DECAY_TICK_MS     = 60UL * 1000UL;         // tick every 60s
static constexpr int      DEFAULT_STAT      = 30;

static inline uint32_t Millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

namespace CareSystem {

static int hunger      = 80;
static int mood        = 80;
static int energy      = 80;
static int cleanliness = 80;

static uint32_t last_decay_ms_  = 0;
static uint32_t hunger_acc_min_ = 0;
static uint32_t mood_acc_min_   = 0;
static uint32_t energy_acc_min_ = 0;
static uint32_t clean_acc_min_  = 0;
static uint32_t last_save_ms_   = 0;
static bool     decay_suspended_ = false;

static int Clamp(int v) {
    if (v < STAT_MIN) return STAT_MIN;
    if (v > STAT_MAX) return STAT_MAX;
    return v;
}

static void SaveSnapshot() {
    Settings s("care_stats", true);
    s.SetBool("has", true);
    s.SetInt("h", hunger);
    s.SetInt("m", mood);
    s.SetInt("e", energy);
    s.SetInt("c", cleanliness);
    ESP_LOGI(TAG, "Saved: hunger=%d mood=%d energy=%d clean=%d", hunger, mood, energy, cleanliness);
}

static void LoadSnapshot() {
    Settings s("care_stats", false);
    bool has = s.GetBool("has", false);
    if (has) {
        hunger      = Clamp(s.GetInt("h", DEFAULT_STAT));
        mood        = Clamp(s.GetInt("m", DEFAULT_STAT));
        energy      = Clamp(s.GetInt("e", DEFAULT_STAT));
        cleanliness = Clamp(s.GetInt("c", DEFAULT_STAT));
        ESP_LOGI(TAG, "Loaded: hunger=%d mood=%d energy=%d clean=%d", hunger, mood, energy, cleanliness);
    } else {
        hunger = mood = energy = cleanliness = DEFAULT_STAT;
        SaveSnapshot();
        ESP_LOGI(TAG, "First boot — defaults written");
    }
}

static void ApplyDecay(uint32_t minutes) {
    if (minutes == 0) return;

    hunger_acc_min_ += minutes;
    mood_acc_min_   += minutes;
    energy_acc_min_ += minutes;
    clean_acc_min_  += minutes;

    if (hunger_acc_min_ >= HUNGER_DECAY_MIN) {
        uint32_t steps = hunger_acc_min_ / HUNGER_DECAY_MIN;
        hunger -= (int)steps;
        hunger_acc_min_ -= steps * HUNGER_DECAY_MIN;
    }
    if (mood_acc_min_ >= MOOD_DECAY_MIN) {
        uint32_t steps = mood_acc_min_ / MOOD_DECAY_MIN;
        mood -= (int)steps;
        mood_acc_min_ -= steps * MOOD_DECAY_MIN;
    }
    if (energy_acc_min_ >= ENERGY_DECAY_MIN) {
        uint32_t steps = energy_acc_min_ / ENERGY_DECAY_MIN;
        energy -= (int)steps;
        energy_acc_min_ -= steps * ENERGY_DECAY_MIN;
    }
    if (clean_acc_min_ >= CLEANLINESS_DECAY_MIN) {
        uint32_t steps = clean_acc_min_ / CLEANLINESS_DECAY_MIN;
        cleanliness -= (int)steps;
        clean_acc_min_ -= steps * CLEANLINESS_DECAY_MIN;
    }

    hunger      = Clamp(hunger);
    mood        = Clamp(mood);
    energy      = Clamp(energy);
    cleanliness = Clamp(cleanliness);
}

void Begin() {
    LoadSnapshot();
    last_decay_ms_ = Millis();
    last_save_ms_  = last_decay_ms_;
    hunger_acc_min_ = mood_acc_min_ = energy_acc_min_ = clean_acc_min_ = 0;
    ESP_LOGI(TAG, "CareSystem ready");
}

void Update() {
    uint32_t now = Millis();

    if (last_decay_ms_ == 0) {
        last_decay_ms_ = now;
        return;
    }

    if (decay_suspended_) {
        last_decay_ms_ = now;
        last_save_ms_  = now;
        return;
    }

    uint32_t elapsed = now - last_decay_ms_;
    if (elapsed >= DECAY_TICK_MS) {
        uint32_t minutes = elapsed / DECAY_TICK_MS;
        last_decay_ms_ += minutes * DECAY_TICK_MS;
        ApplyDecay(minutes);
    }

    if (last_save_ms_ != 0 && (now - last_save_ms_) >= SAVE_INTERVAL_MS) {
        SaveSnapshot();
        last_save_ms_ = now;
    }
}

void SetDecaySuspended(bool s) {
    if (decay_suspended_ == s) return;
    decay_suspended_ = s;
    uint32_t now = Millis();
    last_decay_ms_ = now;
    last_save_ms_  = now;
}

// --- Modifiers ---

void AddHunger(int v) {
    int old = hunger;
    hunger = Clamp(hunger + v);
    if (v > 0 && old < STAT_MAX) {
        int xp = (hunger - old) / 10;
        if (xp > 0) LevelSystem::AddXP(xp);
    }
}

void AddMood(int v) {
    int old = mood;
    mood = Clamp(mood + v);
    if (v > 0 && old < STAT_MAX) {
        int xp = (mood - old) / 10;
        if (xp > 0) LevelSystem::AddXP(xp);
    }
}

void AddEnergy(int v) {
    int old = energy;
    energy = Clamp(energy + v);
    if (v > 0 && old < STAT_MAX) {
        int xp = (energy - old) / 10;
        if (xp > 0) LevelSystem::AddXP(xp);
    }
}

void AddCleanliness(int v) {
    int old = cleanliness;
    cleanliness = Clamp(cleanliness + v);
    if (v > 0 && old < STAT_MAX) {
        int xp = (cleanliness - old) / 10;
        if (xp > 0) LevelSystem::AddXP(xp);
    }
}

// --- Getters ---

int GetHunger()      { return hunger; }
int GetMood()        { return mood; }
int GetEnergy()      { return energy; }
int GetCleanliness() { return cleanliness; }

// --- Status ---

bool NeedsAttention() {
    auto in_band = [](int v) { return v >= ATTENTION_MIN && v <= ATTENTION_MAX; };
    return in_band(hunger) || in_band(mood) || in_band(energy) || in_band(cleanliness);
}

bool IsCritical() {
    return hunger == 0 || mood == 0 || energy == 0 || cleanliness == 0;
}

} // namespace CareSystem
