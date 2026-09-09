#include "eye_game.h"

#include <algorithm>
#include <array>

#include <esp_timer.h>
#include <esp_random.h>

namespace EyeGame {
namespace {

enum class EyeColorType : uint8_t {
    kGreen = 0,
    kRed,
    kBlue,
    kPurple,
    kYellow,
    kCyan,
};

struct State {
    bool running = false;
    CareSystem::StatId reward_stat = CareSystem::STAT_MOOD;
    Config config = {};
    EyeColorType left_color = EyeColorType::kRed;
    EyeColorType right_color = EyeColorType::kBlue;
    RgbColor left_rgb = {255, 255, 255};
    RgbColor right_rgb = {255, 255, 255};
    uint32_t next_change_ms = 0;
    uint16_t rounds = 0;
    uint8_t score = 0;
    GameResult last_result = GameResult::kNone;
};

State state;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t RandomRange(uint32_t min_inclusive, uint32_t max_exclusive) {
    if (max_exclusive <= min_inclusive) {
        return min_inclusive;
    }
    return min_inclusive + (esp_random() % (max_exclusive - min_inclusive));
}

RgbColor ColorFromType(EyeColorType type) {
    switch (type) {
        case EyeColorType::kGreen: return {0, 255, 0};
        case EyeColorType::kRed: return {255, 0, 0};
        case EyeColorType::kBlue: return {0, 0, 255};
        case EyeColorType::kPurple: return {128, 0, 255};
        case EyeColorType::kYellow: return {255, 255, 0};
        case EyeColorType::kCyan: return {0, 255, 255};
        default: return {255, 255, 255};
    }
}

EyeColorType RandomColorType() {
    const uint32_t roll = RandomRange(0, 100);
    if (roll < 25) {
        return EyeColorType::kGreen;
    }

    static constexpr std::array<EyeColorType, 5> kPool = {
        EyeColorType::kRed,
        EyeColorType::kBlue,
        EyeColorType::kPurple,
        EyeColorType::kYellow,
        EyeColorType::kCyan,
    };
    return kPool[RandomRange(0, static_cast<uint32_t>(kPool.size()))];
}

void RefreshColors() {
    state.left_rgb = ColorFromType(state.left_color);
    state.right_rgb = ColorFromType(state.right_color);
}

void ApplyReward() {
    const int reward = static_cast<int>(state.score) * static_cast<int>(state.config.reward_per_hit);
    if (reward == 0) {
        return;
    }

    switch (state.reward_stat) {
        case CareSystem::STAT_HUNGER:
            CareSystem::AddHunger(reward);
            break;
        case CareSystem::STAT_MOOD:
            CareSystem::AddMood(reward);
            break;
        case CareSystem::STAT_ENERGY:
            CareSystem::AddEnergy(reward);
            break;
        case CareSystem::STAT_CLEANLINESS:
            CareSystem::AddCleanliness(reward);
            break;
        default:
            break;
    }
}

void Finish(GameResult result) {
    state.running = false;
    state.last_result = result;
    if (result == GameResult::kFinishNormal) {
        ApplyReward();
    }
}

void ApplyWrongTapPenalty() {
    CareSystem::AddMood(static_cast<int>(state.config.wrong_tap_mood_delta));
    CareSystem::AddEnergy(static_cast<int>(state.config.wrong_tap_energy_delta));
}

void ScheduleNextChange() {
    state.next_change_ms = NowMs() + RandomRange(1000, 2001);
    state.left_color = RandomColorType();
    state.right_color = RandomColorType();
    RefreshColors();
    state.rounds++;
    if (state.rounds >= state.config.max_rounds) {
        Finish(GameResult::kFinishNormal);
    }
}

}  // namespace

void Configure(const Config& config) {
    state.config = config;
}

void Start(CareSystem::StatId reward_stat) {
    state.running = true;
    state.reward_stat = reward_stat;
    state.rounds = 0;
    state.score = 0;
    state.last_result = GameResult::kNone;
    ScheduleNextChange();
}

void Stop() {
    if (!state.running) {
        return;
    }
    ApplyReward();
    state.running = false;
    state.last_result = GameResult::kFinishNormal;
}

void Update() {
    if (!state.running) {
        return;
    }
    if (NowMs() >= state.next_change_ms) {
        ScheduleNextChange();
    }
}

TapOutcome HandleTap(int x, int y) {
    (void)y;
    if (!state.running) {
        return TapOutcome::kIgnored;
    }

    const bool is_left = x < 120;
    const EyeColorType tapped_color = is_left ? state.left_color : state.right_color;
    if (tapped_color == EyeColorType::kGreen) {
        state.score++;
        ScheduleNextChange();
        return TapOutcome::kCorrect;
    }

    ApplyReward();
    ApplyWrongTapPenalty();
    Finish(GameResult::kFinishWrongTap);
    return TapOutcome::kWrong;
}

bool IsRunning() {
    return state.running;
}

uint8_t GetScore() {
    return state.score;
}

GameResult GetLastResult() {
    return state.last_result;
}

uint8_t GetRewardPerHit() {
    return state.config.reward_per_hit;
}

RgbColor GetLeftColor() {
    return state.left_rgb;
}

RgbColor GetRightColor() {
    return state.right_rgb;
}

}  // namespace EyeGame
