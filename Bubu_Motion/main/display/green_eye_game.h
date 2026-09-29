#pragma once

#include <cstdint>

// MẮT XANH -- a colour-reflex game for the round screen.
//
// Every round Bubu opens two to four eyes, each a different colour, and exactly
// one of them is leaf green (xanh lá). Tap it before the rim runs out. Tapping
// another colour, or letting the round run out, costs one of three hearts.
//
// The difficulty follows the score, never a menu:
//   - the look window shrinks with every green found (2.6 s down to 0.85 s),
//     and the round after a mistake is given a little extra time back;
//   - more eyes open: 2, then 3 from 4 points, then 4 from 10 points;
//   - trickier colours join: xanh dương (blue) from 6 points, xanh ngọc
//     (cyan) from 14. In Vietnamese both are "xanh" too, so telling xanh lá
//     from them is the actual skill the game trains, not a side effect.
//
// ĐIỂM = green eyes found. That is also what the game pays Bubu: one point of
// CẢM XÚC (CareSystem mood) per green, capped per game -- see MoodReward().
//
// Pure logic -- no LVGL, no ESP-IDF. The caller supplies the clock and the
// random seed, ticks Update() every frame and draws what GetState() reports,
// which is also what lets tools/green_eye_sim.cc run it on the host.
namespace GreenEyeGame {

constexpr int kMaxEyes = 4;
constexpr int kLives = 3;
constexpr int kMaxMoodReward = 25;

enum class Hue : uint8_t {
    kGreen = 0,   // the target, and the only one
    kRed,
    kYellow,
    kPurple,
    kOrange,
    kPink,
    kBlue,        // xanh dương -- from kBlueFromScore
    kCyan,        // xanh ngọc  -- from kCyanFromScore
    kCount,
};

enum class Phase : uint8_t {
    kStopped = 0,
    kCountdown,   // 3-2-1 before the first round
    kOpening,     // eyes opening; taps are ignored
    kLooking,     // eyes open, clock running, taps are judged
    kFeedback,    // the round is decided and its outcome is on screen
    kClosing,     // eyes shutting before the next round
    kOver,        // hearts gone; the caller shows the scoreboard
};

enum class Outcome : uint8_t {
    kNone = 0,
    kHit,         // tapped the green eye
    kWrong,       // tapped another colour
    kTimeout,     // the round ran out
};

enum class TapResult : uint8_t {
    kIgnored = 0, // not on an eye, or not a judged phase -- costs nothing
    kHit,
    kWrong,
};

struct Eye {
    int16_t x = 0;       // centre, screen coordinates on the 240x240 panel
    int16_t y = 0;
    uint8_t size = 0;    // side of the rounded square
    uint8_t radius = 0;  // its corner radius
    Hue hue = Hue::kRed;
};

struct State {
    Phase phase = Phase::kStopped;
    uint32_t phase_start_ms = 0;
    uint32_t phase_len_ms = 0;     // 0 for the open-ended phases

    uint8_t eye_count = 0;
    Eye eyes[kMaxEyes] = {};
    int8_t green_index = -1;
    int8_t tapped_index = -1;      // the eye a decided round was tapped on, else -1
    Outcome outcome = Outcome::kNone;

    uint32_t window_ms = 0;        // this round's look window (what the rim shows)
    uint16_t decided_permille = 0; // rim left when the round was decided
    uint16_t decisions = 0;        // bumps once per decided round -- an event id

    uint8_t lives = kLives;
    uint16_t score = 0;
    uint16_t streak = 0;
    uint16_t best_streak = 0;
    uint8_t countdown = 0;         // 3, 2, 1 while kCountdown
};

void Start(uint32_t now_ms, uint32_t seed);
void Stop();
void Update(uint32_t now_ms);
TapResult HandleTap(int x, int y, uint32_t now_ms);

const State& GetState();
// kCountdown through kClosing. kOver is not running: the round loop is done
// and only the scoreboard is left.
bool IsRunning();
uint32_t PhaseElapsedMs(uint32_t now_ms);

// Index of the open eye under a screen point, with the finger slack applied,
// or -1. Only meaningful while eyes are on screen.
int EyeAt(int x, int y);

uint32_t HueRgb(Hue hue);
int MoodReward(uint16_t score);

}  // namespace GreenEyeGame
