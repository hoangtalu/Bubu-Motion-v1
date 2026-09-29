#pragma once

#include <cstdint>
#include <random>
#include <string_view>

#include "vox.h"

enum class BubuInteractionEvent : uint8_t {
    EyeTap,
    Blink,
    Mischief,
};

class BubuInteractionVoice {
public:
    BubuInteractionVoice();

    // For Mischief, `mood` is the feeling the eye pose is showing (see
    // EyeAnimation::GetMischiefMood); the clip is chosen to match it.
    std::string_view GetVoiceForEvent(BubuInteractionEvent event, uint64_t now_ms, bool allow_playback,
                                      Vox::Mood mood = Vox::Mood::Mumble);

    // Playback duration of a clip previously returned by GetVoiceForEvent, or
    // 0 if unknown. Lets a caller (e.g. the mischief pose) hold its visual
    // state at least as long as the voice it triggered actually plays.
    uint32_t GetVoiceDurationMs(std::string_view voice) const;

private:
    bool CooldownElapsed(uint64_t now_ms, uint64_t last_ms, uint64_t cooldown_ms) const;
    std::string_view GetVoiceForMood(Vox::Mood mood);
    void MarkPlayed(BubuInteractionEvent event, uint64_t now_ms);

    std::mt19937 rng_;
    uint64_t last_mischief_voice_ms_ = 0;
    uint64_t last_any_bubu_voice_ms_ = 0;

    static constexpr uint64_t kMischiefCooldownMs = 6000;
    static constexpr uint64_t kGlobalCooldownMs = 2000;
};
