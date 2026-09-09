#include "quick_tap_game.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <esp_random.h>
#include <esp_timer.h>

namespace QuickTapGame {
namespace {

// Panel geometry. The dot lives inside the countdown ring, never under it.
// 240x240 panel -> centre (120,120); the ring's inner edge sits at r=100, so a
// field radius of 78 keeps a 15px dot 7px clear of it at full stretch.
constexpr int kCenterX = 120;
constexpr int kCenterY = 120;
constexpr int kDotRadius = 15;
constexpr int kFieldRadius = 78;
// Generous on purpose: a child's finger covers far more than the dot, and a
// near-miss reading as "nothing happened" is the worst feel in a reflex game.
constexpr int kTapSlackPx = 26;
// Far enough that the eye always registers the dot as having moved.
constexpr int kMinDotSeparationPx = 50;
constexpr int kSpawnAttempts = 8;

constexpr uint32_t kRoundMs = 30000;

constexpr std::array<Profile, static_cast<size_t>(Difficulty::kDifficultyCount)> kProfiles = {{
    {2000, 20, 1},   // DỄ
    {1400, 30, 2},   // VỪA
    { 900, 40, 3},   // KHÓ
}};

struct State {
    bool running = false;
    Mode mode = Mode::kSimple;
    Difficulty difficulty = Difficulty::kMedium;
    Result result = Result::kNone;

    uint32_t start_ms = 0;
    uint32_t dot_expire_ms = 0;
    Dot dot = {};

    uint16_t hits = 0;
    uint16_t misses = 0;
    uint16_t red_avoided = 0;
    uint16_t current_streak = 0;
    uint16_t longest_streak = 0;
};

State state;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t RandomBelow(uint32_t max_exclusive) {
    if (max_exclusive == 0) {
        return 0;
    }
    return esp_random() % max_exclusive;
}

float RandomUnit() {
    return static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX);
}

// Uniform over the disc: r = R*sqrt(u), not R*u, or every dot crowds the centre.
void SpawnDot() {
    const int16_t previous_x = state.dot.x;
    const int16_t previous_y = state.dot.y;
    const bool had_dot = state.dot.visible;

    int16_t x = kCenterX;
    int16_t y = kCenterY;
    for (int attempt = 0; attempt < kSpawnAttempts; ++attempt) {
        const float angle = RandomUnit() * 2.0f * static_cast<float>(M_PI);
        const float radius = static_cast<float>(kFieldRadius) * std::sqrt(RandomUnit());
        x = static_cast<int16_t>(kCenterX + radius * std::cos(angle));
        y = static_cast<int16_t>(kCenterY + radius * std::sin(angle));
        if (!had_dot) {
            break;
        }
        const int dx = x - previous_x;
        const int dy = y - previous_y;
        if (dx * dx + dy * dy >= kMinDotSeparationPx * kMinDotSeparationPx) {
            break;
        }
    }

    const Profile& profile = GetProfile(state.difficulty);
    state.dot.x = x;
    state.dot.y = y;
    state.dot.visible = true;
    state.dot.red = state.mode == Mode::kNoGo &&
                    RandomBelow(100) < profile.red_chance_pct;
    state.dot_expire_ms = NowMs() + profile.dot_lifetime_ms;
}

void Finish(Result result) {
    state.running = false;
    state.result = result;
    state.dot.visible = false;
}

}  // namespace

const Profile& GetProfile(Difficulty difficulty) {
    const size_t index = static_cast<size_t>(difficulty);
    if (index >= kProfiles.size()) {
        return kProfiles[static_cast<size_t>(Difficulty::kMedium)];
    }
    return kProfiles[index];
}

void Start(Mode mode, Difficulty difficulty) {
    state = State{};
    state.running = true;
    state.mode = mode;
    state.difficulty = difficulty;
    state.result = Result::kNone;
    state.start_ms = NowMs();
    SpawnDot();
}

void Stop() {
    if (!state.running) {
        return;
    }
    Finish(Result::kStopped);
}

void Update() {
    if (!state.running) {
        return;
    }

    const uint32_t now = NowMs();
    if (now - state.start_ms >= kRoundMs) {
        // A dot still on screen when the clock runs out is not a miss -- the
        // player never got its full window.
        Finish(Result::kTimeUp);
        return;
    }

    if (state.dot.visible && now >= state.dot_expire_ms) {
        if (state.dot.red) {
            // Correct rejection. Worth no points, but it does not break the
            // streak either: the streak counts white dots, and sitting still
            // for a red one is the right move.
            state.red_avoided++;
        } else {
            state.misses++;
            state.current_streak = 0;
        }
        SpawnDot();
    }
}

TapOutcome HandleTap(int x, int y) {
    if (!state.running || !state.dot.visible) {
        return TapOutcome::kIgnored;
    }

    const int dx = x - state.dot.x;
    const int dy = y - state.dot.y;
    if (dx * dx + dy * dy > kTapSlackPx * kTapSlackPx) {
        return TapOutcome::kIgnored;   // empty space costs nothing
    }

    if (state.dot.red) {
        Finish(Result::kTappedRed);
        return TapOutcome::kTappedRed;
    }

    state.hits++;
    state.current_streak++;
    state.longest_streak = std::max(state.longest_streak, state.current_streak);
    SpawnDot();
    return TapOutcome::kHit;
}

bool IsRunning() {
    return state.running;
}

Mode GetMode() {
    return state.mode;
}

Difficulty GetDifficulty() {
    return state.difficulty;
}

Result GetResult() {
    return state.result;
}

uint32_t RoundMs() {
    return kRoundMs;
}

uint32_t RemainingMs() {
    if (!state.running) {
        return 0;
    }
    const uint32_t elapsed = NowMs() - state.start_ms;
    return elapsed >= kRoundMs ? 0 : kRoundMs - elapsed;
}

Dot GetDot() {
    return state.dot;
}

int GetDotRadius() {
    return kDotRadius;
}

uint16_t GetHits() {
    return state.hits;
}

uint16_t GetMisses() {
    return state.misses;
}

uint16_t GetRedAvoided() {
    return state.red_avoided;
}

uint16_t GetLongestStreak() {
    return state.longest_streak;
}

int GetStreakPoints() {
    return static_cast<int>(state.longest_streak) *
           static_cast<int>(GetProfile(state.difficulty).streak_multiplier);
}

int GetScore() {
    const int score = static_cast<int>(state.hits) + GetStreakPoints() -
                      static_cast<int>(state.misses);
    // A child does not need to see "-6 điểm".
    return std::max(score, 0);
}

}  // namespace QuickTapGame
