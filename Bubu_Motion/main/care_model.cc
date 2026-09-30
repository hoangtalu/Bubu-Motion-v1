#include "care_model.h"

#include <algorithm>
#include <cmath>

namespace care {

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------

// Howard Hinnant's days_from_civil.
int32_t DaysFromCivil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

int Weekday(int32_t day) {
    // 1970-01-01 was a Thursday, 3 when Monday is 0.
    const int w = static_cast<int>((static_cast<int64_t>(day) + 3) % 7);
    return w < 0 ? w + 7 : w;
}

int32_t WeekOf(int32_t day) {
    const int64_t shifted = static_cast<int64_t>(day) + 3;
    return static_cast<int32_t>(shifted >= 0 ? shifted / 7 : (shifted - 6) / 7);
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

namespace {

int16_t To10(float v) { return static_cast<int16_t>(std::lround(v * 10.0f)); }
uint16_t ToU10(float v) { return static_cast<uint16_t>(std::lround(std::max(0.0f, v) * 10.0f)); }
bool IsEvening(int minute) { return minute >= Hm(12); }
uint8_t ToByte(float v) { return static_cast<uint8_t>(std::lround(std::min(255.0f, std::max(0.0f, v)))); }

}  // namespace

Model::Model(const Rules& rules) : r_(rules) {}

void Model::StartNew() {
    s_.full = static_cast<float>(r_.new_bubu_full);
    s_.energy = 100.0f;
    s_.clean = 85.0f;
    s_.mood = static_cast<float>(r_.start_value);
}

void Model::StartFresh() {
    const float v = static_cast<float>(r_.start_value);
    s_.full = s_.energy = s_.clean = s_.mood = v;
}

void Model::Restore(const Snapshot& snap) {
    s_.full = snap.full10 / 10.0f;
    s_.energy = snap.energy10 / 10.0f;
    s_.clean = snap.clean10 / 10.0f;
    s_.mood = snap.mood10 / 10.0f;
    in_bed_ = snap.in_bed != 0;
    ema_game_ = snap.ema_game10 / 10.0f;
    ema_study_ = snap.ema_study10 / 10.0f;
    ema_chat_ = snap.ema_chat10 / 10.0f;
    trait_ = snap.trait <= static_cast<uint8_t>(Trait::Chatty) ? static_cast<Trait>(snap.trait) : Trait::None;
    first_feed_done_ = snap.first_feed_done != 0;
    day_ = snap.day;
    mode_ = Mode::Awake;
    Clamp();
}

Snapshot Model::Save() const {
    Snapshot snap;
    snap.in_bed = in_bed_ ? 1 : 0;
    snap.full10 = To10(s_.full);
    snap.energy10 = To10(s_.energy);
    snap.clean10 = To10(s_.clean);
    snap.mood10 = To10(s_.mood);
    snap.ema_game10 = ToU10(ema_game_);
    snap.ema_study10 = ToU10(ema_study_);
    snap.ema_chat10 = ToU10(ema_chat_);
    snap.trait = static_cast<uint8_t>(trait_);
    snap.first_feed_done = first_feed_done_ ? 1 : 0;
    snap.day = day_;
    return snap;
}

void Model::Clamp() {
    s_.full = std::min(100.0f, std::max(static_cast<float>(r_.floor_full), s_.full));
    s_.clean = std::min(100.0f, std::max(static_cast<float>(r_.floor_clean), s_.clean));
    s_.mood = std::min(100.0f, std::max(static_cast<float>(r_.floor_mood), s_.mood));
    s_.energy = std::min(100.0f, std::max(0.0f, s_.energy));
}

void Model::Drift(float dt_s, Mode mode) {
    const float h = dt_s / 3600.0f;
    switch (mode) {
    case Mode::Awake:
        s_.full += r_.awake_full * h;
        s_.clean += r_.awake_clean * h;
        s_.mood += r_.awake_mood * h;
        s_.energy += r_.awake_energy * h;
        break;
    case Mode::Doze:
        s_.full += r_.doze_full * h;
        s_.clean += r_.doze_clean * h;
        s_.mood += r_.doze_mood * h;
        s_.energy += r_.doze_energy * h;
        break;
    case Mode::Night:
        s_.full += r_.night_full * h;
        s_.energy += r_.night_energy * h;
        break;
    }
}

int Model::Ceiling() const {
    int needs = 0;
    needs += Showing(Need::Hungry) ? 1 : 0;
    needs += Showing(Need::Dirty) ? 1 : 0;
    needs += Showing(Need::Tired) ? 1 : 0;
    return 100 - r_.need_ceiling_step * needs;
}

void Model::ApplyCeiling(float dt_s) {
    const float ceiling = static_cast<float>(Ceiling());
    if (s_.mood > ceiling) {
        s_.mood = std::max(ceiling, s_.mood + r_.ceiling_pull * dt_s / 3600.0f);
    }
}

bool Model::Showing(Need need) const {
    switch (need) {
    case Need::Hungry: return s_.full < r_.hungry;
    case Need::Dirty:  return s_.clean < r_.dirty;
    case Need::Tired:  return s_.energy < r_.tired;
    case Need::Lonely: return s_.mood < r_.lonely;
    default:           return false;
    }
}

float Model::GameFactor() const {
    if (s_.energy >= r_.tired) return 1.0f;
    if (s_.energy >= r_.exhausted) return 0.5f;
    return 0.0f;
}

void Model::RecordBedtime() {
    if (day_.bedtime_set) {
        return;
    }
    day_.bedtime_set = 1;
    day_.bedtime_mood = ToByte(s_.mood);
    day_.bedtime_clean = ToByte(s_.clean);
}

void Model::EnterNight(const Clock& clk) {
    mode_ = Mode::Night;
    if (clk.valid && IsEvening(clk.minute)) {
        RecordBedtime();
    }
}

void Model::UpdateModeForTime(const Clock& clk) {
    if (!clk.valid) {
        return;
    }
    const bool morning = clk.minute >= r_.wake && !IsEvening(clk.minute);
    if (morning) {
        // Bubu wakes with the child's morning: hungry, and no longer in bed.
        // The screen stays asleep until someone touches it, so this is a doze.
        if (mode_ == Mode::Night) {
            s_.full = std::min(s_.full, static_cast<float>(r_.wake_hunger_cap));
            mode_ = Mode::Doze;
            asleep_since_s_ = now_s_;
        }
        in_bed_ = false;
    }
    const bool night_time = clk.minute >= r_.auto_night || clk.minute < r_.wake;
    if (mode_ == Mode::Doze && night_time) {
        EnterNight(clk);
    }
}

void Model::Push(const DaySummary& s) {
    if (queue_n_ == kQueue) {
        for (int i = 1; i < kQueue; ++i) {
            queue_[i - 1] = queue_[i];
        }
        --queue_n_;
    }
    queue_[queue_n_++] = s;
}

bool Model::PopSummary(DaySummary* out) {
    if (queue_n_ == 0) {
        return false;
    }
    if (out != nullptr) {
        *out = queue_[0];
    }
    for (int i = 1; i < queue_n_; ++i) {
        queue_[i - 1] = queue_[i];
    }
    --queue_n_;
    return true;
}

void Model::UpdateTrait() {
    const float best = std::max(ema_game_, std::max(ema_study_, ema_chat_));
    if (best < 10.0f) {
        trait_ = Trait::None;
    } else if (best == ema_game_) {
        trait_ = Trait::Playful;
    } else if (best == ema_study_) {
        trait_ = Trait::Curious;
    } else {
        trait_ = Trait::Chatty;
    }
}

void Model::Finalize() {
    DaySummary s;
    s.index = day_.index;
    s.breakfast = (day_.fed & 1) != 0;
    s.bed = day_.bed != 0;
    s.together = day_.together != 0;
    s.cared = day_.cared != 0;
    s.chef = (day_.fed & 7) == 7;
    // Bubu never went to sleep that evening: judge it as the day ended.
    const float bed_clean = day_.bedtime_set ? day_.bedtime_clean : s_.clean;
    const float bed_mood = day_.bedtime_set ? day_.bedtime_mood : s_.mood;
    // Only a day spent together counts: a Bubu nobody played with stays
    // clean on its own, and that is not care.
    s.clean_bed = s.together && bed_clean >= r_.no_smudge;
    s.anchors = (s.breakfast ? 1 : 0) + (s.clean_bed ? 1 : 0) + (s.bed ? 1 : 0);
    const bool happy = s.together && bed_mood >= r_.happy_bedtime;
    s.xp = r_.xp_per_anchor * s.anchors + (happy ? r_.xp_happy_bonus : 0);

    // Personality: a running average of how the days are spent.
    ema_game_ = 0.85f * ema_game_ + 0.15f * static_cast<float>(day_.game_min);
    ema_study_ = 0.85f * ema_study_ + 0.15f * static_cast<float>(day_.study) * 20.0f;
    ema_chat_ = 0.85f * ema_chat_ + 0.15f * static_cast<float>(day_.chats) * 10.0f;
    UpdateTrait();
    Push(s);
}

void Model::Rollover(const Clock& clk) {
    if (!clk.valid) {
        return;
    }
    if (day_.index == 0) {
        // First valid clock: start counting today from here.
        const uint8_t together = day_.together;
        day_ = Day();
        day_.index = clk.day;
        day_.together = together;
        return;
    }
    if (clk.day <= day_.index) {
        return;  // same day, or the clock stepped back
    }
    Finalize();
    // Days nobody saw at all (device off, or asleep): empty, so streaks break.
    const int32_t gap = clk.day - day_.index;
    for (int32_t i = 1; i < gap && i <= 7; ++i) {
        DaySummary empty;
        empty.index = day_.index + i;
        Push(empty);
        ema_game_ *= 0.85f;
        ema_study_ *= 0.85f;
        ema_chat_ *= 0.85f;
    }
    if (gap > 1) {
        UpdateTrait();
    }
    day_ = Day();
    day_.index = clk.day;
}

void Model::Tick(float dt_s, const Clock& clk) {
    if (!(dt_s > 0.0f)) {
        dt_s = 0.0f;
    }
    // A stalled main loop must not look like hours of neglect.
    dt_s = std::min(dt_s, 3600.0f);
    now_s_ += dt_s;

    Rollover(clk);
    UpdateModeForTime(clk);
    Drift(dt_s, mode_);
    if (playing_ && mode_ == Mode::Awake) {
        s_.energy += r_.game_energy_per_min * dt_s / 60.0f;
        s_.clean += r_.game_clean_per_min * dt_s / 60.0f;
        game_sec_ += dt_s;
        while (game_sec_ >= 60.0f) {
            game_sec_ -= 60.0f;
            if (day_.game_min < 0xFFFF) {
                ++day_.game_min;
            }
        }
    }
    if (mode_ == Mode::Awake) {
        ApplyCeiling(dt_s);
    }
    Clamp();
}

void Model::CatchUp(float offline_s) {
    if (!(offline_s > 0.0f)) {
        return;
    }
    const float t = std::min(offline_s, static_cast<float>(r_.offline_cap_h) * 3600.0f);
    const float h = t / 3600.0f;
    // Switched off = away and asleep.
    s_.full += r_.doze_full * h;
    s_.clean += r_.doze_clean * h;
    s_.mood += r_.doze_mood * h;
    if (t >= static_cast<float>(r_.offline_rest_h) * 3600.0f) {
        s_.energy = 100.0f;
    } else {
        s_.energy += r_.doze_energy * h;
    }
    if (t >= static_cast<float>(r_.long_nap_min) * 60.0f) {
        s_.full = std::min(s_.full, static_cast<float>(r_.wake_hunger_cap));
        in_bed_ = false;
    }
    if (offline_s >= 24.0f * 3600.0f) {
        // Back after a day or more: a warm welcome, never a wreck.
        const float floor = static_cast<float>(r_.welcome_floor);
        s_.full = std::max(s_.full, floor);
        s_.clean = std::max(s_.clean, floor);
        s_.mood = std::max(s_.mood, floor);
    }
    Clamp();
}

void Model::Interaction() {
    last_interaction_s_ = now_s_;
    day_.together = 1;
}

void Model::Touch(const Clock& clk) {
    Interaction();
    Wake(clk);
}

void Model::SetAsleep(bool asleep, const Clock& clk) {
    if (!asleep) {
        Wake(clk);
        return;
    }
    if (mode_ != Mode::Awake) {
        return;
    }
    asleep_since_s_ = now_s_;
    const bool night_time = clk.valid && (clk.minute >= r_.auto_night || clk.minute < r_.wake);
    if (in_bed_ || night_time) {
        EnterNight(clk);
    } else {
        mode_ = Mode::Doze;
    }
}

void Model::Wake(const Clock& clk) {
    if (mode_ == Mode::Awake) {
        return;
    }
    if (now_s_ - asleep_since_s_ >= r_.long_nap_min * 60.0) {
        s_.full = std::min(s_.full, static_cast<float>(r_.wake_hunger_cap));
    }
    if (mode_ == Mode::Night && clk.valid && IsEvening(clk.minute)) {
        // Woken again the same evening: that was not bedtime after all.
        day_.bed = 0;
        day_.bedtime_set = 0;
    }
    in_bed_ = false;
    mode_ = Mode::Awake;
}

bool Model::BeginFeed() {
    if (s_.full >= r_.feed_refuse) {
        feeding_ = false;
        return false;
    }
    feeding_ = true;
    feed_was_hungry_ = s_.full < r_.hungry;
    feed_first_bite_ = true;
    return true;
}

void Model::FeedBite(const Clock& clk) {
    s_.full += static_cast<float>(r_.feed_bite);
    s_.clean += static_cast<float>(r_.feed_clean_per_bite);
    Clamp();
    if (!feeding_ || !feed_first_bite_) {
        return;
    }
    feed_first_bite_ = false;
    day_.cared = 1;
    if (clk.valid) {
        if (r_.breakfast.Contains(clk.minute)) day_.fed |= 1;
        if (r_.lunch.Contains(clk.minute)) day_.fed |= 2;
        if (r_.dinner.Contains(clk.minute)) day_.fed |= 4;
    }
    if (!first_feed_done_) {
        first_feed_done_ = true;
        first_feed_pending_ = true;
    }
    if (feed_was_hungry_) {
        GainMood(static_cast<float>(r_.care_bonus_mood));
    }
}

bool Model::TakeFirstFeed() {
    const bool pending = first_feed_pending_;
    first_feed_pending_ = false;
    return pending;
}

void Model::Bath(const Clock& clk) {
    (void)clk;
    const bool was_dirty = s_.clean < r_.dirty;
    s_.clean += static_cast<float>(r_.bath);
    day_.cared = 1;
    Clamp();
    if (was_dirty) {
        GainMood(static_cast<float>(r_.care_bonus_mood));
    }
}

bool Model::PutToBed(const Clock& clk) {
    day_.cared = 1;
    if (clk.valid && r_.bed_window.Contains(clk.minute)) {
        in_bed_ = true;
        day_.bed = 1;
        return true;
    }
    return false;
}

int Model::GainMood(float v) {
    if (!(v > 0.0f)) {
        return 0;
    }
    const float before = s_.mood;
    const float target = std::min(s_.mood + v, static_cast<float>(Ceiling()));
    if (target > s_.mood) {
        s_.mood = target;
    }
    Clamp();
    return static_cast<int>(std::lround(s_.mood - before));
}

int Model::Chat(const Clock& clk) {
    Interaction();
    if (mode_ == Mode::Doze) {
        Wake(clk);
    }
    if (day_.chats < 0xFF) {
        ++day_.chats;
    }
    return GainMood(static_cast<float>(r_.chat_mood));
}

int Model::RewardGame(int base) {
    if (base <= 0) {
        return 0;
    }
    return GainMood(static_cast<float>(base) * GameFactor());
}

int Model::PreviewGameReward(int base) const {
    if (base <= 0) {
        return 0;
    }
    const float mood = std::min(100.0f, std::max(static_cast<float>(r_.floor_mood), s_.mood));
    const float target = std::min(std::min(100.0f, mood + static_cast<float>(base) * GameFactor()),
                                  static_cast<float>(Ceiling()));
    return target > mood ? static_cast<int>(std::lround(target - mood)) : 0;
}

int Model::RewardStudy(int base) {
    if (day_.study < 0xFF) {
        ++day_.study;
    }
    return GainMood(static_cast<float>(base));
}

Need Model::Bubble(const Clock& clk) const {
    if (mode_ != Mode::Awake) {
        return Need::None;
    }
    if (clk.valid && (clk.minute >= r_.sleepy_from || clk.minute < r_.wake)) {
        return Need::Sleepy;
    }
    struct Candidate { Need need; float ratio; };
    const Candidate candidates[] = {
        {Need::Hungry, s_.full / static_cast<float>(r_.hungry)},
        {Need::Dirty, s_.clean / static_cast<float>(r_.dirty)},
        {Need::Tired, s_.energy / static_cast<float>(std::max(1, r_.tired))},
        {Need::Lonely, s_.mood / static_cast<float>(r_.lonely)},
    };
    Need best = Need::None;
    float best_ratio = 0.0f;
    for (const auto& c : candidates) {
        if (Showing(c.need) && (best == Need::None || c.ratio < best_ratio)) {
            best = c.need;
            best_ratio = c.ratio;
        }
    }
    return best;
}

bool Model::AskAllowed(bool can_play) const {
    return can_play && mode_ == Mode::Awake && day_.asks < r_.ask_max_per_day &&
           now_s_ - last_ask_s_ >= r_.ask_cooldown_min * 60.0;
}

Need Model::TakeAsk(Need need) {
    if (need != Need::None) {
        ++day_.asks;
        last_ask_s_ = now_s_;
    }
    return need;
}

Need Model::PollAsk(const Clock& clk, bool can_play) {
    if (!AskAllowed(can_play) || !clk.valid) {
        return Need::None;
    }
    if (now_s_ - last_interaction_s_ > r_.ask_presence_min * 60.0) {
        return Need::None;  // nobody around: never talk to an empty room
    }
    // Same order as tools/care_sim.py: a bath before bed beats "sleepy".
    const int m = clk.minute;
    const bool meal = r_.breakfast.Contains(m) || r_.lunch.Contains(m) || r_.dinner.Contains(m);
    if (Showing(Need::Hungry) && meal) {
        return TakeAsk(Need::Hungry);
    }
    if (Showing(Need::Dirty) && r_.bath_window.Contains(m)) {
        return TakeAsk(Need::Dirty);
    }
    if (m >= r_.sleepy_from || m < r_.wake) {
        return TakeAsk(Need::Sleepy);
    }
    return Need::None;
}

Need Model::AskBeforeGame(bool can_play) {
    if (!AskAllowed(can_play) || !Exhausted()) {
        return Need::None;
    }
    return TakeAsk(Need::Tired);
}

// ---------------------------------------------------------------------------
// HUY HIỆU
// ---------------------------------------------------------------------------

namespace {

// Order must match Badge.
const BadgeInfo kBadgeInfo[kBadgeCount] = {
    {"TUẦN ĐỒNG", "10", 0xCD7F32, true},
    {"TUẦN BẠC", "15", 0xC0C8D2, true},
    {"TUẦN VÀNG", "20", 0xF5C542, true},
    {"TUẦN HOÀN HẢO", "21", 0x7FE8FF, true},
    {"ĂN SÁNG ĐỀU", "7", 0xFF7F50, true},
    {"NGỦ ĐÚNG GIỜ", "7", 0x9C8CFF, true},
    {"SẠCH SẼ", "7", 0x58F5C9, true},
    {"BỮA ĐẦU TIÊN", "1", 0xFF9F43, false},
    {"ĐẦU BẾP NHÍ", "3", 0xFFB347, true},
    {"7 NGÀY BÊN NHAU", "7", 0xFF6FA8, false},
    {"30 NGÀY BÊN NHAU", "30", 0xFF6FA8, false},
    {"100 NGÀY BÊN NHAU", "100", 0xFF6FA8, false},
    {"365 NGÀY BÊN NHAU", "365", 0xFF6FA8, false},
    {"MỚI QUEN", "1", 0x70C1FF, false},
    {"HIỂU BẠN", "2", 0x70C1FF, false},
    {"CÓ CÁ TÍNH", "3", 0x70C1FF, false},
    {"BẠN THÂN", "4", 0x70C1FF, false},
};

constexpr int kTogetherDays[4] = {7, 30, 100, 365};

}  // namespace

const BadgeInfo& Info(Badge badge) {
    const int i = static_cast<int>(badge);
    return kBadgeInfo[(i >= 0 && i < kBadgeCount) ? i : 0];
}

void Badges::Award(Badge b) {
    const int i = static_cast<int>(b);
    if (st_.count[i] < 0xFFFF) {
        ++st_.count[i];
    }
    if (st_.pending_n < sizeof(st_.pending)) {
        st_.pending[st_.pending_n++] = static_cast<uint8_t>(i);
    }
}

void Badges::OnDay(const DaySummary& s) {
    const int32_t week = WeekOf(s.index);
    if (week != st_.week) {
        st_.week = week;
        st_.week_points = 0;
        st_.week_tiers = 0;
    }
    st_.week_points = static_cast<uint8_t>(std::min(255, st_.week_points + s.anchors));
    const struct { int threshold; Badge badge; } tiers[] = {
        {r_.week_bronze, Badge::WeekBronze},
        {r_.week_silver, Badge::WeekSilver},
        {r_.week_gold, Badge::WeekGold},
        {r_.week_perfect, Badge::WeekPerfect},
    };
    for (int t = 0; t < 4; ++t) {
        const uint8_t bit = static_cast<uint8_t>(1u << t);
        if (st_.week_points >= tiers[t].threshold && (st_.week_tiers & bit) == 0) {
            st_.week_tiers |= bit;
            Award(tiers[t].badge);
        }
    }

    struct Streak { uint8_t* run; bool ok; Badge badge; };
    const Streak streaks[] = {
        {&st_.streak_breakfast, s.breakfast, Badge::Breakfast7},
        {&st_.streak_bed, s.bed, Badge::Bed7},
        {&st_.streak_clean, s.clean_bed, Badge::Clean7},
    };
    for (const auto& streak : streaks) {
        *streak.run = streak.ok ? static_cast<uint8_t>(*streak.run + 1) : 0;
        if (*streak.run >= 7) {
            *streak.run = 0;
            Award(streak.badge);
        }
    }

    if (s.chef) {
        Award(Badge::Chef);
    }
    if (s.cared && st_.days_together < 0xFFFF) {
        ++st_.days_together;
        for (int i = 0; i < 4; ++i) {
            if (st_.days_together == kTogetherDays[i]) {
                Award(static_cast<Badge>(static_cast<int>(Badge::Together7) + i));
            }
        }
    }
    st_.last_day = s.index;
}

void Badges::OnFirstFeed() {
    if (Count(Badge::FirstFeed) == 0) {
        Award(Badge::FirstFeed);
    }
}

void Badges::OnStage(int stage) {
    stage = std::min(3, std::max(0, stage));
    for (int i = 0; i <= stage; ++i) {
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        if ((st_.stage_done & bit) == 0) {
            st_.stage_done |= bit;
            Award(static_cast<Badge>(static_cast<int>(Badge::StageNew) + i));
        }
    }
}

bool Badges::PeekPending(Badge* out) const {
    if (st_.pending_n == 0) {
        return false;
    }
    if (out != nullptr) {
        *out = static_cast<Badge>(st_.pending[0]);
    }
    return true;
}

bool Badges::PopPending(Badge* out) {
    if (!PeekPending(out)) {
        return false;
    }
    for (int i = 1; i < st_.pending_n; ++i) {
        st_.pending[i - 1] = st_.pending[i];
    }
    --st_.pending_n;
    return true;
}

void Badges::Progress(Badge b, int* have, int* need) const {
    int h = 0, n = 0;
    switch (b) {
    case Badge::WeekBronze:  h = st_.week_points; n = r_.week_bronze; break;
    case Badge::WeekSilver:  h = st_.week_points; n = r_.week_silver; break;
    case Badge::WeekGold:    h = st_.week_points; n = r_.week_gold; break;
    case Badge::WeekPerfect: h = st_.week_points; n = r_.week_perfect; break;
    case Badge::Breakfast7:  h = st_.streak_breakfast; n = 7; break;
    case Badge::Bed7:        h = st_.streak_bed; n = 7; break;
    case Badge::Clean7:      h = st_.streak_clean; n = 7; break;
    case Badge::Together7:   h = st_.days_together; n = 7; break;
    case Badge::Together30:  h = st_.days_together; n = 30; break;
    case Badge::Together100: h = st_.days_together; n = 100; break;
    case Badge::Together365: h = st_.days_together; n = 365; break;
    default: break;
    }
    if (have != nullptr) *have = std::min(h, n);
    if (need != nullptr) *need = n;
}

int StageForLevel(int level, const Rules& rules) {
    int stage = 0;
    for (int i = 0; i < 3; ++i) {
        if (level >= rules.stage_levels[i]) {
            stage = i + 1;
        }
    }
    return stage;
}

const char* StageName(int stage) {
    static const char* const kNames[] = {"MỚI QUEN", "HIỂU BẠN", "CÓ CÁ TÍNH", "BẠN THÂN"};
    return kNames[std::min(3, std::max(0, stage))];
}

const char* TraitName(Trait trait) {
    switch (trait) {
    case Trait::Playful: return "HAM CHƠI";
    case Trait::Curious: return "HAM HỌC";
    case Trait::Chatty:  return "HAY NÓI";
    default:             return "";
    }
}

}  // namespace care
