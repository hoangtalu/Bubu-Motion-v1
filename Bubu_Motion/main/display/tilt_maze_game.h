#pragma once

#include <cstddef>
#include <cstdint>

namespace TiltMazeGame {

constexpr uint16_t kMaxProgressLevels = 200;
constexpr uint8_t kImplementedLevels = 34;
constexpr int kCellSize = 42;
constexpr int kBallRadius = 7;

enum class Phase : uint8_t {
    kStopped,
    kCalibrating,
    kCountdown,
    kPlaying,
    kPlanning,
    kComplete,
};

enum class PanDirection : uint8_t { kUp, kDown, kLeft, kRight };

struct Cell {
    uint8_t col;
    uint8_t row;
};

struct WormholePair {
    Cell a;
    Cell b;
    int8_t a_offset_x;
    int8_t a_offset_y;
    int8_t b_offset_x;
    int8_t b_offset_y;
    uint8_t style;
};

struct BoostPad {
    Cell cell;
    int8_t direction_x;
    int8_t direction_y;
};

struct LevelDefinition {
    uint8_t width;
    uint8_t height;
    const char* const* rows;
    Cell start;
    Cell goal;
    const Cell* gold;
    uint8_t gold_count;
    const WormholePair* wormholes;
    uint8_t wormhole_count;
    const BoostPad* boosts;
    uint8_t boost_count;
    uint16_t target_seconds;
};

struct State {
    Phase phase = Phase::kStopped;
    uint8_t level_index = 0;
    float ball_x = 0.0f;
    float ball_y = 0.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    uint32_t elapsed_ms = 0;
    uint32_t countdown_remaining_ms = 0;
    uint16_t gold_collected_mask = 0;
    uint16_t broken_wall_mask = 0;
    uint8_t last_run_stars = 0;
    bool neutral_stable = false;
    bool boost_active = false;
    uint8_t boost_style = 0;
};

// Loads the compact, versioned 200-level progress record from NVS. Safe to
// call repeatedly; only the first call touches flash.
void Initialize();

const LevelDefinition& GetLevel(uint8_t index);
const State& GetState();

uint8_t GetCurrentLevel();
uint8_t GetHighestUnlockedLevel();
uint8_t GetSavedStars(uint16_t level_index);
uint32_t GetBestTimeMs(uint16_t level_index);

void StartLevel(uint8_t index, uint32_t now_ms);
void Stop();

// FeedAccel only stores/filter samples. It is intended to be called on the
// application task after the board's IMU timer schedules the reading there.
void FeedAccel(float ax, float ay, float az, uint32_t now_ms);
bool ConfirmNeutral(uint32_t now_ms);
void Tick(uint32_t now_ms);

bool EnterPlanning();
void PanPlanning(PanDirection direction);
bool ResumeFromPlanning(uint32_t now_ms);

bool IsGoldCollected(uint8_t index);
bool IsBreakableBroken(uint8_t col, uint8_t row);
uint8_t CountCollectedGold();

}  // namespace TiltMazeGame
