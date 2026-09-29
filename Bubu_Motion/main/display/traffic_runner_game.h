#pragma once

#include <cstddef>
#include <cstdint>

namespace TrafficRunnerGame {

constexpr size_t kMaxEntities = 18;

enum class Phase : uint8_t {
    kStopped,
    kCalibrating,
    kCountdown,
    kPlaying,
    kGameOver,
};

enum class EntityType : uint8_t {
    kObstacle,
    kCoin,
    kBoost,
};

struct Entity {
    EntityType type = EntityType::kObstacle;
    int8_t lane = 1;
    float y = -30.0f;
    uint8_t length = 1;
    uint8_t style = 0;
    uint16_t boost_duration_ms = 0;
    bool active = false;
};

struct State {
    Phase phase = Phase::kStopped;
    int8_t player_lane = 1;
    uint32_t score = 0;
    uint32_t high_score = 0;
    uint32_t elapsed_ms = 0;
    uint32_t countdown_remaining_ms = 0;
    uint32_t boost_remaining_ms = 0;
    uint16_t gold = 0;
    uint8_t difficulty_tier = 0;
    float road_scroll_px = 0.0f;
    bool neutral_stable = false;
    bool new_high_score = false;
};

// Loads the persisted high score. Repeated calls do not touch flash again.
void Initialize();

void Start(uint32_t now_ms);
void Stop();
void Tick(uint32_t now_ms);

// Called on the application task with samples forwarded by the board IMU timer.
void FeedAccel(float ax, float ay, float az, uint32_t now_ms);

const State& GetState();
const Entity* GetEntities();
size_t GetEntityCount();
uint32_t GetHighScore();
bool IsBoostActive();

}  // namespace TrafficRunnerGame
