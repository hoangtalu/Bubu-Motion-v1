#include "pomodoro_timer.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <mutex>

#include <esp_log.h>
#include <esp_timer.h>

#include "settings.h"

#define TAG "Pomodoro"

namespace PomodoroTimer {
namespace {

constexpr std::array<Profile, static_cast<size_t>(Preset::kPresetCount)> kProfiles = {{
    {15,  3, 10, 4},   // 15' -- timer
    {25,  5, 15, 4},   // 25' -- the pomodoro
    {45, 10, 20, 4},   // 45' -- timer
}};

constexpr const char* kPresetLabels[static_cast<size_t>(Preset::kPresetCount)] = {
    "15'", "25'", "45'",
};

// NVS keys are capped at 15 chars; these are well inside it.
constexpr const char* kNs         = "pomodoro";
constexpr const char* kKeyTotal   = "total";
constexpr const char* kKeyToday   = "today";
constexpr const char* kKeyDay     = "day";      // yyyymmdd of `today`
constexpr const char* kKeyStreak  = "streak";
constexpr const char* kKeyBest    = "best";

// The clock is only trusted from 2025 on -- same test mcp_server.cc uses for
// self.get_time. Before that, day-of-year arithmetic is meaningless.
constexpr int kEarliestValidYear = 2025;

struct State {
    Phase  phase = Phase::kIdle;
    Preset preset = Preset::kClassic;

    // Absolute deadline, so a late Update() cannot lose time.
    uint64_t phase_end_ms = 0;
    uint32_t phase_total_ms = 0;

    uint8_t focus_done_run = 0;

    Stats stats;
    int32_t stats_day = 0;      // yyyymmdd the counters belong to
    bool    loaded = false;
};

State g;
std::mutex g_mutex;

uint64_t NowMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

const Profile& ProfileFor(Preset preset) {
    const size_t index = static_cast<size_t>(preset);
    if (index >= kProfiles.size()) {
        return kProfiles[static_cast<size_t>(Preset::kClassic)];
    }
    return kProfiles[index];
}

uint8_t MinutesFor(Phase phase, Preset preset) {
    const Profile& p = ProfileFor(preset);
    switch (phase) {
        case Phase::kFocus:      return p.focus_min;
        case Phase::kShortBreak: return p.short_break_min;
        case Phase::kLongBreak:  return p.long_break_min;
        case Phase::kIdle:       break;
    }
    return 0;
}

// yyyymmdd, or 0 when the clock is not trustworthy yet.
int32_t TodayKey() {
    const time_t now = time(nullptr);
    struct tm local = {};
    localtime_r(&now, &local);
    if (local.tm_year + 1900 < kEarliestValidYear) {
        return 0;
    }
    return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

// Days between two yyyymmdd keys. Only ever asked "is this exactly 1?", so it
// goes through mktime rather than trying to do calendar maths by hand.
int DaysBetween(int32_t from_key, int32_t to_key) {
    auto to_time = [](int32_t key) {
        struct tm t = {};
        t.tm_year = key / 10000 - 1900;
        t.tm_mon  = (key / 100) % 100 - 1;
        t.tm_mday = key % 100;
        t.tm_hour = 12;   // midday, so a DST shift cannot move the day
        t.tm_isdst = -1;
        return mktime(&t);
    };
    const time_t a = to_time(from_key);
    const time_t b = to_time(to_key);
    if (a == static_cast<time_t>(-1) || b == static_cast<time_t>(-1)) {
        return -1;
    }
    return static_cast<int>((b - a) / 86400);
}

void LoadLocked() {
    if (g.loaded) {
        return;
    }
    g.loaded = true;
    Settings settings(kNs, false);
    const int32_t total  = settings.GetInt(kKeyTotal, 0);
    const int32_t today  = settings.GetInt(kKeyToday, 0);
    const int32_t streak = settings.GetInt(kKeyStreak, 0);
    const int32_t best   = settings.GetInt(kKeyBest, 0);
    g.stats_day     = settings.GetInt(kKeyDay, 0);
    g.stats.total   = total  < 0 ? 0 : static_cast<uint32_t>(total);
    g.stats.today   = today  < 0 ? 0 : static_cast<uint16_t>(std::min<int32_t>(today, 9999));
    g.stats.streak_days = streak < 0 ? 0 : static_cast<uint16_t>(std::min<int32_t>(streak, 9999));
    g.stats.best_day    = best   < 0 ? 0 : static_cast<uint16_t>(std::min<int32_t>(best, 9999));

    // A stale `today` from a previous day must not be shown as today's count.
    const int32_t key = TodayKey();
    if (key != 0 && g.stats_day != 0 && key != g.stats_day) {
        g.stats.today = 0;
        if (DaysBetween(g.stats_day, key) != 1) {
            g.stats.streak_days = 0;   // a gap breaks the streak
        }
        g.stats_day = key;
    }
    ESP_LOGI(TAG, "Stats loaded: today=%u streak=%u best=%u total=%u day=%d",
             static_cast<unsigned>(g.stats.today),
             static_cast<unsigned>(g.stats.streak_days),
             static_cast<unsigned>(g.stats.best_day),
             static_cast<unsigned>(g.stats.total),
             static_cast<int>(g.stats_day));
}

void EnterLocked(Phase phase) {
    g.phase = phase;
    g.phase_total_ms = static_cast<uint32_t>(MinutesFor(phase, g.preset)) * 60000U;
    g.phase_end_ms = NowMs() + g.phase_total_ms;
}

void GoIdleLocked() {
    g.phase = Phase::kIdle;
    g.phase_total_ms = 0;
    g.phase_end_ms = 0;
    g.focus_done_run = 0;
}

uint32_t RemainingLocked() {
    if (g.phase == Phase::kIdle) {
        return 0;
    }
    const uint64_t now = NowMs();
    return now >= g.phase_end_ms ? 0 : static_cast<uint32_t>(g.phase_end_ms - now);
}

}  // namespace

void Begin() {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
}

const Profile& GetProfile(Preset preset) {
    return ProfileFor(preset);
}

bool IsPomodoro(Preset preset) {
    return preset == Preset::kClassic;
}

const char* PresetLabel(Preset preset) {
    const size_t index = static_cast<size_t>(preset);
    if (index >= static_cast<size_t>(Preset::kPresetCount)) {
        return kPresetLabels[static_cast<size_t>(Preset::kClassic)];
    }
    return kPresetLabels[index];
}

const char* PhaseName(Phase phase) {
    switch (phase) {
        case Phase::kFocus:      return "TẬP TRUNG";
        case Phase::kShortBreak: return "NGHỈ NGẮN";
        case Phase::kLongBreak:  return "NGHỈ DÀI";
        case Phase::kIdle:       break;
    }
    return "HỌC TẬP";
}

void Start(Preset preset) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    if (static_cast<size_t>(preset) >= static_cast<size_t>(Preset::kPresetCount)) {
        preset = Preset::kClassic;
    }
    g.preset = preset;
    g.focus_done_run = 0;
    EnterLocked(Phase::kFocus);
    ESP_LOGI(TAG, "Start: %s (%u min focus)%s", PresetLabel(preset),
             static_cast<unsigned>(ProfileFor(preset).focus_min),
             IsPomodoro(preset) ? "" : " [plain timer, not a pomodoro]");
}

void VoidBlock() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g.phase != Phase::kFocus) {
        return;
    }
    ESP_LOGI(TAG, "Void: focus block discarded after %u s, nothing banked",
             static_cast<unsigned>((g.phase_total_ms - RemainingLocked()) / 1000));
    GoIdleLocked();
}

