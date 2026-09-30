// Host tests for the care rules in main/care_model.{h,cc}.
//
// The model has no device dependencies, so it builds and runs here:
//   g++ -std=c++17 -Wall -Wextra -I main main/care_model.cc tools/care_model_test.cc -o /tmp/care_model_test && /tmp/care_model_test
//
// Expected numbers come from Rules (the same values as tools/care_sim.py) and
// the arithmetic is spelled out next to each check.
#include "care_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace care;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                    \
    do {                                                                         \
        ++g_checks;                                                              \
        const double _a = (a), _b = (b);                                         \
        if (std::fabs(_a - _b) > (eps)) {                                        \
            ++g_failures;                                                        \
            std::printf("FAIL %s:%d  %s = %.3f, want %.3f\n", __FILE__, __LINE__, \
                        #a, _a, _b);                                             \
        }                                                                        \
    } while (0)

constexpr int32_t kWed = 20726;  // 2026-09-30, a Wednesday

Clock At(int32_t day, int h, int m = 0) {
    Clock c;
    c.valid = true;
    c.day = day;
    c.minute = Hm(h, m);
    return c;
}

// Advance minute by minute from `from` to `to` (same or next day), so the
// model sees every time-of-day boundary on the way.
void Run(Model& model, int32_t day, int from_h, int from_m, int to_h, int to_m) {
    int32_t d = day;
    int minute = Hm(from_h, from_m);
    const int end = Hm(to_h, to_m) + (Hm(to_h, to_m) < minute ? 24 * 60 : 0);
    for (int m = minute; m < end; ++m) {
        const int mm = m % (24 * 60);
        const int32_t dd = d + m / (24 * 60);
        Clock c;
        c.valid = true;
        c.day = dd;
        c.minute = (mm + 1) % (24 * 60);
        if (mm + 1 == 24 * 60) {
            c.day = dd + 1;
        }
        model.Tick(60.0f, c);
    }
}

Model Fresh(float full, float energy, float clean, float mood, int32_t day = kWed) {
    Model model;
    Snapshot snap;
    snap.full10 = static_cast<int16_t>(full * 10);
    snap.energy10 = static_cast<int16_t>(energy * 10);
    snap.clean10 = static_cast<int16_t>(clean * 10);
    snap.mood10 = static_cast<int16_t>(mood * 10);
    snap.day.index = day;
    model.Restore(snap);
    return model;
}

void TestCalendar() {
    CHECK(DaysFromCivil(1970, 1, 1) == 0);
    CHECK(DaysFromCivil(2026, 9, 30) == 20726);
    CHECK(DaysFromCivil(2024, 2, 29) == 19782);
    CHECK(DaysFromCivil(2000, 3, 1) == 11017);
    CHECK(Weekday(20726) == 2);            // Wednesday
    CHECK(Weekday(20731) == 0);            // Monday 2026-10-05
    CHECK(WeekOf(20731) == WeekOf(20731 + 6));
    CHECK(WeekOf(20731) == WeekOf(20730) + 1);  // Sunday -> Monday starts a week
}

void TestAwakeDrift() {
    Model m = Fresh(80, 80, 80, 50);
    const Clock c = At(kWed, 10);
    m.Tick(3600.0f, c);  // one awake hour, nothing showing
    CHECK_NEAR(m.stats().full, 80 - 3.5, 0.01);
    CHECK_NEAR(m.stats().clean, 80 - 1.5, 0.01);
    CHECK_NEAR(m.stats().mood, 50 - 2.5, 0.01);
    CHECK_NEAR(m.stats().energy, 80 - 2.0, 0.01);
}

void TestDozeRecharges() {
    Model m = Fresh(80, 20, 80, 50);
    const Clock c = At(kWed, 14);
    m.SetAsleep(true, c);
    CHECK(m.mode() == Mode::Doze);
    m.Tick(600.0f, c);  // 10 minutes dozing: +2 per minute
    CHECK_NEAR(m.stats().energy, 40.0, 0.01);
    CHECK_NEAR(m.stats().full, 80 - 10.0 / 60.0, 0.01);
}

void TestNightAndMorning() {
    Model m = Fresh(80, 50, 80, 70);
    // Nobody puts Bubu to bed: dozing from 21:00, the night starts at 21:30.
    m.SetAsleep(true, At(kWed, 21));
    CHECK(m.mode() == Mode::Doze);
    Run(m, kWed, 21, 0, 21, 30);
    CHECK(m.mode() == Mode::Night);
    CHECK(m.day().bedtime_set == 1);
    CHECK(m.day().bed == 0);                        // no credit without NGỦ
    Run(m, kWed, 21, 30, 6, 30);                    // through midnight
    CHECK(m.mode() == Mode::Doze);                  // morning: awake-ish, screen asleep
    CHECK(m.stats().full <= 35.0f + 0.01f);         // "Bubu ngủ dậy là đói"
    CHECK_NEAR(m.stats().energy, 100.0, 0.01);      // 9 h at +15/h from 50
}

void TestBedCredit() {
    Model m = Fresh(80, 50, 80, 70);
    CHECK(!m.PutToBed(At(kWed, 18)));               // before the window: a nap
    CHECK(m.PutToBed(At(kWed, 20, 45)));            // in the window
    m.SetAsleep(true, At(kWed, 20, 45));
    CHECK(m.mode() == Mode::Night);
    CHECK(m.day().bed == 1);
    // Saying good night by voice is allowed and costs nothing (decision 3).
    CHECK(m.Chat(At(kWed, 20, 55)) >= 0);
    CHECK(m.mode() == Mode::Night);
    CHECK(m.day().bed == 1);
    // Woken again the same evening by a touch: not bedtime after all.
    m.Touch(At(kWed, 21, 10));
    CHECK(m.day().bed == 0);
    CHECK(m.day().bedtime_set == 0);
    CHECK(m.mode() == Mode::Awake);
}

void TestWaking() {
    // Talking ends a nap.
    Model nap = Fresh(80, 50, 80, 50);
    nap.SetAsleep(true, At(kWed, 14));
    CHECK(nap.mode() == Mode::Doze);
    CHECK(nap.Chat(At(kWed, 14, 10)) == 10);
    CHECK(nap.mode() == Mode::Awake);
    // Presence alone (a care action's bookkeeping) does not wake Bubu; the
    // child's touch does.
    Model m = Fresh(80, 50, 80, 50);
    m.SetAsleep(true, At(kWed, 14));
    m.Interaction();
    CHECK(m.mode() == Mode::Doze);
    m.Touch(At(kWed, 14, 5));
    CHECK(m.mode() == Mode::Awake);
    // Presence is what lets the day's XP be paid (CareSystem::Update).
    m.Tick(30.0f, At(kWed, 14, 6));
    CHECK(m.Present(60.0));
    m.Tick(60.0f, At(kWed, 14, 7));
    CHECK(!m.Present(60.0));
    // A long nap ends hungry.
    Model longnap = Fresh(80, 50, 80, 50);
    longnap.SetAsleep(true, At(kWed, 13));
    Run(longnap, kWed, 13, 0, 15, 0);
    longnap.Touch(At(kWed, 15));
    CHECK(longnap.stats().full <= 35.0f + 0.01f);
}

void TestFeeding() {
    Model m = Fresh(30, 80, 80, 50);
    CHECK(m.BeginFeed());
    const Clock breakfast = At(kWed, 7);
    m.FeedBite(breakfast);
    CHECK_NEAR(m.stats().full, 40.0, 0.01);
    CHECK_NEAR(m.stats().clean, 79.0, 0.01);        // crumbs
    CHECK(m.day().fed == 1);
    CHECK_NEAR(m.stats().mood, 55.0, 0.01);         // was hungry: +5 care bonus
    m.FeedBite(breakfast);
    m.FeedBite(breakfast);
    CHECK_NEAR(m.stats().full, 60.0, 0.01);
    CHECK_NEAR(m.stats().mood, 55.0, 0.01);         // the bonus is paid once
    CHECK(m.TakeFirstFeed());
    CHECK(!m.TakeFirstFeed());

    Model full = Fresh(90, 80, 80, 50);
    CHECK(!full.BeginFeed());                       // >= 85 refuses
}

void TestCeiling() {
    Model m = Fresh(30, 20, 80, 50);                // hungry + tired showing
    CHECK(m.Ceiling() == 60);
    CHECK(m.GainMood(30) == 10);                    // 50 -> capped at 60
    CHECK_NEAR(m.stats().mood, 60.0, 0.01);

    Model above = Fresh(30, 80, 80, 95);            // hungry: ceiling 80
    above.Tick(1800.0f, At(kWed, 10));              // half an hour awake
    // Pull -20/h for 0.5 h plus awake drift -2.5/h: 95 - 10 - 1.25 = 83.75.
    CHECK_NEAR(above.stats().mood, 83.75, 0.05);
    above.Tick(3600.0f, At(kWed, 11));
    CHECK_NEAR(above.stats().mood, 80.0, 0.05);     // stops at the ceiling
}

void TestGames() {
    Model m = Fresh(80, 100, 80, 40);
    m.SetPlaying(true);
    m.Tick(57.0f * 60.0f, At(kWed, 17));
    // 57 min: -1.2/min playing and -2/h awake = -70.3 -> 29.7, just tired.
    CHECK_NEAR(m.stats().energy, 100 - 57 * 1.2 - 57 * 2.0 / 60.0, 0.05);
    CHECK(m.GameFactor() == 0.5f);
    CHECK(m.day().game_min == 57);
    CHECK_NEAR(m.stats().clean, 80 - 57 * 0.7 - 57 * 1.5 / 60.0, 0.05);
    m.SetPlaying(false);

    Model fresh = Fresh(80, 100, 80, 40);
    CHECK(fresh.PreviewGameReward(12) == 12);
    CHECK_NEAR(fresh.stats().mood, 40.0, 0.01);     // preview changes nothing
    CHECK(fresh.RewardGame(12) == 12);
    Model near_top = Fresh(80, 100, 80, 95);
    CHECK(near_top.PreviewGameReward(12) == 5);     // 100 is the most there is
    Model hungry_player = Fresh(30, 100, 80, 75);   // ceiling 80
    CHECK(hungry_player.PreviewGameReward(12) == 5);
    CHECK(hungry_player.RewardGame(12) == 5);
    Model tired = Fresh(80, 20, 80, 40);
    CHECK(tired.RewardGame(12) == 6);
    Model spent = Fresh(80, 5, 80, 40);
    CHECK(spent.RewardGame(12) == 0);
}

void TestAsks() {
    Model m = Fresh(30, 80, 80, 70);                // hungry
    const Clock lunch = At(kWed, 12);
    CHECK(m.PollAsk(lunch, true) == Need::None);    // nobody around yet
    m.Interaction();
    CHECK(m.PollAsk(lunch, false) == Need::None);   // not allowed to play sound
    CHECK(m.PollAsk(lunch, true) == Need::Hungry);
    CHECK(m.PollAsk(lunch, true) == Need::None);    // 45 min cooldown
    m.Tick(44 * 60.0f, lunch);
    m.Interaction();
    CHECK(m.PollAsk(At(kWed, 12, 44), true) == Need::None);
    m.Tick(60.0f, At(kWed, 12, 45));
    CHECK(m.PollAsk(At(kWed, 12, 45), true) == Need::Hungry);

    Model late = Fresh(80, 80, 80, 70);
    late.Interaction();
    CHECK(late.PollAsk(At(kWed, 21, 5), true) == Need::Sleepy);

    Model out_of_window = Fresh(30, 80, 80, 70);    // hungry, but not a meal time
    out_of_window.Interaction();
    CHECK(out_of_window.PollAsk(At(kWed, 15), true) == Need::None);

    // A bath before bed: dirty is asked before sleepy, as in care_sim.py.
    Model grubby = Fresh(80, 80, 40, 70);
    grubby.Interaction();
    CHECK(grubby.PollAsk(At(kWed, 21, 10), true) == Need::Dirty);

    // Tired is only offered when a game is about to start, and only once
    // Bubu is exhausted.
    Model tired = Fresh(80, 20, 80, 70);
    tired.Interaction();
    CHECK(tired.PollAsk(At(kWed, 10), true) == Need::None);
    CHECK(tired.AskBeforeGame(true) == Need::None);
    Model spent = Fresh(80, 5, 80, 70);
    CHECK(spent.AskBeforeGame(false) == Need::None);
    CHECK(spent.AskBeforeGame(true) == Need::Tired);
    CHECK(spent.AskBeforeGame(true) == Need::None);  // 45 min cooldown
    CHECK(spent.day().asks == 1);

    // At most 4 a day.
    Rules quick;
    quick.ask_cooldown_min = 1;
    Model many(quick);
    Snapshot hungry;
    hungry.full10 = 300;
    hungry.day.index = kWed;
    many.Restore(hungry);
    int asks = 0;
    for (int i = 0; i < 12; ++i) {
        many.Interaction();
        if (many.PollAsk(At(kWed, 12, 2 * i), true) != Need::None) ++asks;
        many.Tick(120.0f, At(kWed, 12, 2 * i + 2));
    }
    CHECK(asks == 4);
}

void TestBubble() {
    Model m = Fresh(35, 80, 45, 70);                // hungry 35/40, dirty 45/50
    CHECK(m.Bubble(At(kWed, 10)) == Need::Hungry);  // 0.875 < 0.9
    CHECK(m.Bubble(At(kWed, 21, 15)) == Need::Sleepy);
    m.SetAsleep(true, At(kWed, 10));
    CHECK(m.Bubble(At(kWed, 10)) == Need::None);    // asleep: nothing to show
    Model ok = Fresh(80, 80, 80, 70);
    CHECK(ok.Bubble(At(kWed, 10)) == Need::None);
}

void TestRollover() {
    Model m = Fresh(30, 80, 80, 70);
    m.Interaction();
    CHECK(m.BeginFeed());
    for (int i = 0; i < 3; ++i) m.FeedBite(At(kWed, 7));
    m.Bath(At(kWed, 18));
    CHECK(m.PutToBed(At(kWed, 20, 50)));
    m.SetAsleep(true, At(kWed, 20, 50));
    Run(m, kWed, 20, 50, 0, 5);                     // past midnight
    DaySummary s;
    CHECK(m.PopSummary(&s));
    CHECK(s.index == kWed);
    CHECK(s.breakfast && s.clean_bed && s.bed && s.together);
    CHECK(s.anchors == 3);
    CHECK(s.xp == 35);                              // 3 x 10 + happy bonus 5
    CHECK(!m.PopSummary(&s));

    // Two days away (device off): empty days in between break the streaks.
    Model gone = Fresh(80, 80, 80, 70);
    gone.Interaction();
    gone.Tick(60.0f, At(kWed + 3, 10));
    int n = 0, xp = 0;
    while (gone.PopSummary(&s)) {
        ++n;
        xp += s.xp;
    }
    CHECK(n == 3);                                  // Wed, then the two empty days
    // Wed: together and still clean (80 >= 60) = 1 anchor, happy (70) = +5.
    // The two empty days pay nothing.
    CHECK(xp == 15);

    // Clean at bedtime only counts on a day spent together.
    Model alone = Fresh(80, 80, 95, 70);
    alone.Tick(60.0f, At(kWed + 1, 0, 1));
    CHECK(alone.PopSummary(&s));
    CHECK(!s.clean_bed && s.anchors == 0 && s.xp == 0);
}

void TestCatchUp() {
    Model m = Fresh(80, 20, 80, 70);
    m.CatchUp(8 * 3600.0f);
    CHECK_NEAR(m.stats().energy, 100.0, 0.01);      // a full night's sleep
    CHECK(m.stats().full <= 35.0f + 0.01f);
    Model away = Fresh(12, 50, 12, 21);
    away.CatchUp(30 * 3600.0f);
    CHECK(away.stats().full >= 25.0f && away.stats().clean >= 25.0f && away.stats().mood >= 25.0f);
}

void TestSnapshot() {
    Model m = Fresh(33.3f, 44.4f, 55.5f, 66.6f);
    m.Interaction();
    CHECK(m.PutToBed(At(kWed, 20)));
    const Snapshot snap = m.Save();
    Model back;
    back.Restore(snap);
    CHECK_NEAR(back.stats().full, 33.3, 0.06);
    CHECK_NEAR(back.stats().mood, 66.6, 0.06);
    CHECK(back.day().bed == 1 && back.day().together == 1 && back.day().index == kWed);
}

void TestBadges() {
    Badges b;
    // Week of Monday 2026-10-05: 3 anchors a day.
    const int32_t mon = 20731;
    for (int i = 0; i < 7; ++i) {
        DaySummary s;
        s.index = mon + i;
        s.breakfast = s.clean_bed = s.bed = s.together = s.cared = true;
        s.anchors = 3;
        b.OnDay(s);
    }
    CHECK(b.Count(Badge::WeekBronze) == 1);         // at 12 points (day 4)
    CHECK(b.Count(Badge::WeekSilver) == 1);         // at 15
    CHECK(b.Count(Badge::WeekGold) == 1);           // at 21 >= 20
    CHECK(b.Count(Badge::WeekPerfect) == 1);
    CHECK(b.Count(Badge::Breakfast7) == 1);
    CHECK(b.Count(Badge::Bed7) == 1);
    CHECK(b.Count(Badge::Clean7) == 1);
    CHECK(b.Count(Badge::Together7) == 1);
    Badge first;
    CHECK(b.PopPending(&first) && first == Badge::WeekBronze);

    // The next week starts from zero; a missed day restarts the streaks.
    DaySummary s;
    s.index = mon + 7;
    s.breakfast = true;
    s.anchors = 1;
    s.cared = true;
    s.together = true;
    b.OnDay(s);
    CHECK(b.state().week_points == 1);
    int have = 0, need = 0;
    b.Progress(Badge::WeekBronze, &have, &need);
    CHECK(have == 1 && need == 10);
    b.Progress(Badge::Bed7, &have, &need);
    CHECK(have == 0 && need == 7);

    b.OnFirstFeed();
    b.OnFirstFeed();
    CHECK(b.Count(Badge::FirstFeed) == 1);
    b.OnStage(2);
    CHECK(b.Count(Badge::StageNew) == 1 && b.Count(Badge::StageKnows) == 1 &&
          b.Count(Badge::StagePersonality) == 1 && b.Count(Badge::StageBestFriend) == 0);
    b.OnStage(2);
    CHECK(b.Count(Badge::StageKnows) == 1);

    CHECK(StageForLevel(1) == 0 && StageForLevel(3) == 1 && StageForLevel(6) == 2 && StageForLevel(12) == 3);
}

// An engaged school day, scripted end to end against the rules.
void TestEngagedDay() {
    Model m;
    m.StartNew();
    Clock c = At(kWed, 6, 0);
    m.Tick(1.0f, c);                                // starts the day
    m.SetAsleep(true, c);                           // still night
    Run(m, kWed, 6, 0, 6, 35);
    m.Touch(At(kWed, 6, 35));                       // child comes
    CHECK(m.Bubble(At(kWed, 6, 35)) == Need::Hungry);
    CHECK(m.BeginFeed());
    for (int i = 0; i < 3; ++i) m.FeedBite(At(kWed, 6, 36));
    m.SetAsleep(true, At(kWed, 6, 45));             // off to school
    Run(m, kWed, 6, 45, 16, 45);
    m.Touch(At(kWed, 16, 45));                      // home: long nap -> hungry
    CHECK(m.Showing(Need::Hungry));
    CHECK(m.BeginFeed());
    for (int i = 0; i < 3; ++i) m.FeedBite(At(kWed, 16, 46));
    m.Chat(At(kWed, 16, 46));
    m.SetPlaying(true);
    Run(m, kWed, 16, 46, 17, 16);                   // 30 min of games
    m.SetPlaying(false);
    m.RewardGame(12);
    CHECK(m.Showing(Need::Dirty) || m.stats().clean < 70.0f);
    m.Bath(At(kWed, 19, 0));
    Run(m, kWed, 17, 16, 20, 50);
    CHECK(m.PutToBed(At(kWed, 20, 50)));
    m.SetAsleep(true, At(kWed, 20, 50));
    Run(m, kWed, 20, 50, 0, 5);
    DaySummary s;
    CHECK(m.PopSummary(&s));
    CHECK(s.anchors == 3);
    CHECK(s.xp == 35);
}

}  // namespace

int main() {
    TestCalendar();
    TestAwakeDrift();
    TestDozeRecharges();
    TestWaking();
    TestNightAndMorning();
    TestBedCredit();
    TestFeeding();
    TestCeiling();
    TestGames();
    TestAsks();
    TestBubble();
    TestRollover();
    TestCatchUp();
    TestSnapshot();
    TestBadges();
    TestEngagedDay();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
