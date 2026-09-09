#pragma once

#include <cstdint>
#include "care_system.h"

namespace EyeGame {

struct Config {
    uint16_t max_rounds = 40;
    uint8_t reward_per_hit = 10;
    int8_t wrong_tap_mood_delta = -1;
    int8_t wrong_tap_energy_delta = -5;
};

enum class GameResult : uint8_t {
    kNone = 0,
    kFinishNormal,
    kFinishWrongTap,
};

enum class TapOutcome : uint8_t {
    kIgnored = 0,
    kCorrect,
    kWrong,
};

struct RgbColor {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
};

void Configure(const Config& config);
void Start(CareSystem::StatId reward_stat);
void Stop();
void Update();
TapOutcome HandleTap(int x, int y);

bool IsRunning();
uint8_t GetScore();
GameResult GetLastResult();
uint8_t GetRewardPerHit();
RgbColor GetLeftColor();
RgbColor GetRightColor();

}  // namespace EyeGame
