#include "bubu_interaction_voice.h"

#include "assets/lang_config.h"
#include <chrono>

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
            break;
        case BubuInteractionEvent::Blink:
            last_any_bubu_voice_ms_ = now_ms;
            last_blink_voice_ms_ = now_ms;
            break;
        case BubuInteractionEvent::Mischief:
            last_any_bubu_voice_ms_ = now_ms;
            last_mischief_voice_ms_ = now_ms;
            break;
    }
}

std::string_view BubuInteractionVoice::GetVoiceForEvent(BubuInteractionEvent event, uint64_t now_ms, bool allow_playback) {
    if (!allow_playback) {
        return {};
    }

    switch (event) {
        case BubuInteractionEvent::EyeTap:
            return Lang::Sounds::OGG_BUBU_TAP;

        case BubuInteractionEvent::Blink:
            if (!CooldownElapsed(now_ms, last_any_bubu_voice_ms_, kGlobalCooldownMs)) {
                return {};
            }
            if (!CooldownElapsed(now_ms, last_blink_voice_ms_, kBlinkCooldownMs)) {
                return {};
            }
            if (std::uniform_int_distribution<int>(0, 2)(rng_) != 0) {
                return {};
            }
            MarkPlayed(event, now_ms);
            return Lang::Sounds::OGG_BUBU_BLINK;

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
