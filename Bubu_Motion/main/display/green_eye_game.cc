#include "green_eye_game.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace GreenEyeGame {
namespace {

constexpr int kCenter = 120;

// ---- Pace -----------------------------------------------------------------
// The look window shrinks by kWindowStepMs per green found, down to the floor:
// 2600 ms at 0 points, 2000 at 10, 1400 at 20, the 850 ms floor from 30.
constexpr uint32_t kStartWindowMs = 2600;
constexpr uint32_t kWindowStepMs = 60;
constexpr uint32_t kMinWindowMs = 850;
// The round after a lost heart gets this much extra, once. A child who just
// missed is the one most likely to miss again straight away.
constexpr uint32_t kRecoveryMs = 300;
// Taps are dispatched on release, so they land after the finger went down.
// The rim shows the window; the judge keeps listening this long past it.
constexpr uint32_t kLateGraceMs = 200;

constexpr uint32_t kCountdownStepMs = 600;
constexpr uint32_t kOpenMs = 150;
constexpr uint32_t kHitFeedbackMs = 280;
// Long enough to see which eye was the green one -- a miss shows the answer.
constexpr uint32_t kMissFeedbackMs = 950;
constexpr uint32_t kCloseMs = 120;

// ---- Difficulty steps -------------------------------------------------------
constexpr uint16_t kThreeEyesFromScore = 4;
constexpr uint16_t kFourEyesFromScore = 10;
constexpr uint16_t kBlueFromScore = 6;
constexpr uint16_t kCyanFromScore = 14;
// Once unlocked, how often a round is made to include blue or cyan.
constexpr uint32_t kXanhChanceBluePct = 40;
constexpr uint32_t kXanhChanceCyanPct = 70;

// ---- Touch --------------------------------------------------------------------
// A child's finger is much bigger than the gap between eyes. A tap within this
// of an eye's edge counts for it; where two eyes' slack overlaps, the nearer
// edge wins (EyeAt), so it is safe to be generous.
constexpr int kTapSlackPx = 12;
// Two green eyes this close in consecutive rounds count as the same place, and
// the second round is not allowed to put it there -- no camping on one spot.
constexpr int kSamePlacePx = 24;

// Chosen for the GC9A01 in RGB565, and spread so that no two neighbours in
// hue are a coin toss. Green is a leaf green, not a teal: cyan sits 57 degrees
// of hue away from it, which is the trick, not an ambiguity.
constexpr uint32_t kHueRgb[] = {
    0x2EDB4B,   // kGreen   xanh lá
    0xFF3B3B,   // kRed
    0xFFD21F,   // kYellow
    0xA45CFF,   // kPurple
    0xFF8A1A,   // kOrange
    0xFF5FB0,   // kPink
    0x2F7BFF,   // kBlue    xanh dương
    0x16D2E6,   // kCyan    xanh ngọc
};
static_assert(sizeof(kHueRgb) / sizeof(kHueRgb[0]) == static_cast<size_t>(Hue::kCount),
              "one RGB value per Hue");

struct Slot {
    int8_t x;
    int8_t y;
};

struct Layout {
    uint8_t count;
    uint8_t size;
    uint8_t radius;
    Slot slots[kMaxEyes];
};

// LAYOUTS -- offsets from the panel centre, {count, size, radius, {{x, y}...}}.
// tools/verify_green_eye_layout.py parses this table and checks every eye
// against the rim, the score above and the hearts below, and its neighbours.
// Keep one layout per line in this exact shape, or the checker cannot read it.
constexpr Layout kLayouts[] = {
    {2, 76, 24, {{-50, 0}, {50, 0}}},
    {2, 72, 22, {{-46, -22}, {46, 22}}},
    {2, 72, 22, {{-46, 22}, {46, -22}}},
    {3, 60, 18, {{0, -36}, {-56, 30}, {56, 30}}},
    {3, 58, 18, {{-56, -30}, {56, -30}, {0, 33}}},
    {3, 52, 16, {{-68, 0}, {0, 0}, {68, 0}}},
    {4, 54, 17, {{-46, -34}, {46, -34}, {-46, 34}, {46, 34}}},
    {4, 50, 16, {{0, -41}, {-62, 0}, {62, 0}, {0, 39}}},
};
constexpr int kLayoutCount = static_cast<int>(sizeof(kLayouts) / sizeof(kLayouts[0]));

State state;
uint32_t rng = 1;
int last_layout = -1;
int last_green_x = -1000;
int last_green_y = -1000;
bool recovery_next = false;

// xorshift32: the caller seeds it (esp_random() on the device, a fixed value in
// the host simulator), so a whole game can be replayed from its seed.
uint32_t NextRandom() {
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

uint32_t RandomBelow(uint32_t n) {
    return n == 0 ? 0 : NextRandom() % n;
}

void EnterPhase(Phase phase, uint32_t now_ms, uint32_t len_ms) {
    state.phase = phase;
    state.phase_start_ms = now_ms;
    state.phase_len_ms = len_ms;
}

uint32_t WindowFor(uint16_t score) {
    const uint32_t cut = kWindowStepMs * score;
    if (cut >= kStartWindowMs - kMinWindowMs) {
        return kMinWindowMs;
    }
    return kStartWindowMs - cut;
}

uint8_t EyeCountFor(uint16_t score) {
    if (score >= kFourEyesFromScore) {
        return 4;
    }
    if (score >= kThreeEyesFromScore) {
        return 3;
    }
    return 2;
}

bool IsXanh(Hue hue) {
    return hue == Hue::kBlue || hue == Hue::kCyan;
}

int ChooseLayout(uint8_t count) {
    int candidates[kLayoutCount];
    int n = 0;
    for (int i = 0; i < kLayoutCount; ++i) {
        if (kLayouts[i].count == count && i != last_layout) {
            candidates[n++] = i;
        }
    }
    if (n == 0) {
        // Only one layout of this size: repeating it is the only option.
        for (int i = 0; i < kLayoutCount; ++i) {
            if (kLayouts[i].count == count) {
                candidates[n++] = i;
            }
        }
    }
    return candidates[RandomBelow(static_cast<uint32_t>(n))];
}

void BeginRound(uint32_t now_ms) {
    const uint8_t count = EyeCountFor(state.score);
    const int layout_index = ChooseLayout(count);
    const Layout& layout = kLayouts[layout_index];
    last_layout = layout_index;

    // Where the green goes: anywhere but where the last one was.
    int places[kMaxEyes];
    int place_count = 0;
    for (int i = 0; i < layout.count; ++i) {
        const int x = kCenter + layout.slots[i].x;
        const int y = kCenter + layout.slots[i].y;
        if (std::abs(x - last_green_x) > kSamePlacePx || std::abs(y - last_green_y) > kSamePlacePx) {
            places[place_count++] = i;
        }
    }
    if (place_count == 0) {
        for (int i = 0; i < layout.count; ++i) {
            places[place_count++] = i;
        }
    }
    const int green = places[RandomBelow(static_cast<uint32_t>(place_count))];

    // Distractors: all different, drawn from the colours this score unlocks.
    Hue pool[static_cast<int>(Hue::kCount)];
    int pool_size = 0;
    pool[pool_size++] = Hue::kRed;
    pool[pool_size++] = Hue::kYellow;
    pool[pool_size++] = Hue::kPurple;
    if (state.score >= kBlueFromScore) {
        pool[pool_size++] = Hue::kOrange;
        pool[pool_size++] = Hue::kPink;
        pool[pool_size++] = Hue::kBlue;
    }
    if (state.score >= kCyanFromScore) {
        pool[pool_size++] = Hue::kCyan;
    }
    for (int i = pool_size - 1; i > 0; --i) {
        const int j = static_cast<int>(RandomBelow(static_cast<uint32_t>(i + 1)));
        std::swap(pool[i], pool[j]);
    }
    const int need = layout.count - 1;   // every pool is at least 3 colours

    // Make the xanh family show up often once it is unlocked -- a round of
    // green against red and yellow does not teach xanh lá from xanh dương.
    const uint32_t xanh_pct = state.score >= kCyanFromScore ? kXanhChanceCyanPct
                            : state.score >= kBlueFromScore ? kXanhChanceBluePct
                            : 0;
    if (xanh_pct > 0 && RandomBelow(100) < xanh_pct) {
        bool has_xanh = false;
        for (int i = 0; i < need; ++i) {
            has_xanh = has_xanh || IsXanh(pool[i]);
        }
        if (!has_xanh) {
            for (int i = need; i < pool_size; ++i) {
                if (IsXanh(pool[i])) {
                    std::swap(pool[static_cast<int>(RandomBelow(static_cast<uint32_t>(need)))], pool[i]);
                    break;
                }
            }
        }
    }

    state.eye_count = layout.count;
    int next_distractor = 0;
    for (int i = 0; i < kMaxEyes; ++i) {
        Eye& eye = state.eyes[i];
        if (i >= layout.count) {
            eye = Eye{};
            continue;
        }
        eye.x = static_cast<int16_t>(kCenter + layout.slots[i].x);
        eye.y = static_cast<int16_t>(kCenter + layout.slots[i].y);
        eye.size = layout.size;
        eye.radius = layout.radius;
        eye.hue = (i == green) ? Hue::kGreen : pool[next_distractor++];
    }
    state.green_index = static_cast<int8_t>(green);
    state.tapped_index = -1;
    state.outcome = Outcome::kNone;
    state.window_ms = WindowFor(state.score) + (recovery_next ? kRecoveryMs : 0);
    recovery_next = false;
    EnterPhase(Phase::kOpening, now_ms, kOpenMs);
}

void Decide(Outcome outcome, int tapped, uint32_t now_ms) {
    const uint32_t elapsed = now_ms - state.phase_start_ms;
    state.decided_permille = elapsed >= state.window_ms
        ? 0
        : static_cast<uint16_t>(1000 - (static_cast<uint64_t>(elapsed) * 1000ULL) / state.window_ms);
    state.outcome = outcome;
    state.tapped_index = static_cast<int8_t>(tapped);
    state.decisions++;

    if (outcome == Outcome::kHit) {
        state.score++;
        state.streak++;
        state.best_streak = std::max(state.best_streak, state.streak);
    } else {
        if (state.lives > 0) {
            state.lives--;
        }
        state.streak = 0;
        recovery_next = true;
    }

    const Eye& green = state.eyes[state.green_index];
    last_green_x = green.x;
    last_green_y = green.y;
    EnterPhase(Phase::kFeedback, now_ms,
               outcome == Outcome::kHit ? kHitFeedbackMs : kMissFeedbackMs);
}

}  // namespace

void Start(uint32_t now_ms, uint32_t seed) {
    state = State{};
    rng = seed != 0 ? seed : 0x9E3779B9u;
    last_layout = -1;
    last_green_x = -1000;
    last_green_y = -1000;
    recovery_next = false;
    state.lives = kLives;
    state.countdown = 3;
    EnterPhase(Phase::kCountdown, now_ms, 3 * kCountdownStepMs);
}

void Stop() {
    if (state.phase != Phase::kStopped) {
        EnterPhase(Phase::kStopped, state.phase_start_ms, 0);
    }
}

void Update(uint32_t now_ms) {
    const uint32_t elapsed = now_ms - state.phase_start_ms;
    switch (state.phase) {
        case Phase::kCountdown:
            if (elapsed >= 3 * kCountdownStepMs) {
                state.countdown = 0;
                BeginRound(now_ms);
            } else {
                state.countdown = static_cast<uint8_t>(3 - elapsed / kCountdownStepMs);
            }
            break;
        case Phase::kOpening:
            if (elapsed >= kOpenMs) {
                EnterPhase(Phase::kLooking, now_ms, 0);
            }
            break;
        case Phase::kLooking:
            if (elapsed >= state.window_ms + kLateGraceMs) {
                Decide(Outcome::kTimeout, -1, now_ms);
            }
            break;
        case Phase::kFeedback:
            if (elapsed >= state.phase_len_ms) {
                if (state.lives == 0) {
                    EnterPhase(Phase::kOver, now_ms, 0);
                } else {
                    EnterPhase(Phase::kClosing, now_ms, kCloseMs);
                }
            }
            break;
        case Phase::kClosing:
            if (elapsed >= kCloseMs) {
                BeginRound(now_ms);
            }
            break;
        case Phase::kStopped:
        case Phase::kOver:
            break;
    }
}

TapResult HandleTap(int x, int y, uint32_t now_ms) {
    // A round whose time ran out between two ticks is a timeout, not a target.
    Update(now_ms);
    if (state.phase != Phase::kLooking) {
        return TapResult::kIgnored;
    }
    const int index = EyeAt(x, y);
    if (index < 0) {
        return TapResult::kIgnored;   // empty space never costs a heart
    }
    if (index == state.green_index) {
        Decide(Outcome::kHit, index, now_ms);
        return TapResult::kHit;
    }
    Decide(Outcome::kWrong, index, now_ms);
    return TapResult::kWrong;
}

const State& GetState() {
    return state;
}

bool IsRunning() {
    return state.phase != Phase::kStopped && state.phase != Phase::kOver;
}

uint32_t PhaseElapsedMs(uint32_t now_ms) {
    return now_ms - state.phase_start_ms;
}

// Judged by distance to each eye's EDGE, not its centre. Nearest-centre is
// wrong for the triangle layouts: the corner of one eye can be closer to its
// neighbour's centre than to its own, and a tap squarely on an eye was scored
// against the one next to it (caught by tools/verify_green_eye_layout.py).
int EyeAt(int x, int y) {
    int best = -1;
    int best_edge2 = INT_MAX;
    int best_centre2 = INT_MAX;
    for (int i = 0; i < state.eye_count; ++i) {
        const Eye& eye = state.eyes[i];
        const int half = eye.size / 2;
        const int dx = x - eye.x;
        const int dy = y - eye.y;
        const int ox = std::max(0, std::abs(dx) - half);
        const int oy = std::max(0, std::abs(dy) - half);
        const int edge2 = ox * ox + oy * oy;
        if (edge2 > kTapSlackPx * kTapSlackPx) {
            continue;
        }
        const int centre2 = dx * dx + dy * dy;
        if (edge2 < best_edge2 || (edge2 == best_edge2 && centre2 < best_centre2)) {
            best_edge2 = edge2;
            best_centre2 = centre2;
            best = i;
        }
    }
    return best;
}

uint32_t HueRgb(Hue hue) {
    const int index = static_cast<int>(hue);
    if (index < 0 || index >= static_cast<int>(Hue::kCount)) {
        return 0xFFFFFF;
    }
    return kHueRgb[index];
}

int MoodReward(uint16_t score) {
    return std::min<int>(score, kMaxMoodReward);
}

}  // namespace GreenEyeGame
