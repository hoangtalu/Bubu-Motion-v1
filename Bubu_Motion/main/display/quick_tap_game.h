#pragma once

#include <cstdint>

// Quick Tap ("CHẠM NHANH") -- a 30-second reflex game for the round screen.
//
// A countdown ring drains around the bezel while a single dot appears at
// random points inside it. Tap the white dots before they expire. In the
// no-go mode red dots appear too, and tapping one ends the round on the spot.
//
// Scoring:  ĐIỂM = HIT + (CHUỖI dài nhất × HỆ SỐ) - TRẬT   (floored at 0)
//   HIT   -- white dots tapped, 1 point each, never multiplied
//   CHUỖI -- the longest unbroken run of hits in the round, times the
//            difficulty coefficient. This is the only term the multiplier
//            touches, and it is also the term a tight deadline destroys, so
//            harder difficulties raise the payout and attack it at once.
//   TRẬT  -- white dots that expired untapped, -1 point each
//
// Pure logic -- no LVGL here. The caller polls this each frame and draws it.
namespace QuickTapGame {

enum class Mode : uint8_t {
    kSimple = 0,   // CƠ BẢN   -- white dots only
    kNoGo,         // TRÁNH ĐỎ -- red dots end the round
    kModeCount,
};

enum class Difficulty : uint8_t {
    kEasy = 0,     // DỄ
    kMedium,       // VỪA
    kHard,         // KHÓ
    kDifficultyCount,
};

enum class Result : uint8_t {
    kNone = 0,
    kTimeUp,       // HẾT GIỜ!
    kTappedRed,    // CHẠM NHẦM!
    kStopped,      // long-pressed out mid-round
};

enum class TapOutcome : uint8_t {
    kIgnored = 0,  // empty space -- costs nothing
    kHit,
    kTappedRed,
};

// What a difficulty changes. Dot size is deliberately NOT in here: the dot is
// the same size at every difficulty, so the deadline is the only thing that
// gets harder -- which is exactly what the streak multiplier pays for.
struct Profile {
    uint16_t dot_lifetime_ms;
    uint8_t  red_chance_pct;     // ignored in kSimple
    uint8_t  streak_multiplier;  // HỆ SỐ
};

struct Dot {
    int16_t x = 0;          // screen coordinates, 240x240 panel
    int16_t y = 0;
    bool visible = false;
    bool red = false;
};

const Profile& GetProfile(Difficulty difficulty);

void Start(Mode mode, Difficulty difficulty);
void Stop();
void Update();
TapOutcome HandleTap(int x, int y);

bool IsRunning();
Mode GetMode();
Difficulty GetDifficulty();
Result GetResult();

uint32_t RoundMs();
uint32_t RemainingMs();

Dot GetDot();
int GetDotRadius();

uint16_t GetHits();
uint16_t GetMisses();
uint16_t GetRedAvoided();
uint16_t GetLongestStreak();
int GetStreakPoints();   // longest streak * HỆ SỐ
int GetScore();

}  // namespace QuickTapGame
