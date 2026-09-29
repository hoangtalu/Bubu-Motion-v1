#include "bubu_interaction_voice.h"

#include "assets/lang_config.h"
#include <array>
#include <chrono>

namespace {

struct VoiceDuration {
    std::string_view sound;
    uint32_t duration_ms;
};

// Measured clip lengths (ffprobe) for every vox clip GetVoiceForEvent can
// return. The mischief pose holds at least this long so it never ends
// mid-clip (see EyeAnimation::ExtendMischiefHold).
const std::array<VoiceDuration, 40>& GetVoiceDurationTable() {
    static const std::array<VoiceDuration, 40> table = {{
        {Lang::Sounds::OGG_VOX_ANNOYED_1_A, 661},
        {Lang::Sounds::OGG_VOX_ANNOYED_1_B, 617},
        {Lang::Sounds::OGG_VOX_ANNOYED_2_A, 749},
        {Lang::Sounds::OGG_VOX_ANNOYED_2_B, 981},
        {Lang::Sounds::OGG_VOX_HAPPY_1_A, 787},
        {Lang::Sounds::OGG_VOX_HAPPY_1_B, 460},
        {Lang::Sounds::OGG_VOX_HAPPY_2_A, 943},
        {Lang::Sounds::OGG_VOX_HAPPY_2_B, 966},
        {Lang::Sounds::OGG_VOX_HUM_1_A, 2637},
        {Lang::Sounds::OGG_VOX_HUM_1_B, 2206},
        {Lang::Sounds::OGG_VOX_HUM_2_A, 2246},
        {Lang::Sounds::OGG_VOX_HUM_2_B, 2166},
        {Lang::Sounds::OGG_VOX_HUM_3_A, 2080},
        {Lang::Sounds::OGG_VOX_HUM_3_B, 2659},
        {Lang::Sounds::OGG_VOX_HUM_4_A, 2654},
        {Lang::Sounds::OGG_VOX_HUM_4_B, 2479},
        {Lang::Sounds::OGG_VOX_LAUGH_1_A, 1297},
        {Lang::Sounds::OGG_VOX_LAUGH_1_B, 1354},
        {Lang::Sounds::OGG_VOX_MUMBLE_1_A, 2886},
        {Lang::Sounds::OGG_VOX_MUMBLE_1_B, 2425},
        {Lang::Sounds::OGG_VOX_MUMBLE_2_A, 2286},
        {Lang::Sounds::OGG_VOX_MUMBLE_2_B, 2412},
        {Lang::Sounds::OGG_VOX_MUMBLE_3_A, 2447},
        {Lang::Sounds::OGG_VOX_MUMBLE_3_B, 2296},
        {Lang::Sounds::OGG_VOX_MUMBLE_4_A, 2015},
        {Lang::Sounds::OGG_VOX_MUMBLE_4_B, 2886},
        {Lang::Sounds::OGG_VOX_SAD_1_A, 812},
        {Lang::Sounds::OGG_VOX_SAD_1_B, 1647},
        {Lang::Sounds::OGG_VOX_SAD_2_A, 795},
        {Lang::Sounds::OGG_VOX_SAD_2_B, 1525},
        {Lang::Sounds::OGG_VOX_SURPRISE_1_A, 431},
        {Lang::Sounds::OGG_VOX_SURPRISE_1_B, 726},
        {Lang::Sounds::OGG_VOX_SURPRISE_2_A, 1126},
        {Lang::Sounds::OGG_VOX_SURPRISE_2_B, 756},
        {Lang::Sounds::OGG_VOX_THINK_1_A, 1049},
        {Lang::Sounds::OGG_VOX_THINK_1_B, 1675},
        {Lang::Sounds::OGG_VOX_THINK_2_A, 494},
        {Lang::Sounds::OGG_VOX_THINK_2_B, 694},
        {Lang::Sounds::OGG_VOX_YAWN_1_A, 1352},
        {Lang::Sounds::OGG_VOX_YAWN_1_B, 942},
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

std::string_view BubuInteractionVoice::GetVoiceForMood(Vox::Mood mood) {
    using namespace Lang::Sounds;
    // Each cue is an _a/_b pair; Vox::Pick chooses the take. Where a feeling
    // has two cues (think_1/think_2, ...) one of them is picked first.
    auto either = [this]() { return std::uniform_int_distribution<int>(0, 1)(rng_) == 0; };
    switch (mood) {
        case Vox::Mood::Think:
            return either() ? Vox::Pick(OGG_VOX_THINK_1_A, OGG_VOX_THINK_1_B)
                            : Vox::Pick(OGG_VOX_THINK_2_A, OGG_VOX_THINK_2_B);
        case Vox::Mood::Surprise:
            return either() ? Vox::Pick(OGG_VOX_SURPRISE_1_A, OGG_VOX_SURPRISE_1_B)
                            : Vox::Pick(OGG_VOX_SURPRISE_2_A, OGG_VOX_SURPRISE_2_B);
        case Vox::Mood::Happy:
            return either() ? Vox::Pick(OGG_VOX_HAPPY_1_A, OGG_VOX_HAPPY_1_B)
                            : Vox::Pick(OGG_VOX_HAPPY_2_A, OGG_VOX_HAPPY_2_B);
        case Vox::Mood::Laugh:
            return Vox::Pick(OGG_VOX_LAUGH_1_A, OGG_VOX_LAUGH_1_B);
        case Vox::Mood::Annoyed:
            return either() ? Vox::Pick(OGG_VOX_ANNOYED_1_A, OGG_VOX_ANNOYED_1_B)
                            : Vox::Pick(OGG_VOX_ANNOYED_2_A, OGG_VOX_ANNOYED_2_B);
        case Vox::Mood::Sleepy:
            return Vox::Pick(OGG_VOX_YAWN_1_A, OGG_VOX_YAWN_1_B);
        case Vox::Mood::Hum:
            switch (std::uniform_int_distribution<int>(0, 3)(rng_)) {
                case 0:
                    return Vox::Pick(OGG_VOX_HUM_1_A, OGG_VOX_HUM_1_B);
                case 1:
                    return Vox::Pick(OGG_VOX_HUM_2_A, OGG_VOX_HUM_2_B);
                case 2:
                    return Vox::Pick(OGG_VOX_HUM_3_A, OGG_VOX_HUM_3_B);
                default:
                    return Vox::Pick(OGG_VOX_HUM_4_A, OGG_VOX_HUM_4_B);
            }
        case Vox::Mood::Mumble:
            break;
    }
    switch (std::uniform_int_distribution<int>(0, 3)(rng_)) {
        case 0:
            return Vox::Pick(OGG_VOX_MUMBLE_1_A, OGG_VOX_MUMBLE_1_B);
        case 1:
            return Vox::Pick(OGG_VOX_MUMBLE_2_A, OGG_VOX_MUMBLE_2_B);
        case 2:
            return Vox::Pick(OGG_VOX_MUMBLE_3_A, OGG_VOX_MUMBLE_3_B);
        default:
            return Vox::Pick(OGG_VOX_MUMBLE_4_A, OGG_VOX_MUMBLE_4_B);
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

std::string_view BubuInteractionVoice::GetVoiceForEvent(BubuInteractionEvent event, uint64_t now_ms, bool allow_playback,
                                                        Vox::Mood mood) {
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
            return GetVoiceForMood(mood);
    }

    return {};
}
