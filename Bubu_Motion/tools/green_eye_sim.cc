// Host simulator for MẮT XANH (main/display/green_eye_game.cc).
//
// Two jobs:
//   1. Fuzz the rules: thousands of games with random taps, asserting the
//      invariants the screen and the child rely on (exactly one green, no
//      repeated colours, no green in the same place twice, colours unlocked
//      only at their score, taps outside the look window never judged, ...).
//   2. Play it with two simple player models -- a young child and an adult --
//      and print how long a game lasts and what it pays CareSystem, so the
//      pace constants are tuned against numbers rather than guesses.
//
// Build and run from the repo root:
//   c++ -std=c++17 -O2 -I main/display tools/green_eye_sim.cc \
//       main/display/green_eye_game.cc -o /tmp/green_eye_sim && /tmp/green_eye_sim
#include "green_eye_game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace GreenEyeGame;

namespace {

constexpr uint32_t kTickMs = 33;
int failures = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            if (failures < 20) {                                             \
                std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
                std::printf(__VA_ARGS__);                                    \
                std::printf("\n");                                           \
            }                                                                \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

bool IsXanh(Hue h) { return h == Hue::kBlue || h == Hue::kCyan; }

int ExpectedEyes(uint16_t score) { return score >= 10 ? 4 : score >= 4 ? 3 : 2; }

uint32_t ExpectedWindow(uint16_t score) {
    const int w = 2600 - 60 * static_cast<int>(score);
    return static_cast<uint32_t>(std::max(w, 850));
}

// Checks one freshly opened round against the rules, given what came before.
struct RoundAudit {
    int prev_green_x = -1000;
    int prev_green_y = -1000;
    bool prev_missed = false;
    long rounds = 0;
    long rounds_after_14 = 0;
    long xanh_after_14 = 0;

