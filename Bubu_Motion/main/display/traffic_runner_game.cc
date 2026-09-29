#include "traffic_runner_game.h"

#include "settings.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <esp_log.h>
#include <esp_random.h>

namespace TrafficRunnerGame {
namespace {

constexpr char kTag[] = "TrafficRunner";
constexpr char kSettingsNamespace[] = "trafficrun";
constexpr char kHighScoreKey[] = "highscore";

constexpr uint32_t kCountdownMs = 1200;
constexpr uint8_t kNeutralSamples = 16;
constexpr float kNeutralMotionLimitG = 0.08f;
constexpr float kLaneTiltThresholdG = 0.24f;
constexpr float kLaneRearmThresholdG = 0.12f;
constexpr float kInputSign = -1.0f;
constexpr float kPlayerY = 194.0f;
constexpr float kPlayerRadius = 11.0f;
constexpr float kBaseSpeed = 68.0f;
constexpr float kMaxSpeed = 132.0f;
constexpr float kBoostSpeedMultiplier = 1.18f;
constexpr uint32_t kDifficultyRampMs = 75000;
constexpr uint32_t kMaxScore = 999999;

State state;
std::array<Entity, kMaxEntities> entities;
bool initialized = false;
bool lane_input_armed = true;
uint8_t neutral_sample_count = 0;
float neutral_sum_x = 0.0f;
float neutral_x = 0.0f;
float filtered_x = 0.0f;
float previous_calibration_x = 0.0f;
bool have_accel_sample = false;
uint32_t last_tick_ms = 0;
uint32_t play_started_ms = 0;
uint32_t countdown_started_ms = 0;
uint32_t next_wave_ms = 0;
uint32_t next_boost_ms = 0;
float score_fraction = 0.0f;
int8_t planned_safe_lane = 1;
bool previous_wave_was_gate = false;
uint32_t spawned_wave_count = 0;

uint32_t RandomRange(uint32_t minimum, uint32_t maximum) {
    if (maximum <= minimum) {
        return minimum;
    }
    return minimum + (esp_random() % (maximum - minimum + 1));
}

bool TimeReached(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

void ClearEntities() {
    for (auto& entity : entities) {
        entity = Entity{};
    }
}

Entity* AllocateEntity() {
    for (auto& entity : entities) {
        if (!entity.active) {
            entity = Entity{};
            entity.active = true;
            return &entity;
        }
    }
    return nullptr;
}

void AddScore(uint32_t value) {
    state.score = std::min(kMaxScore, state.score + value);
}

void FinishRun() {
    state.phase = Phase::kGameOver;
    state.boost_remaining_ms = 0;
    state.new_high_score = state.score > state.high_score;
    if (!state.new_high_score) {
        return;
    }

    state.high_score = state.score;
    Settings settings(kSettingsNamespace, true);
    const esp_err_t result = settings.SetInt(kHighScoreKey, static_cast<int32_t>(state.high_score));
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Could not save high score: %s", esp_err_to_name(result));
    }
}

float ObstacleHalfHeight(const Entity& entity) {
    return 10.0f + (static_cast<float>(entity.length) * 7.0f);
}

uint32_t CurrentWaveIntervalMs() {
    // Keep enough world-space distance between waves even at maximum speed.
    // This prevents the trailing edge of one obstacle from visually merging
    // into the next decision point.
    constexpr std::array<int32_t, 5> kBaseIntervalsMs = {
        1380, 1300, 1220, 1140, 1080,
    };
    const int32_t base = kBaseIntervalsMs[std::min<size_t>(state.difficulty_tier, 4)];
    const int32_t jitter = static_cast<int32_t>(RandomRange(0, 160)) - 80;
    return static_cast<uint32_t>(std::max<int32_t>(1000, base + jitter));
}

void SpawnObstacle(int8_t lane, float y, uint8_t length) {
    Entity* entity = AllocateEntity();
    if (entity == nullptr) {
        return;
    }
    entity->type = EntityType::kObstacle;
    entity->lane = lane;
    entity->y = y;
    entity->length = std::clamp<uint8_t>(length, 1, 3);
    entity->style = static_cast<uint8_t>(RandomRange(0, 3));
}

void SpawnReward(EntityType type, int8_t lane, float y) {
    Entity* entity = AllocateEntity();
    if (entity == nullptr) {
        return;
    }
    entity->type = type;
    entity->lane = lane;
    entity->y = y;
    entity->length = 1;
    if (type == EntityType::kBoost) {
        entity->boost_duration_ms = static_cast<uint16_t>(RandomRange(3000, 5000));
    }
}

void SpawnWave(uint32_t now_ms) {
    const float spawn_y = -38.0f;
    constexpr std::array<uint8_t, 5> kGateChance = {0, 12, 18, 24, 30};
    const bool gate_allowed = state.elapsed_ms >= 8000 && !previous_wave_was_gate;
    const bool double_obstacle = gate_allowed &&
        RandomRange(0, 99) < kGateChance[std::min<size_t>(state.difficulty_tier, 4)];

    std::array<int8_t, 2> open_lanes = {-1, -1};
    uint8_t open_count = 0;
    if (double_obstacle) {
        // A closed two-lane gate may move its opening by only one lane. Since
        // IMU input needs to return to neutral between lane changes, jumping
        // directly from lane 0 to lane 2 would be unfair.
        const int step = static_cast<int>(RandomRange(0, 2)) - 1;
        planned_safe_lane = static_cast<int8_t>(std::clamp<int>(
            static_cast<int>(planned_safe_lane) + step, 0, 2));
        const uint8_t gate_length = static_cast<uint8_t>(RandomRange(1, 2));
        for (int8_t lane = 0; lane < 3; ++lane) {
            if (lane != planned_safe_lane) {
                SpawnObstacle(lane, spawn_y, gate_length);
            }
        }
        open_lanes[open_count++] = planned_safe_lane;
    } else {
        // Single-obstacle waves either preserve the current safe corridor or
        // gently move it by one lane. They never block the planned corridor
        // without first providing an adjacent escape.
        const int8_t previous_safe_lane = planned_safe_lane;
        const bool shift_corridor = spawned_wave_count >= 3 &&
            state.difficulty_tier > 0 && RandomRange(0, 99) < 34;
        int8_t obstacle_lane = -1;
        if (shift_corridor) {
            std::array<int8_t, 2> adjacent = {-1, -1};
            uint8_t adjacent_count = 0;
            if (previous_safe_lane > 0) {
                adjacent[adjacent_count++] = previous_safe_lane - 1;
            }
            if (previous_safe_lane < 2) {
                adjacent[adjacent_count++] = previous_safe_lane + 1;
            }
            planned_safe_lane = adjacent[RandomRange(0, adjacent_count - 1)];
            obstacle_lane = previous_safe_lane;
        } else {
            std::array<int8_t, 2> candidates = {-1, -1};
            uint8_t candidate_count = 0;
            for (int8_t lane = 0; lane < 3; ++lane) {
                if (lane != planned_safe_lane) {
                    candidates[candidate_count++] = lane;
                }
            }
            obstacle_lane = candidates[RandomRange(0, candidate_count - 1)];
        }

        SpawnObstacle(obstacle_lane, spawn_y,
                      static_cast<uint8_t>(RandomRange(1, 3)));
        for (int8_t lane = 0; lane < 3; ++lane) {
            if (lane != obstacle_lane) {
                open_lanes[open_count++] = lane;
            }
        }
    }

    if (open_count > 0) {
        if (TimeReached(now_ms, next_boost_ms)) {
            // Boost appears in the planned corridor so it never lures the
            // player into a dead end.
            SpawnReward(EntityType::kBoost, planned_safe_lane, spawn_y - 8.0f);
            next_boost_ms = now_ms + RandomRange(7000, 12000);
        } else if (!double_obstacle && RandomRange(0, 99) < 62) {
            // Coins prefer the other open lane, creating an optional risk vs.
            // reward choice without making the safe route ambiguous.
            int8_t reward_lane = planned_safe_lane;
            for (uint8_t index = 0; index < open_count; ++index) {
                if (open_lanes[index] != planned_safe_lane) {
                    reward_lane = open_lanes[index];
                    break;
                }
            }
            SpawnReward(EntityType::kCoin, reward_lane, spawn_y - 8.0f);
        }
    }

    previous_wave_was_gate = double_obstacle;
    ++spawned_wave_count;
    next_wave_ms = now_ms + CurrentWaveIntervalMs();
}

void BeginPlaying(uint32_t now_ms) {
    state.phase = Phase::kPlaying;
    state.countdown_remaining_ms = 0;
    play_started_ms = now_ms;
    last_tick_ms = now_ms;
    next_wave_ms = now_ms + 420;
    next_boost_ms = now_ms + RandomRange(5000, 8500);
}

void UpdateDifficulty() {
    if (state.elapsed_ms >= 60000) {
        state.difficulty_tier = 4;
    } else if (state.elapsed_ms >= 35000) {
        state.difficulty_tier = 3;
    } else if (state.elapsed_ms >= 15000) {
        state.difficulty_tier = 2;
    } else if (state.elapsed_ms >= 6000) {
        state.difficulty_tier = 1;
    } else {
        state.difficulty_tier = 0;
    }
}

void UpdateEntities(float distance, uint32_t delta_ms) {
    for (auto& entity : entities) {
        if (!entity.active) {
            continue;
        }

        entity.y += distance;
        if (entity.lane != state.player_lane) {
            if (entity.y > 270.0f) {
                entity.active = false;
            }
            continue;
        }

        const float collision_distance = (entity.type == EntityType::kObstacle)
            ? (ObstacleHalfHeight(entity) + kPlayerRadius)
            : 18.0f;
        if (std::fabs(entity.y - kPlayerY) <= collision_distance) {
            if (entity.type == EntityType::kCoin) {
                state.gold = static_cast<uint16_t>(std::min<uint32_t>(999, state.gold + 1));
                AddScore(IsBoostActive() ? 20 : 10);
                entity.active = false;
            } else if (entity.type == EntityType::kBoost) {
                state.boost_remaining_ms = std::max<uint32_t>(state.boost_remaining_ms,
                                                               entity.boost_duration_ms);
                entity.active = false;
            } else if (IsBoostActive()) {
                AddScore(15);
                entity.active = false;
            } else {
                FinishRun();
                return;
            }
        } else if (entity.y > 270.0f) {
            entity.active = false;
        }
    }

    if (state.boost_remaining_ms > 0) {
        state.boost_remaining_ms = (delta_ms >= state.boost_remaining_ms)
            ? 0
            : state.boost_remaining_ms - delta_ms;
    }
}

}  // namespace

void Initialize() {
    if (initialized) {
        return;
    }
    Settings settings(kSettingsNamespace, false);
    state.high_score = static_cast<uint32_t>(std::clamp<int32_t>(
        settings.GetInt(kHighScoreKey, 0), 0, static_cast<int32_t>(kMaxScore)));
    initialized = true;
}

void Start(uint32_t now_ms) {
    Initialize();
    const uint32_t saved_high_score = state.high_score;
    state = State{};
    state.phase = Phase::kCalibrating;
    state.player_lane = 1;
    state.high_score = saved_high_score;
    ClearEntities();
    lane_input_armed = true;
    neutral_sample_count = 0;
    neutral_sum_x = 0.0f;
    neutral_x = 0.0f;
    filtered_x = 0.0f;
    previous_calibration_x = 0.0f;
    have_accel_sample = false;
    last_tick_ms = now_ms;
    score_fraction = 0.0f;
    planned_safe_lane = 1;
    previous_wave_was_gate = false;
    spawned_wave_count = 0;
}

void Stop() {
    state.phase = Phase::kStopped;
    state.boost_remaining_ms = 0;
    ClearEntities();
}

void FeedAccel(float ax, float, float, uint32_t now_ms) {
    if (state.phase == Phase::kStopped || state.phase == Phase::kGameOver) {
        return;
    }

    if (!have_accel_sample) {
        filtered_x = ax;
        previous_calibration_x = ax;
        have_accel_sample = true;
    } else {
        filtered_x = filtered_x * 0.78f + ax * 0.22f;
    }

    if (state.phase == Phase::kCalibrating) {
        if (std::fabs(filtered_x - previous_calibration_x) <= kNeutralMotionLimitG) {
            neutral_sum_x += filtered_x;
            ++neutral_sample_count;
        } else {
            neutral_sample_count = 0;
            neutral_sum_x = 0.0f;
        }
        previous_calibration_x = filtered_x;

        if (neutral_sample_count >= kNeutralSamples) {
            neutral_x = neutral_sum_x / static_cast<float>(neutral_sample_count);
            state.neutral_stable = true;
            state.phase = Phase::kCountdown;
            state.countdown_remaining_ms = kCountdownMs;
            countdown_started_ms = now_ms;
        }
        return;
    }

    if (state.phase != Phase::kPlaying) {
        return;
    }

    const float tilt = (filtered_x - neutral_x) * kInputSign;
    if (std::fabs(tilt) <= kLaneRearmThresholdG) {
        lane_input_armed = true;
        return;
    }
    if (!lane_input_armed) {
        return;
    }

    if (tilt >= kLaneTiltThresholdG && state.player_lane < 2) {
        ++state.player_lane;
        lane_input_armed = false;
    } else if (tilt <= -kLaneTiltThresholdG && state.player_lane > 0) {
        --state.player_lane;
        lane_input_armed = false;
    }
}

void Tick(uint32_t now_ms) {
    if (state.phase == Phase::kStopped || state.phase == Phase::kGameOver ||
        state.phase == Phase::kCalibrating) {
        last_tick_ms = now_ms;
        return;
    }

    if (state.phase == Phase::kCountdown) {
        const uint32_t elapsed = now_ms - countdown_started_ms;
        if (elapsed >= kCountdownMs) {
            BeginPlaying(now_ms);
        } else {
            state.countdown_remaining_ms = kCountdownMs - elapsed;
        }
        return;
    }

    const uint32_t raw_delta_ms = now_ms - last_tick_ms;
    last_tick_ms = now_ms;
    const uint32_t delta_ms = std::min<uint32_t>(raw_delta_ms, 100);
    state.elapsed_ms = now_ms - play_started_ms;
    UpdateDifficulty();

    const float ramp = std::min(1.0f, static_cast<float>(state.elapsed_ms) /
        static_cast<float>(kDifficultyRampMs));
    float speed = kBaseSpeed + (kMaxSpeed - kBaseSpeed) * ramp;
    if (IsBoostActive()) {
        speed *= kBoostSpeedMultiplier;
    }

    score_fraction += (IsBoostActive() ? 20.0f : 10.0f) *
        (static_cast<float>(delta_ms) / 1000.0f);
    const uint32_t whole_score = static_cast<uint32_t>(score_fraction);
    if (whole_score > 0) {
        AddScore(whole_score);
        score_fraction -= static_cast<float>(whole_score);
    }

    const float distance = speed * (static_cast<float>(delta_ms) / 1000.0f);
    state.road_scroll_px = std::fmod(state.road_scroll_px + distance, 32.0f);
    UpdateEntities(distance, delta_ms);
    if (state.phase != Phase::kPlaying) {
        return;
    }
    if (TimeReached(now_ms, next_wave_ms)) {
        SpawnWave(now_ms);
    }
}

const State& GetState() {
    return state;
}

const Entity* GetEntities() {
    return entities.data();
}

size_t GetEntityCount() {
    return entities.size();
}

uint32_t GetHighScore() {
    Initialize();
    return state.high_score;
}

bool IsBoostActive() {
    return state.boost_remaining_ms > 0;
}

}  // namespace TrafficRunnerGame
