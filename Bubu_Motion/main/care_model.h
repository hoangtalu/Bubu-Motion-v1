#pragma once

#include <cstdint>

// Bubu's care rules as plain logic: no ESP-IDF, no LVGL, no clock of its own.
//
// CareSystem (care_system.cc) owns one Model and one Badges, feeds them time
// and events from the device, and persists them. Nothing here touches the
// device, so the rules compile and run on a host (tools/care_model_test.cc)
// with the same numbers as the simulator (tools/care_sim.py). The rules
// themselves are explained in docs/care-system-plan.md.
namespace care {

// Minutes after local midnight.
constexpr int Hm(int h, int m = 0) { return h * 60 + m; }

struct Window {
    int start;  // minutes after midnight, inclusive
    int end;    // exclusive
    bool Contains(int minute) const { return minute >= start && minute < end; }
};

struct Rules {
    // Bubu's day. Defaults until parents can set them in the portal.
    int wake = Hm(6, 30);
    int sleepy_from = Hm(21);            // sleepy eyes + yawn; Bubu keeps talking
    int auto_night = Hm(21, 30);         // asleep after this = the night, no bed credit
    Window breakfast{Hm(6), Hm(9, 30)};
    Window lunch{Hm(11), Hm(13, 30)};
    Window dinner{Hm(17), Hm(20)};
    Window bath_window{Hm(15), Hm(21, 30)};
    Window bed_window{Hm(19, 30), Hm(21, 30)};

    int long_nap_min = 120;              // waking from a nap this long = hungry
    int wake_hunger_cap = 35;            // "Bubu ngủ dậy là đói"

    // Rates per hour, by what Bubu is doing. Dozing = the child is away.
    float awake_full = -3.5f, awake_clean = -1.5f, awake_mood = -2.5f, awake_energy = -2.0f;
    float doze_full = -1.0f, doze_clean = -0.5f, doze_mood = -1.0f, doze_energy = 120.0f;
    float night_full = -1.0f, night_energy = 15.0f;
    float game_energy_per_min = -1.2f;
    float game_clean_per_min = -0.7f;    // playing gets Bubu grubby

    // Actions.
    int feed_bite = 10;                  // kFeedBiteBoost; 3 bites a meal
    int feed_refuse = 85;                // EyeAnimation::kFeedFullThreshold
    int feed_clean_per_bite = -1;        // crumbs
    int bath = 90;                       // kBathBoost
    int chat_mood = 10;                  // kept, user decision 2026-09-30
    int care_bonus_mood = 5;             // a need met while it was showing

    // A need shows (bubble, face) below these.
    int hungry = 40, dirty = 50, tired = 30, exhausted = 10, lonely = 40;
    // "Bubu đói thì không vui được": each need showing lowers how high CẢM XÚC
    // can go, and CẢM XÚC above that sinks toward it while Bubu is awake.
    int need_ceiling_step = 20;
    float ceiling_pull = -20.0f;         // per hour

    int floor_full = 10, floor_clean = 10, floor_mood = 20;
    int no_smudge = 60;                  // EyeAnimation::kDirtyLight
    int welcome_floor = 25;              // after a day or more away

    // Voice asks: local non-verbal clips.
    int ask_max_per_day = 4;
    int ask_cooldown_min = 45;
    int ask_presence_min = 15;           // someone touched/talked to Bubu this recently

    // Time the device was off.
    int offline_cap_h = 24;
    int offline_rest_h = 6;              // off this long = a full night's sleep

    int start_value = 70;                // fresh start on upgrade
    int new_bubu_full = 35;              // a new Bubu is a little hungry

    // TÌNH BẠN.
    int xp_per_anchor = 10;              // breakfast, clean at bedtime, bed on time
    int xp_happy_bonus = 5;
    int happy_bedtime = 60;
    int stage_levels[3] = {3, 6, 10};    // HIỂU BẠN, CÓ CÁ TÍNH, BẠN THÂN