    void Check(const State& s, uint16_t score_at_start) {
        ++rounds;
        CHECK(s.eye_count == ExpectedEyes(score_at_start), "eyes %d at score %u", s.eye_count, score_at_start);
        int greens = 0;
        bool has_xanh = false;
        for (int i = 0; i < s.eye_count; ++i) {
            const Eye& e = s.eyes[i];
            greens += e.hue == Hue::kGreen;
            has_xanh = has_xanh || IsXanh(e.hue);
            for (int j = i + 1; j < s.eye_count; ++j) {
                CHECK(e.hue != s.eyes[j].hue, "repeated hue %d", static_cast<int>(e.hue));
            }
            if (score_at_start < 6) {
                CHECK(e.hue != Hue::kBlue && e.hue != Hue::kOrange && e.hue != Hue::kPink,
                      "hue %d before score 6 (score %u)", static_cast<int>(e.hue), score_at_start);
            }
            if (score_at_start < 14) {
                CHECK(e.hue != Hue::kCyan, "cyan before score 14 (score %u)", score_at_start);
            }
        }
        CHECK(greens == 1, "%d greens", greens);
        CHECK(s.green_index >= 0 && s.green_index < s.eye_count &&
                  s.eyes[s.green_index].hue == Hue::kGreen,
              "green_index %d", s.green_index);
        const Eye& g = s.eyes[s.green_index];
        CHECK(std::abs(g.x - prev_green_x) > 24 || std::abs(g.y - prev_green_y) > 24,
              "green in the same place twice (%d,%d)", g.x, g.y);
        const uint32_t expect = ExpectedWindow(score_at_start) + (prev_missed ? 300 : 0);
        CHECK(s.window_ms == expect, "window %u, expected %u", s.window_ms, expect);
        if (score_at_start >= 14) {
            ++rounds_after_14;
            xanh_after_14 += has_xanh;
        }
        prev_green_x = g.x;
        prev_green_y = g.y;
    }
};

// Fuzz: random taps anywhere at random times, including outside the window.
void Fuzz(int games) {
    std::mt19937 rng(12345);
    RoundAudit audit;
    long taps_judged = 0, taps_ignored = 0;
    for (int game = 0; game < games; ++game) {
        uint32_t now = 1000 + rng() % 100000;
        Start(now, rng());
        audit.prev_green_x = audit.prev_green_y = -1000;
        audit.prev_missed = false;
        CHECK(GetState().phase == Phase::kCountdown, "not in countdown");
        uint16_t last_decisions = 0;
        uint32_t last_open_start = UINT32_MAX;
        // Called after every Update() and every HandleTap(): a tap runs Update()
        // itself, so a round can open inside it.
        auto observe = [&]() {
            const State& s = GetState();
            if (s.phase == Phase::kOpening && s.phase_start_ms != last_open_start) {
                last_open_start = s.phase_start_ms;
                audit.Check(s, s.score);
            }
            if (s.decisions != last_decisions) {
                last_decisions = s.decisions;
                audit.prev_missed = s.outcome != Outcome::kHit;
                CHECK(s.phase == Phase::kFeedback, "decided outside feedback");
                if (s.outcome == Outcome::kTimeout) {
                    CHECK(s.decided_permille == 0, "timeout with rim left");
                }
            }
        };
        int guard = 0;
        // Make some players good, so the fuzz also reaches the high-score rules.
        const int skill = static_cast<int>(rng() % 100);
        while (GetState().phase != Phase::kOver && ++guard < 200000) {
            now += kTickMs;
            Update(now);
            observe();
            const State& s = GetState();
            // A tap now and then, anywhere on the panel.
            if (rng() % 7 == 0) {
                int x = static_cast<int>(rng() % 240), y = static_cast<int>(rng() % 240);
                if (s.phase == Phase::kLooking && static_cast<int>(rng() % 100) < skill) {
                    x = s.eyes[s.green_index].x + static_cast<int>(rng() % 21) - 10;
                    y = s.eyes[s.green_index].y + static_cast<int>(rng() % 21) - 10;
                }
                const Phase before = s.phase;
                const uint16_t lives_before = s.lives, score_before = s.score;
                const bool on_green = before == Phase::kLooking && EyeAt(x, y) == s.green_index;
                const TapResult r = HandleTap(x, y, now);
                observe();
                const State& after = GetState();
                if (r == TapResult::kIgnored) {
                    ++taps_ignored;
                    CHECK(after.score == score_before, "ignored tap changed score");
                } else {
                    ++taps_judged;
                    CHECK(before == Phase::kLooking, "judged a tap in phase %d", static_cast<int>(before));
                    if (r == TapResult::kHit) {
                        CHECK(on_green, "hit off the green eye");
                        CHECK(after.score == score_before + 1, "hit did not score");
                    } else {
                        CHECK(after.lives + 1 == lives_before, "wrong tap did not cost a heart");
                    }
                }
            }
        }
        CHECK(guard < 200000, "game %d never ended", game);
        CHECK(GetState().lives == 0, "over with %u lives", GetState().lives);
        CHECK(!IsRunning(), "kOver still running");
    }
    std::printf("fuzz: %d games, %ld rounds, %ld judged taps, %ld ignored taps\n",
                games, audit.rounds, taps_judged, taps_ignored);
    if (audit.rounds_after_14 > 0) {
        std::printf("fuzz: rounds from 14 points with blue/cyan present: %.0f%% of %ld\n",
                    100.0 * audit.xanh_after_14 / audit.rounds_after_14, audit.rounds_after_14);
        CHECK(audit.xanh_after_14 * 100 >= audit.rounds_after_14 * 65, "xanh too rare after 14");
    }
}

// A round with no tap times out at window + 200 ms grace, not before.
void TimeoutEdge() {
    Start(0, 7);
    uint32_t now = 0;
    while (GetState().phase != Phase::kLooking) {
        now += 1;
        Update(now);
    }
    const uint32_t opened = now;
    const uint32_t window = GetState().window_ms;
    while (GetState().phase == Phase::kLooking) {
        now += 1;
        Update(now);
    }
    CHECK(now - opened == window + 200, "timed out after %u ms, window %u", now - opened, window);
    CHECK(GetState().outcome == Outcome::kTimeout && GetState().lives == kLives - 1, "timeout bookkeeping");

    // A green tapped inside the grace still counts.
    Start(0, 9);
    now = 0;
    while (GetState().phase != Phase::kLooking) Update(++now);
    const uint32_t opened2 = now;
    const State& s = GetState();
    const TapResult r = HandleTap(s.eyes[s.green_index].x, s.eyes[s.green_index].y,
                                  opened2 + s.window_ms + 150);
    CHECK(r == TapResult::kHit, "tap in the grace was not a hit");
}

// ---- Player models ------------------------------------------------------------

struct Player {
    const char* name;
    double base_ms;        // median reaction to a 2-eye round
    double per_eye_ms;     // extra per eye beyond two
    double xanh_ms;        // extra when blue/cyan is on screen
    double spread;         // lognormal sigma
    double err_base;       // chance of tapping a wrong colour
    double err_xanh;       // extra, when blue/cyan is on screen
    double err_per_eye;
    double press_ms;       // finger down -> release (taps dispatch on release)
};

struct GameResult {
    uint16_t score;
    double seconds;
};

GameResult PlayOne(const Player& p, std::mt19937& rng) {
    std::lognormal_distribution<double> noise(0.0, p.spread);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    uint32_t now = 0;
    Start(now, rng());
    const uint32_t started = now;
    uint32_t tap_at = 0;
    int tap_eye = -1;
    Phase last = Phase::kCountdown;
    int guard = 0;
    while (GetState().phase != Phase::kOver && ++guard < 400000) {
        now += kTickMs;
        Update(now);
        const State& s = GetState();
        if (s.phase == Phase::kLooking && last != Phase::kLooking) {
            bool xanh = false;
            for (int i = 0; i < s.eye_count; ++i) xanh = xanh || IsXanh(s.eyes[i].hue);
            const double react = (p.base_ms + p.per_eye_ms * (s.eye_count - 2) + (xanh ? p.xanh_ms : 0)) *
                                 noise(rng) + p.press_ms;
            tap_at = now + static_cast<uint32_t>(react);
            const double err = p.err_base + (xanh ? p.err_xanh : 0) + p.err_per_eye * (s.eye_count - 2);
            tap_eye = s.green_index;
            if (unit(rng) < err) {
                // A wrong tap goes to a xanh eye when there is one -- that is the confusion.
                int pick = -1;
                for (int i = 0; i < s.eye_count; ++i) {
                    if (i != s.green_index && (IsXanh(s.eyes[i].hue) || pick < 0)) pick = i;
                }
                tap_eye = pick;
            }
        }
        if (s.phase == Phase::kLooking && tap_eye >= 0 && now >= tap_at) {
            HandleTap(s.eyes[tap_eye].x, s.eyes[tap_eye].y, now);
            tap_eye = -1;
        }
        last = GetState().phase;
    }
    return {GetState().score, (now - started) / 1000.0};
}

template <typename T>
T Pct(std::vector<T> v, double q) {
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(q * (v.size() - 1))];
}

