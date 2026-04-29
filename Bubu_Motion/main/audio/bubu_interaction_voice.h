#pragma once

#include <cstdint>
#include <random>
#include <string_view>

enum class BubuInteractionEvent : uint8_t {
    EyeTap,
    Blink,
    Mischief,
};

class BubuInteractionVoice {
public:
    BubuInteractionVoice();

    std::string_view GetVoiceForEvent(BubuInteractionEvent event, uint64_t now_ms, bool allow_playback);

private:
    bool CooldownElapsed(uint64_t now_ms, uint64_t last_ms, uint64_t cooldown_ms) const;
    std::string_view GetOccasionalMumblingVoice();
    void MarkPlayed(BubuInteractionEvent event, uint64_t now_ms);

    std::mt19937 rng_;
    uint64_t last_blink_voice_ms_ = 0;
    uint64_t last_mischief_voice_ms_ = 0;
    uint64_t last_any_bubu_voice_ms_ = 0;

    static constexpr uint64_t kBlinkCooldownMs = 8000;
    static constexpr uint64_t kMischiefCooldownMs = 6000;
    static constexpr uint64_t kGlobalCooldownMs = 2000;
};