    // HUY HIỆU tuần: one routine point per anchor per day, max 21.
    int week_bronze = 10, week_silver = 15, week_gold = 20, week_perfect = 21;
};

enum class Mode : uint8_t { Awake, Doze, Night };

// What Bubu shows in its thought bubble / asks for out loud.
enum class Need : uint8_t { None, Hungry, Dirty, Tired, Lonely, Sleepy };

enum class Trait : uint8_t { None, Playful, Curious, Chatty };

// What the device knows about the time right now.
struct Clock {
    bool valid = false;   // the wall clock can be trusted
    int32_t day = 0;      // local days since 1970-01-01
    int minute = 0;       // minutes after local midnight
};

// Days since 1970-01-01 for a proleptic Gregorian date (month 1-12).
int32_t DaysFromCivil(int year, int month, int day);
// 0 = Monday ... 6 = Sunday.
int Weekday(int32_t day);
// Monday-based week number.
int32_t WeekOf(int32_t day);

struct Stats {
    float full = 70.0f;    // CÁI BỤNG
    float energy = 70.0f;  // NĂNG LƯỢNG
    float clean = 70.0f;   // SẠCH SẼ
    float mood = 70.0f;    // CẢM XÚC
};

// Progress of one local day. Persisted, so a reboot mid-day keeps it.
struct Day {
    int32_t index = 0;          // 0 = not started (needs a valid clock)
    uint8_t fed = 0;            // bit0 breakfast, bit1 lunch, bit2 dinner
    uint8_t bed = 0;            // put to bed in the window, not woken again
    uint8_t together = 0;       // the child touched or talked to Bubu
    uint8_t cared = 0;          // at least one care action
    uint8_t bedtime_set = 0;
    uint8_t bedtime_mood = 0;
    uint8_t bedtime_clean = 0;
    uint8_t asks = 0;
    uint8_t chats = 0;
    uint8_t study = 0;
    uint16_t game_min = 0;
};

struct DaySummary {
    int32_t index = 0;
    bool breakfast = false;
    bool clean_bed = false;
    bool bed = false;
    bool together = false;
    bool cared = false;
    bool chef = false;
    int anchors = 0;   // 0..3
    int xp = 0;
};

// Everything Model needs to resume after a reboot (an NVS blob).
struct Snapshot {
    uint8_t version = 2;
    uint8_t in_bed = 0;         // put to bed for the night
    int16_t full10 = 700, energy10 = 700, clean10 = 700, mood10 = 700;
    uint16_t ema_game10 = 0, ema_study10 = 0, ema_chat10 = 0;
    uint8_t trait = 0;
    uint8_t first_feed_done = 0;
    Day day;
};

class Model {
public:
    explicit Model(const Rules& rules = Rules());

    const Rules& rules() const { return r_; }
    const Stats& stats() const { return s_; }
    const Day& day() const { return day_; }
    Mode mode() const { return mode_; }
    Trait trait() const { return trait_; }
    bool playing() const { return playing_; }

    // A Bubu that has never been cared for (first boot) vs one upgraded from
    // the old care system: both start from fixed values, see Rules.
    void StartNew();
    void StartFresh();
    void Restore(const Snapshot& snap);
    Snapshot Save() const;

    // Advance by dt seconds of monotonic time; clk is the local time at the end
    // of the step. Handles the night, the morning and day rollover. Finished
    // days queue up for PopSummary().
    void Tick(float dt_s, const Clock& clk);
    // Time the device was switched off, applied once after boot.
    void CatchUp(float offline_s);

    // Device inputs.
    // Someone is with Bubu (a care action, a game, a chat). Presence only.
    void Interaction();
    // The child touched Bubu or pressed a button: presence, and a sleeping
    // Bubu wakes up. In the evening that cancels the bed anchor. The screen
    // lighting up on its own (a status, a notification) is not a wake.
    void Touch(const Clock& clk);
    // true: the screen fell asleep (idle, or NGỦ): a nap, or the night.
    // false: wake, as Touch does.
    void SetAsleep(bool asleep, const Clock& clk);
    void SetPlaying(bool playing) { playing_ = playing; }

    // Care actions.
    bool BeginFeed();                   // false = full, Bubu refuses
    void FeedBite(const Clock& clk);
    void Bath(const Clock& clk);
    bool PutToBed(const Clock& clk);    // true = for the night, false = a nap
    // A conversation started: CẢM XÚC gained. Talking ends a nap, but not the
    // night: whether Bubu talks at bedtime is the parents' call (Phase 3).
    int Chat(const Clock& clk);
    int RewardGame(int base);
    int PreviewGameReward(int base) const;  // what RewardGame(base) would pay now
    int RewardStudy(int base);
    int GainMood(float v);
    // True once, the first time Bubu is ever fed (BỮA ĐẦU TIÊN).
    bool TakeFirstFeed();

