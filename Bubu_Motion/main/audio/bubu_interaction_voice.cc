#include "bubu_interaction_voice.h"

#include "assets/lang_config.h"
#include <array>
#include <chrono>

namespace {

struct VoiceDuration {
    std::string_view sound;
    uint32_t duration_ms;
};

// Measured clip lengths (ffprobe) for the mischief mumbling/singing pool -
// the only voices GetVoiceForEvent can return that are long enough to
// outlast the mischief pose's own random hold (see GetOccasionalMumblingVoice
// and MischiefConfig::min_stay_ms/max_stay_ms).
const std::array<VoiceDuration, 8>& GetVoiceDurationTable() {
    static const std::array<VoiceDuration, 8> table = {{
        {Lang::Sounds::OGG_BUBU_MUMBLING_1, 4761},
        {Lang::Sounds::OGG_BUBU_MUMBLING_2, 5571},
        {Lang::Sounds::OGG_BUBU_MUMBLING_3, 4996},
        {Lang::Sounds::OGG_BUBU_MUMBLING_4, 5571},
        {Lang::Sounds::OGG_BUBU_SING1, 3246},
        {Lang::Sounds::OGG_BUBU_SING2, 2697},
        {Lang::Sounds::OGG_BUBU_SING3, 15314},
        {Lang::Sounds::OGG_BUBU_SING4, 17091},
    }};
    return table;
}

}  // namespace

BubuInteractionVoice::BubuInteractionVoice() {
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rng_.seed(seed);
}

bool BubuInteractionVoice::CooldownElapsed(uint64_t now_ms, uint64_t last_ms, uint64_t cooldown_ms) const {
    return last_ms == 0 || now_ms - last_ms >= cooldown_ms;
}

std::string_view BubuInteractionVoice::GetOccasionalMumblingVoice() {
    // Keep mumbling dominant and let the singing variants surface occasionally.
    if (std::uniform_int_distribution<int>(0, 4)(rng_) == 0) {
        switch (std::uniform_int_distribution<int>(0, 3)(rng_)) {
            case 0:
                return Lang::Sounds::OGG_BUBU_SING1;
            case 1:
                return Lang::Sounds::OGG_BUBU_SING2;
            case 2:
                return Lang::Sounds::OGG_BUBU_SING3;
            default:
                return Lang::Sounds::OGG_BUBU_SING4;
        }
    }

    switch (std::uniform_int_distribution<int>(0, 3)(rng_)) {
        case 0:
            return Lang::Sounds::OGG_BUBU_MUMBLING_1;
        case 1:
            return Lang::Sounds::OGG_BUBU_MUMBLING_2;
        case 2:
            return Lang::Sounds::OGG_BUBU_MUMBLING_3;
        default:
            return Lang::Sounds::OGG_BUBU_MUMBLING_4;
    }
}

void BubuInteractionVoice::MarkPlayed(BubuInteractionEvent event, uint64_t now_ms) {
    switch (event) {
        case BubuInteractionEvent::EyeTap:
        case BubuInteractionEvent::Blink:
            break;
        case BubuInteractionEvent::Mischief:
            last_any_bubu_voice_ms_ = now_ms;
            last_mischief_voice_ms_ = now_ms;
            break;
    }
}

uint32_t BubuInteractionVoice::GetVoiceDurationMs(std::string_view voice) const {
    for (const auto& entry : GetVoiceDurationTable()) {
        // Compare by pointer: each clip is a distinct embedded-file symbol,
        // so identity (not content) is what tells them apart here.
        if (entry.sound.data() == voice.data()) {
            return entry.duration_ms;
        }
    }
    return 0;
}

std::string_view BubuInteractionVoice::GetVoiceForEvent(BubuInteractionEvent event, uint64_t now_ms, bool allow_playback) {
    if (!allow_playback) {
        return {};
    }

    switch (event) {
        case BubuInteractionEvent::EyeTap:
            // Tapping is silent by design; no voice line is associated with it.
            return {};

        case BubuInteractionEvent::Blink:
            // Blinking is silent by design; no voice line is associated with it.
            return {};

        case BubuInteractionEvent::Mischief:
            if (!CooldownElapsed(now_ms, last_any_bubu_voice_ms_, kGlobalCooldownMs)) {
                return {};
            }
            if (!CooldownElapsed(now_ms, last_mischief_voice_ms_, kMischiefCooldownMs)) {
                return {};
            }
            if (std::uniform_int_distribution<int>(0, 1)(rng_) != 0) {
                return {};
            }
            MarkPlayed(event, now_ms);
            return GetOccasionalMumblingVoice();
    }

    return {};
}
