#include "care_system.h"

#include "level_system.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <mutex>

#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "CareSystem";

namespace CareSystem {
namespace {

constexpr const char* kCareNs = "care_stats";
constexpr const char* kBadgeNs = "badges";
constexpr uint32_t kSaveIntervalMs = 3UL * 60UL * 1000UL;

// Guards everything below. Update() and the actions run on the main task;
// the menu reads stats from the LVGL task.
std::mutex s_mutex;
care::Model s_model;
care::Badges s_badges;
int64_t s_last_tick_us = 0;
int64_t s_last_save_ms = 0;
int32_t s_saved_epoch = 0;     // wall clock at the last save of a previous boot
int32_t s_pending_xp = 0;      // the finished days' XP, not yet paid (persisted)
bool s_clock_seen = false;     // the time the device was off has been applied
bool s_badges_dirty = false;
bool s_save_now = false;
int s_stage = -1;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

// The local time as the model sees it. Before SNTP (or the OTA reply) sets the
// clock, it is not valid: no windows, no anchors, no badge progress.
care::Clock NowClock(int64_t* epoch_out = nullptr) {
    care::Clock clk;
    const time_t now = time(nullptr);
    if (epoch_out != nullptr) {
        *epoch_out = static_cast<int64_t>(now);
    }
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_year < 2025 - 1900) {
        return clk;
    }
    clk.valid = true;
    clk.day = care::DaysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    clk.minute = lt.tm_hour * 60 + lt.tm_min;
    return clk;
}

void SaveLocked() {
    const care::Snapshot snap = s_model.Save();
    int64_t epoch = 0;
    const care::Clock clk = NowClock(&epoch);
    {
        Settings settings(kCareNs, true);
        settings.SetBlob("snap", &snap, sizeof(snap));
        // 0 = unknown, so a later boot does not count clock-less time twice.
        settings.SetInt("t", clk.valid ? static_cast<int32_t>(epoch) : 0);
        settings.SetInt("xp", s_pending_xp);
    }
    if (s_badges_dirty) {
        Settings settings(kBadgeNs, true);
        const care::BadgeState& st = s_badges.state();
        settings.SetBlob("st", &st, sizeof(st));
        s_badges_dirty = false;
    }
    s_last_save_ms = NowMs();
    s_save_now = false;
}

void LoadLocked() {
    Settings settings(kCareNs, false);
    const std::vector<uint8_t> blob = settings.GetBlob("snap");
    care::Snapshot snap;
    if (blob.size() == sizeof(snap)) {
        std::memcpy(&snap, blob.data(), sizeof(snap));
    }
    if (blob.size() == sizeof(snap) && snap.version == 2) {
        s_model.Restore(snap);
        s_saved_epoch = settings.GetInt("t", 0);
        s_pending_xp = settings.GetInt("xp", 0);
        const care::Stats& s = s_model.stats();
        ESP_LOGI(TAG, "Loaded: full=%.1f energy=%.1f clean=%.1f mood=%.1f day=%ld",
                 s.full, s.energy, s.clean, s.mood, static_cast<long>(snap.day.index));
    } else if (settings.GetBool("has", false)) {
        // Stats from the old care system mean nothing under the new rules and
        // are usually near 0 by now: start the new Bubu at 70 across the board.
        // The old keys are left alone so a rollback still finds them.
        s_model.StartFresh();
        s_save_now = true;
        ESP_LOGI(TAG, "Upgraded from the old care stats: fresh start");
    } else {
        // A brand-new Bubu: content, but a little hungry, so the first bubble
        // shows up within minutes and teaches the loop.
        s_model.StartNew();
        s_save_now = true;
        ESP_LOGI(TAG, "First boot: new Bubu");
    }

    Settings badge_settings(kBadgeNs, false);
    const std::vector<uint8_t> badge_blob = badge_settings.GetBlob("st");
    care::BadgeState st;
    if (badge_blob.size() == sizeof(st)) {
        std::memcpy(&st, badge_blob.data(), sizeof(st));
        if (st.version == 1) {
            s_badges.Restore(st);
        }
    }
}

// Friendship stage from the current level; awards the stage badges.
void SyncStageLocked(int level) {
    const int stage = care::StageForLevel(level, s_model.rules());
    if (stage == s_stage) {
        return;
    }
    s_stage = stage;
    const uint16_t before = s_badges.state().stage_done;
    s_badges.OnStage(stage);
    if (s_badges.state().stage_done != before) {
        s_badges_dirty = true;
        s_save_now = true;
        ESP_LOGI(TAG, "TÌNH BẠN stage %d (%s)", stage, care::StageName(stage));
    }
}

}  // namespace

