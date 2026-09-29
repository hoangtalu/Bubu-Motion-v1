/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "emotion_voice_map.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include <chrono>

static const char* TAG = "EmotionVoiceMap";

namespace {

// SFX disabled for all emotions - see EmotionVoiceMap::Initialize() below.
// std::vector<std::string_view> BuildOccasionalMumblingVoices() {
//     return {
//         Lang::Sounds::OGG_VOX_MUMBLE_1_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_2_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_3_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_4_A,
//         Lang::Sounds::OGG_VOX_HUM_1_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_1_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_2_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_3_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_4_A,
//         Lang::Sounds::OGG_VOX_HUM_2_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_1_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_2_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_3_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_4_A,
//         Lang::Sounds::OGG_VOX_HUM_3_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_1_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_2_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_3_A,
//         Lang::Sounds::OGG_VOX_MUMBLE_4_A,
//         Lang::Sounds::OGG_VOX_HUM_4_A,
//     };
// }
//
// std::vector<std::string_view> BuildEmbarrassedVoices() {
//     auto voices = BuildOccasionalMumblingVoices();
//     voices.push_back(Lang::Sounds::OGG_VOX_SAD_2_B);
//     return voices;
// }

} // namespace

void EmotionVoiceMap::Initialize() {
    // Seed random generator with time
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rng_.seed(seed);

    // Happy emotion - multiple voice options
    // SFX disabled:
    // RegisterEmotion("happy", {
    //     Lang::Sounds::OGG_VOX_HAPPY_2_A,
    //     Lang::Sounds::OGG_VOX_HAPPY_1_A,
    //     Lang::Sounds::OGG_VOX_HAPPY_1_B
    // });
    RegisterEmotion("happy", {});
    // "funny" is the same animation as "happy" (see EyeEmotion_Apply) - share its voice.
    // SFX disabled:
    // RegisterEmotion("funny", {
    //     Lang::Sounds::OGG_VOX_HAPPY_2_A,
    //     Lang::Sounds::OGG_VOX_HAPPY_1_A,
    //     Lang::Sounds::OGG_VOX_HAPPY_1_B
    // });
    RegisterEmotion("funny", {});

    // Laugh emotion
    // SFX disabled:
    // RegisterEmotion("laugh", {Lang::Sounds::OGG_VOX_LAUGH_1_A});
    // RegisterEmotion("laughing", {Lang::Sounds::OGG_VOX_LAUGH_1_A});
    RegisterEmotion("laugh", {});
    RegisterEmotion("laughing", {});

    // Sad emotion - multiple options
    // SFX disabled:
    // RegisterEmotion("sad", {Lang::Sounds::OGG_VOX_SAD_1_A, Lang::Sounds::OGG_VOX_SAD_2_A});
    RegisterEmotion("sad", {});
    // "crying" is the same pose as "sad" (see EyeEmotion_Apply) - share its voice.
    // SFX disabled:
    // RegisterEmotion("crying", {Lang::Sounds::OGG_VOX_SAD_1_A, Lang::Sounds::OGG_VOX_SAD_2_A});
    RegisterEmotion("crying", {});

    // Angry emotion - multiple options
    // SFX disabled:
    // RegisterEmotion("angry", {Lang::Sounds::OGG_VOX_ANNOYED_1_A, Lang::Sounds::OGG_VOX_ANNOYED_2_A});
    RegisterEmotion("angry", {});
    // "annoyed" is the same pose as "angry", just shallower (see EyeEmotion_Apply) - share its voice.
    // SFX disabled:
    // RegisterEmotion("annoyed", {Lang::Sounds::OGG_VOX_ANNOYED_1_A, Lang::Sounds::OGG_VOX_ANNOYED_2_A});
    RegisterEmotion("annoyed", {});

    // Bored emotion
    // SFX disabled:
    // RegisterEmotion("bored", {Lang::Sounds::OGG_VOX_SAD_2_B});
    RegisterEmotion("bored", {});

    // Curious emotion - used for thinking, winking, silly states
    // SFX disabled:
    // RegisterEmotion("curious", {Lang::Sounds::OGG_VOX_THINK_1_A});
    // RegisterEmotion("thinking", {Lang::Sounds::OGG_VOX_THINK_1_A});
    // RegisterEmotion("winking", {Lang::Sounds::OGG_VOX_THINK_1_A});
    // RegisterEmotion("silly", {Lang::Sounds::OGG_VOX_THINK_1_A});
    RegisterEmotion("curious", {});
    RegisterEmotion("thinking", {});
    RegisterEmotion("winking", {});
    RegisterEmotion("silly", {});

    // Tired emotion
    // SFX disabled:
    // RegisterEmotion("tired", {Lang::Sounds::OGG_VOX_YAWN_1_A});
    // RegisterEmotion("sleepy", {Lang::Sounds::OGG_VOX_YAWN_1_A});
    RegisterEmotion("tired", {});
    RegisterEmotion("sleepy", {});

    // Confused keeps the bored fallback
    // SFX disabled:
    // RegisterEmotion("confused", {Lang::Sounds::OGG_VOX_SAD_2_B});
    RegisterEmotion("confused", {});
    // SFX disabled:
    // RegisterEmotion("embarrassed", BuildEmbarrassedVoices());
    // RegisterEmotion("nervous", BuildOccasionalMumblingVoices());
    // RegisterEmotion("anxious", BuildOccasionalMumblingVoices());
    RegisterEmotion("embarrassed", {});
    RegisterEmotion("nervous", {});
    RegisterEmotion("anxious", {});

    // Other recognized emotions without voice (no automatic playback).
    // Registered explicitly (rather than left out) so GetVoiceForEmotion's
    // "Unknown emotion" warning is reserved for emotions the app doesn't
    // actually recognize, not ones that are deliberately silent.
    RegisterEmotion("neutral", {});
    RegisterEmotion("relaxed", {});
    RegisterEmotion("cool", {});
    RegisterEmotion("shocked", {});
    RegisterEmotion("surprised", {});
    // Same visual family as laughing/shocked (see EyeEmotion_Apply) but no
    // dedicated asset - silent like shocked rather than reusing the laugh voice.
    RegisterEmotion("confident", {});
    RegisterEmotion("loving", {});
    RegisterEmotion("kissy", {});
    RegisterEmotion("delicious", {});
    // No dedicated asset for these.
    RegisterEmotion("worried", {});
    RegisterEmotion("skeptic", {});
    RegisterEmotion("skeptical", {});
    RegisterEmotion("doubt", {});
    RegisterEmotion("doubtful", {});
    // Legacy emote set - no dedicated assets. Both the canonical
    // ("legacy_emo_*") and short ("legacy_*") forms are registered since
    // EyeEmotion_Apply accepts either and the server may send either.
    for (const char* legacy : {"love", "cyclop", "drunk", "confuse", "angry",
                                "furious", "banh_chung", "deadpool", "cry"}) {
        RegisterEmotion(std::string("legacy_emo_") + legacy, {});
        RegisterEmotion(std::string("legacy_") + legacy, {});
    }

    ESP_LOGI(TAG, "EmotionVoiceMap initialized with %d emotions and %d voice mappings",
             emotion_map_.size(), voice_name_map_.size());
}

