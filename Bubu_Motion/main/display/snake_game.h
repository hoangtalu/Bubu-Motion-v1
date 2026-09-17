#pragma once

#include <cstdint>

// RẮN SĂN MỒI (Snake) -- the arcade classic, fitted to the round panel.
//
// A 15x15 board sized so its corners clear the bezel. The snake advances one
// cell per step, eats the pellet to grow one segment, and dies against a wall
// or against its own body. Steering comes from a swipe or from a tap on the
// side of the board you want to head towards; both funnel into Steer().
//
// Speed is chosen before the round AND the snake keeps accelerating as it
// grows, so the pick is a starting point rather than a ceiling. Records are
// per speed, so an easy grind cannot beat a fast run.
//
// Pure logic -- no LVGL here. The caller ticks Update() and draws the result.
namespace SnakeGame {

constexpr int kBoardCells = 15;
constexpr int kCellCount = kBoardCells * kBoardCells;   // 225, also the max length

enum class Speed : uint8_t {
    kSlow = 0,     // CHẬM
    kNormal,       // VỪA
    kFast,         // NHANH
    kSpeedCount,
};

enum class Direction : uint8_t { kUp = 0, kDown, kLeft, kRight };

enum class Result : uint8_t {
    kNone = 0,
    kHitWall,       // ĐỤNG VÁCH
    kHitSelf,       // CẮN ĐUÔI
    kFilledBoard,   // the board is full -- a win, not a loss
    kStopped,       // long-pressed out mid-round
};

struct Cell {
    int8_t x = 0;
    int8_t y = 0;
};

void Start(Speed speed);
void Stop();

// Advances the world when the step interval has elapsed. Returns true only if
// a step actually happened, so the caller repaints on a real change rather
// than on every tick.
bool Update();

// Queues a turn. Returns false when it is rejected: a 180° reversal, a repeat
// of the direction already pending, or a full queue. Safe to call at any rate
// -- the queue is what stops two quick taps inside one step from folding the
// snake back into itself, which is the classic way this game gets it wrong.
bool Steer(Direction direction);

bool IsRunning();
Speed GetSpeed();
Result GetResult();
Direction GetDirection();

uint16_t GetScore();          // pellets eaten
uint16_t GetLength();         // segments, head included
uint32_t StepIntervalMs();    // current interval, after the growth ramp

Cell GetPellet();
// index 0 is the head. Returns the head for any index past the tail.
Cell GetSegment(uint16_t index);

}  // namespace SnakeGame