void Model(const Player& p, int games) {
    std::mt19937 rng(424242);
    std::vector<int> scores, rewards;
    std::vector<double> seconds;
    for (int i = 0; i < games; ++i) {
        const GameResult r = PlayOne(p, rng);
        scores.push_back(r.score);
        rewards.push_back(MoodReward(r.score));
        seconds.push_back(r.seconds);
    }
    std::printf("%-6s score p10/p50/p90 %3d /%3d /%3d   game %4.0f /%4.0f /%4.0f s   "
                "CẢM XÚC +%d /+%d /+%d\n",
                p.name, Pct(scores, 0.1), Pct(scores, 0.5), Pct(scores, 0.9),
                Pct(seconds, 0.1), Pct(seconds, 0.5), Pct(seconds, 0.9),
                Pct(rewards, 0.1), Pct(rewards, 0.5), Pct(rewards, 0.9));
}

}  // namespace

int main() {
    Fuzz(3000);
    TimeoutEdge();

    // Rough models, not measurements: a 4-6 year old is slow to find a colour
    // and confuses blue/cyan with green; an adult mostly does not.
    const Player kid   {"kid",   1050, 220, 300, 0.30, 0.04, 0.14, 0.04, 120};
    const Player older {"7-10y",  750, 150, 180, 0.25, 0.02, 0.06, 0.02, 100};
    const Player adult {"adult",  520,  90, 110, 0.20, 0.01, 0.03, 0.01,  90};
    Model(kid, 2000);
    Model(older, 2000);
    Model(adult, 2000);

    std::printf("RESULT: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