std::string_view EmotionVoiceMap::GetVoiceForEmotion(const std::string& emotion) {
    auto it = emotion_map_.find(emotion);
    if (it == emotion_map_.end()) {
        ESP_LOGW(TAG, "Unknown emotion: %s", emotion.c_str());
        return {};
    }

    auto& entry = it->second;
    if (entry.voices.empty()) {
        // Emotion recognized but has no voice (e.g., "neutral")
        return {};
    }

    // Select voice using round-robin to ensure variety
    auto voice = entry.voices[entry.last_played_index];
    entry.last_played_index = (entry.last_played_index + 1) % entry.voices.size();

    ESP_LOGD(TAG, "Selected voice for emotion '%s': index %zu/%zu",
             emotion.c_str(), entry.last_played_index - 1, entry.voices.size());

    return voice;
}

std::string_view EmotionVoiceMap::GetVoiceByName(const std::string& voice_name) {
    auto it = voice_name_map_.find(voice_name);
    if (it == voice_name_map_.end()) {
        ESP_LOGW(TAG, "Unknown voice name: %s", voice_name.c_str());
        return {};
    }

    ESP_LOGD(TAG, "Found voice by name: %s", voice_name.c_str());
    return it->second;
}

std::vector<std::string> EmotionVoiceMap::GetSupportedEmotions() const {
    std::vector<std::string> emotions;
    for (const auto& pair : emotion_map_) {
        if (!pair.second.voices.empty()) {
            emotions.push_back(pair.first);
        }
    }
    return emotions;
}

void EmotionVoiceMap::RegisterEmotion(const std::string& emotion,
                                     const std::vector<std::string_view>& voices) {
    emotion_map_[emotion] = {voices, 0};

    if (!voices.empty()) {
        ESP_LOGD(TAG, "Registered emotion '%s' with %zu voice(s)", emotion.c_str(), voices.size());
    }
}

void EmotionVoiceMap::RegisterVoice(const std::string& name, std::string_view voice_data) {
    voice_name_map_[name] = voice_data;
    ESP_LOGD(TAG, "Registered voice name: %s", name.c_str());
}