    // Reading the state.
    bool Showing(Need need) const;
    int Ceiling() const;
    float GameFactor() const;           // 1, 0.5 or 0 by NĂNG LƯỢNG
    bool Exhausted() const { return s_.energy < r_.exhausted; }
    Need Bubble(const Clock& clk) const;
    // The need to ask for out loud now, or None. Consumes the day's budget.
    // Only inside the need's window: hungry at meal times, dirty in the bath
    // window, sleepy at night. Never to an empty room.
    Need PollAsk(const Clock& clk, bool can_play);
    // A game is about to start: an exhausted Bubu yawns once to offer a nap
    // (Need::Tired), from the same daily budget. The child can play on.
    Need AskBeforeGame(bool can_play);
    bool PopSummary(DaySummary* out);
    double now_s() const { return now_s_; }
    // Someone touched, fed, played with or talked to Bubu in the last within_s.
    bool Present(double within_s) const { return now_s_ - last_interaction_s_ <= within_s; }

private:
    void Drift(float dt_s, Mode mode);
    void Clamp();
    void ApplyCeiling(float dt_s);
    void UpdateModeForTime(const Clock& clk);
    void EnterNight(const Clock& clk);
    void RecordBedtime();
    void Rollover(const Clock& clk);
    void Finalize();
    void Push(const DaySummary& s);
    void UpdateTrait();
    void Wake(const Clock& clk);
    bool AskAllowed(bool can_play) const;
    Need TakeAsk(Need need);

    Rules r_;
    Stats s_;
    Day day_;
    Mode mode_ = Mode::Awake;
    Trait trait_ = Trait::None;
    bool playing_ = false;
    bool in_bed_ = false;
    bool first_feed_done_ = false;
    bool first_feed_pending_ = false;
    bool feeding_ = false;
    bool feed_was_hungry_ = false;
    bool feed_first_bite_ = false;
    double now_s_ = 0.0;
    double asleep_since_s_ = 0.0;
    double last_interaction_s_ = -1e12;
    double last_ask_s_ = -1e12;
    float game_sec_ = 0.0f;
    float ema_game_ = 0.0f, ema_study_ = 0.0f, ema_chat_ = 0.0f;

    static constexpr int kQueue = 8;
    DaySummary queue_[kQueue];
    int queue_n_ = 0;
};

// ---------------------------------------------------------------------------
// HUY HIỆU
// ---------------------------------------------------------------------------

enum class Badge : uint8_t {
    WeekBronze, WeekSilver, WeekGold, WeekPerfect,
    Breakfast7, Bed7, Clean7,
    FirstFeed, Chef,
    Together7, Together30, Together100, Together365,
    StageNew, StageKnows, StagePersonality, StageBestFriend,
    Count,
};
constexpr int kBadgeCount = static_cast<int>(Badge::Count);

struct BadgeInfo {
    const char* name;     // shown under the medal
    const char* face;     // short text on the medal
    uint32_t color;       // medal colour, 0xRRGGBB
    bool repeats;         // earned again and again (shows a count)
};
const BadgeInfo& Info(Badge badge);

// Persisted as an NVS blob.
struct BadgeState {
    uint8_t version = 1;
    uint8_t pending_n = 0;
    uint8_t pending[8] = {};
    uint16_t count[kBadgeCount] = {};
    int32_t week = 0;
    uint8_t week_points = 0;
    uint8_t week_tiers = 0;      // bit per weekly tier already awarded this week
    uint8_t streak_breakfast = 0;
    uint8_t streak_bed = 0;
    uint8_t streak_clean = 0;
    uint8_t stage_done = 0;      // bit per friendship stage awarded
    uint16_t days_together = 0;
    int32_t last_day = 0;
};

class Badges {
public:
    explicit Badges(const Rules& rules = Rules()) : r_(rules) {}
    void Restore(const BadgeState& st) { st_ = st; }
    const BadgeState& state() const { return st_; }

    void OnDay(const DaySummary& s);
    void OnFirstFeed();
    void OnStage(int stage);            // 0 MỚI QUEN .. 3 BẠN THÂN

    uint16_t Count(Badge b) const { return st_.count[static_cast<int>(b)]; }
    bool HasPending() const { return st_.pending_n > 0; }
    bool PeekPending(Badge* out) const;
    bool PopPending(Badge* out);
    // Progress toward a badge for the HUY HIỆU screen: have / need, or 0/0
    // when the badge has no running progress.
    void Progress(Badge b, int* have, int* need) const;

private:
    void Award(Badge b);
    Rules r_;
    BadgeState st_;
};

// 0 MỚI QUEN, 1 HIỂU BẠN, 2 CÓ CÁ TÍNH, 3 BẠN THÂN.
int StageForLevel(int level, const Rules& rules = Rules());
const char* StageName(int stage);
const char* TraitName(Trait trait);

}  // namespace care