void Begin() {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();
    s_last_tick_us = esp_timer_get_time();
    s_last_save_ms = NowMs();
    // LevelSystem::Begin() runs first (application.cc), so the level is known.
    SyncStageLocked(LevelSystem::GetLevel());
    if (s_save_now) {
        SaveLocked();
    }
    ESP_LOGI(TAG, "CareSystem ready (stage %d)", s_stage);
}

void Update() {
    int xp = 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        const int64_t now_us = esp_timer_get_time();
        const float dt = static_cast<float>(now_us - s_last_tick_us) / 1e6f;
        s_last_tick_us = now_us;

        int64_t epoch = 0;
        const care::Clock clk = NowClock(&epoch);
        if (clk.valid && !s_clock_seen) {
            s_clock_seen = true;
            // The device was off from the last save until it booted.
            if (s_saved_epoch > 0) {
                const double boot_epoch = static_cast<double>(epoch) - static_cast<double>(now_us) / 1e6;
                const double offline = boot_epoch - static_cast<double>(s_saved_epoch);
                if (offline > 60.0) {
                    s_model.CatchUp(static_cast<float>(offline));
                    ESP_LOGI(TAG, "Was off for %.1f h", offline / 3600.0);
                }
            }
        }

        s_model.Tick(dt, clk);

        care::DaySummary day;
        while (s_model.PopSummary(&day)) {
            ESP_LOGI(TAG, "Day %ld: breakfast=%d clean=%d bed=%d together=%d -> %d points, +%d XP",
                     static_cast<long>(day.index), day.breakfast, day.clean_bed, day.bed,
                     day.together, day.anchors, day.xp);
            s_badges.OnDay(day);
            s_badges_dirty = true;
            s_save_now = true;
            s_pending_xp += day.xp;
        }
        // Days end at midnight, but the XP is paid when the child is next with
        // Bubu in the daytime, so a level-up and its celebration happen in
        // front of them, never on a sleeping screen.
        const care::Rules& r = s_model.rules();
        if (s_pending_xp > 0 && clk.valid && s_model.mode() == care::Mode::Awake &&
            clk.minute >= r.wake && clk.minute < r.sleepy_from && s_model.Present(60.0)) {
            xp = s_pending_xp;
            s_pending_xp = 0;
            s_save_now = true;
        }
        if (s_model.TakeFirstFeed()) {
            s_badges.OnFirstFeed();
            s_badges_dirty = true;
            s_save_now = true;
        }
    }

    // Outside the lock: a level-up hands an animation to the main task.
    if (xp > 0) {
        LevelSystem::AddXP(xp);
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    SyncStageLocked(LevelSystem::GetLevel());
    if (s_save_now || NowMs() - s_last_save_ms >= kSaveIntervalMs) {
        SaveLocked();
    }
}

void OnInteraction() {
    const care::Clock clk = NowClock();
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.Touch(clk);
}

// Only falling asleep is reported. The screen also wakes for a status or a
// notification with nobody there, so Bubu wakes on the child's touch
// (OnInteraction) or a chat, never on the screen alone.
void FellAsleep() {
    const care::Clock clk = NowClock();
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.SetAsleep(true, clk);
    s_save_now = true;
}

void SetPlaying(bool playing) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.SetPlaying(playing);
    if (playing) {
        s_model.Interaction();
    }
}

bool BeginFeed() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.Interaction();
    return s_model.BeginFeed();
}

void OnFeedBite() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.FeedBite(NowClock());
}

void OnBath() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.Interaction();
    s_model.Bath(NowClock());
}

bool PutToBed() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.Interaction();
    const bool night = s_model.PutToBed(NowClock());
    s_save_now = true;
    return night;
}

int OnChat() {
    const care::Clock clk = NowClock();
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.Chat(clk);
}