void Stop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g.phase == Phase::kIdle) {
        return;
    }
    GoIdleLocked();
}

bool SkipBreak() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g.phase != Phase::kShortBreak && g.phase != Phase::kLongBreak) {
        return false;
    }
    EnterLocked(Phase::kFocus);
    return true;
}

Event Update() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g.phase == Phase::kIdle || RemainingLocked() > 0) {
        return Event::kNone;
    }

    if (g.phase == Phase::kFocus) {
        if (g.focus_done_run < 255) {
            g.focus_done_run++;
        }
        // Counted here so the banner shows the new number immediately; the NVS
        // write is a separate, deferred step.
        if (g.stats.today < 9999)  g.stats.today++;
        if (g.stats.total < 0xFFFFFFFFu) g.stats.total++;
        g.stats.best_day = std::max(g.stats.best_day, g.stats.today);

        const uint8_t before_long = ProfileFor(g.preset).focus_before_long;
        const bool long_break = before_long > 0 && (g.focus_done_run % before_long) == 0;
        EnterLocked(long_break ? Phase::kLongBreak : Phase::kShortBreak);
        return Event::kFocusCompleted;
    }

    EnterLocked(Phase::kFocus);
    return Event::kBreakCompleted;
}

bool IsActive() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g.phase != Phase::kIdle;
}

Phase GetPhase() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g.phase;
}

Preset GetPreset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g.preset;
}

uint32_t PhaseTotalMs() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g.phase_total_ms;
}

uint32_t RemainingMs() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return RemainingLocked();
}

uint8_t FocusDoneInRun() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g.focus_done_run;
}

uint8_t FocusBeforeLong() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ProfileFor(g.preset).focus_before_long;
}

Stats GetStats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    return g.stats;
}

void CommitCompletedFocus() {
    std::lock_guard<std::mutex> lock(g_mutex);

    const int32_t key = TodayKey();
    if (key != 0) {
        if (g.stats_day == 0) {
            g.stats_day = key;
            g.stats.streak_days = std::max<uint16_t>(g.stats.streak_days, 1);
        } else if (key != g.stats_day) {
            // Update() already bumped `today`, so this block belongs to the new
            // day: the count restarts at 1 rather than 0.
            const int gap = DaysBetween(g.stats_day, key);
            g.stats.today = 1;
            g.stats.streak_days = (gap == 1) ? g.stats.streak_days + 1 : 1;
            g.stats_day = key;
            g.stats.best_day = std::max(g.stats.best_day, g.stats.today);
        } else if (g.stats.streak_days == 0) {
            g.stats.streak_days = 1;   // first block of a fresh streak
        }
    }

    Settings settings(kNs, true);
    settings.SetInt(kKeyTotal, static_cast<int32_t>(g.stats.total));
    settings.SetInt(kKeyBest, g.stats.best_day);
    if (key != 0) {
        settings.SetInt(kKeyToday, g.stats.today);
        settings.SetInt(kKeyDay, g.stats_day);
        settings.SetInt(kKeyStreak, g.stats.streak_days);
    }
    ESP_LOGI(TAG, "Banked focus block: today=%u streak=%u total=%u%s",
             static_cast<unsigned>(g.stats.today),
             static_cast<unsigned>(g.stats.streak_days),
             static_cast<unsigned>(g.stats.total),
             key == 0 ? " (clock not set: day/streak not written)" : "");
}

}  // namespace PomodoroTimer
