#include "eye_game.h"

#include <algorithm>
#include <array>

#include <esp_timer.h>
#include <esp_random.h>

namespace EyeGame {
namespace {

// A tap that lands just after the colours change is judged against what the
// child actually saw. Between the logic re-rolling a colour and that colour
// reaching the glass there is a 33ms game tick, a 33ms render frame and the LCD
// flush -- and a tap is aimed well before it lands. Without this, a green eye
// that turns red under a finger mid-tap ends the game as a wrong tap.
constexpr uint32_t kLateTapGraceMs = 250;

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
    // The pair shown before the last change, and when that change happened.
    // prev_round_open is false when the previous round is already settled --
    // it ended because it was hit, or there was none -- so a quick second tap
    // cannot claim the same green eye twice.
    EyeColorType prev_left_color = EyeColorType::kRed;
    EyeColorType prev_right_color = EyeColorType::kRed;
    uint32_t changed_ms = 0;
    bool prev_round_open = false;
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

// timed_out: the round ended because its time ran out rather than because it
// was hit, so a late tap may still claim it.
void ScheduleNextChange(bool timed_out) {
    state.prev_left_color = state.left_color;
    state.prev_right_color = state.right_color;
    state.prev_round_open = timed_out;
    state.changed_ms = NowMs();
    state.next_change_ms = state.changed_ms + RandomRange(1000, 2001);
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
    ScheduleNextChange(false);
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
        ScheduleNextChange(true);
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
        ScheduleNextChange(false);
        return TapOutcome::kCorrect;
    }

    // The eye was green until a moment ago: credit the round the child saw and
    // leave the new one running, rather than re-rolling colours on a hit the
    // screen no longer shows.
    const EyeColorType previous = is_left ? state.prev_left_color : state.prev_right_color;
    if (state.prev_round_open && previous == EyeColorType::kGreen &&
        NowMs() - state.changed_ms < kLateTapGraceMs) {
        state.score++;
        state.prev_round_open = false;
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