int RewardGame(int base) {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.RewardGame(base);
}

int PreviewGameReward(int base) {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.PreviewGameReward(base);
}

int RewardStudy(int base) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.Interaction();
    return s_model.RewardStudy(base);
}

void AddMood(int v) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_model.GainMood(static_cast<float>(v));
}

int GetHunger() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return static_cast<int>(std::lround(s_model.stats().full));
}

int GetMood() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return static_cast<int>(std::lround(s_model.stats().mood));
}

int GetEnergy() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return static_cast<int>(std::lround(s_model.stats().energy));
}

int GetCleanliness() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return static_cast<int>(std::lround(s_model.stats().clean));
}

int GetMoodCeiling() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.Ceiling();
}

bool Showing(care::Need need) {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.Showing(need);
}

bool NeedsAttention() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.Showing(care::Need::Hungry) || s_model.Showing(care::Need::Dirty) ||
           s_model.Showing(care::Need::Tired) || s_model.Showing(care::Need::Lonely);
}

bool IsCritical() {
    std::lock_guard<std::mutex> lock(s_mutex);
    const care::Stats& s = s_model.stats();
    const care::Rules& r = s_model.rules();
    return s.full <= r.floor_full || s.clean <= r.floor_clean || s.mood <= r.floor_mood ||
           s.energy <= 0.0f;
}

bool IsTired() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.Showing(care::Need::Tired);
}

bool IsExhausted() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.Exhausted();
}

bool IsBedtime() {
    const care::Clock clk = NowClock();
    std::lock_guard<std::mutex> lock(s_mutex);
    const care::Rules& r = s_model.rules();
    return clk.valid && (clk.minute >= r.sleepy_from || clk.minute < r.wake);
}

bool IsAsleep() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.mode() != care::Mode::Awake;
}

care::Need BubbleNeed() {
    const care::Clock clk = NowClock();
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.Bubble(clk);
}

care::Need PollVoiceAsk(bool can_play) {
    const care::Clock clk = NowClock();
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.PollAsk(clk, can_play);
}

care::Need AskBeforeGame(bool can_play) {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.AskBeforeGame(can_play);
}

int GetStage() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_stage < 0 ? 0 : s_stage;
}

care::Trait GetTrait() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_model.trait();
}

bool HasPendingBadge() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_badges.HasPending();
}

bool PeekPendingBadge(care::Badge* out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_badges.PeekPending(out);
}

bool PopPendingBadge(care::Badge* out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    const bool popped = s_badges.PopPending(out);
    if (popped) {
        s_badges_dirty = true;
        s_save_now = true;
    }
    return popped;
}

uint16_t BadgeCount(care::Badge badge) {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_badges.Count(badge);
}

void BadgeProgress(care::Badge badge, int* have, int* need) {
    const care::Clock clk = NowClock();
    const int level = LevelSystem::GetLevel();
    std::lock_guard<std::mutex> lock(s_mutex);
    int h = 0;
    int n = 0;
    s_badges.Progress(badge, &h, &n);
    const care::Rules& r = s_model.rules();
    switch (badge) {
    case care::Badge::WeekBronze:
    case care::Badge::WeekSilver:
    case care::Badge::WeekGold:
    case care::Badge::WeekPerfect:
        // Points are banked when a day ends; a new week starts at 0 even
        // before its first day has been banked.
        if (clk.valid && care::WeekOf(clk.day) != s_badges.state().week) {
            h = 0;
        }
        break;
    case care::Badge::Chef: {
        const uint8_t fed = s_model.day().fed;
        h = (fed & 1) + ((fed >> 1) & 1) + ((fed >> 2) & 1);
        n = 3;
        break;
    }
    case care::Badge::FirstFeed:
        n = 1;
        break;
    case care::Badge::StageNew:
    case care::Badge::StageKnows:
    case care::Badge::StagePersonality:
    case care::Badge::StageBestFriend: {
        const int i = static_cast<int>(badge) - static_cast<int>(care::Badge::StageNew);
        n = i == 0 ? 1 : r.stage_levels[i - 1];
        h = std::min(level, n);
        break;
    }
    default:
        break;
    }
    if (have != nullptr) *have = h;
    if (need != nullptr) *need = n;
}

}  // namespace CareSystem
