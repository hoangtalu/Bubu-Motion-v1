#pragma once

#include <cstdint>
#include <esp_random.h>
#include <string_view>

// Every Bubu vocalization (assets/common/vox_*.ogg, bed_*.ogg) ships as an
// _a/_b pair recorded from the same prompt; callers pass both and one is
// picked at random so repeated cues don't sound canned.
namespace Vox {

inline std::string_view Pick(std::string_view a, std::string_view b) {
    return (esp_random() & 1) ? b : a;
}

// The feeling a mischief cycle expresses. EyeAnimation picks one first and
// builds the eye pose for it; BubuInteractionVoice then picks the matching
// vox clip, so what Bubu looks like and what it sounds like always agree.
enum class Mood : uint8_t {
    Mumble,
    Hum,
    Think,
    Surprise,
    Happy,
    Laugh,
    Annoyed,
    Sleepy,
};

}  // namespace Vox
