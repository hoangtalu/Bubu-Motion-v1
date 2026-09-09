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

std::vector<std::string_view> BuildOccasionalMumblingVoices() {
    return {
        Lang::Sounds::OGG_BUBU_MUMBLING_1,
        Lang::Sounds::OGG_BUBU_MUMBLING_2,
        Lang::Sounds::OGG_BUBU_MUMBLING_3,
        Lang::Sounds::OGG_BUBU_MUMBLING_4,
        Lang::Sounds::OGG_BUBU_SING1,
        Lang::Sounds::OGG_BUBU_MUMBLING_1,
        Lang::Sounds::OGG_BUBU_MUMBLING_2,
        Lang::Sounds::OGG_BUBU_MUMBLING_3,
        Lang::Sounds::OGG_BUBU_MUMBLING_4,
        Lang::Sounds::OGG_BUBU_SING2,
        Lang::Sounds::OGG_BUBU_MUMBLING_1,
        Lang::Sounds::OGG_BUBU_MUMBLING_2,
        Lang::Sounds::OGG_BUBU_MUMBLING_3,
        Lang::Sounds::OGG_BUBU_MUMBLING_4,
        Lang::Sounds::OGG_BUBU_SING3,
        Lang::Sounds::OGG_BUBU_MUMBLING_1,
        Lang::Sounds::OGG_BUBU_MUMBLING_2,
        Lang::Sounds::OGG_BUBU_MUMBLING_3,
        Lang::Sounds::OGG_BUBU_MUMBLING_4,
        Lang::Sounds::OGG_BUBU_SING4,
    };
}

std::vector<std::string_view> BuildEmbarrassedVoices() {
    auto voices = BuildOccasionalMumblingVoices();
    voices.push_back(Lang::Sounds::OGG_BUBU_BORED1);
    return voices;
}

} // namespace

void EmotionVoiceMap::Initialize() {
    // Seed random generator with time
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rng_.seed(seed);

    // Happy emotion - multiple voice options
    RegisterEmotion("happy", {
        Lang::Sounds::OGG_BUBU_HAPPY2,
        Lang::Sounds::OGG_BUBU_HAPPY1,
        Lang::Sounds::OGG_BUBU_HAPPY3
    });
    // "funny" is the same animation as "happy" (see EyeEmotion_Apply) - share its voice.
    RegisterEmotion("funny", {
        Lang::Sounds::OGG_BUBU_HAPPY2,
        Lang::Sounds::OGG_BUBU_HAPPY1,
        Lang::Sounds::OGG_BUBU_HAPPY3
    });

    // Laugh emotion
    RegisterEmotion("laugh", {Lang::Sounds::OGG_BUBU_LAUGH});
    RegisterEmotion("laughing", {Lang::Sounds::OGG_BUBU_LAUGH});

    // Sad emotion - multiple options
    RegisterEmotion("sad", {Lang::Sounds::OGG_BUBU_SAD1, Lang::Sounds::OGG_BUBU_SAD2});
    // "crying" is the same pose as "sad" (see EyeEmotion_Apply) - share its voice.
    RegisterEmotion("crying", {Lang::Sounds::OGG_BUBU_SAD1, Lang::Sounds::OGG_BUBU_SAD2});

    // Angry emotion - multiple options
    RegisterEmotion("angry", {Lang::Sounds::OGG_BUBU_ANGRY1, Lang::Sounds::OGG_BUBU_ANGRY2});
    // "annoyed" is the same pose as "angry", just shallower (see EyeEmotion_Apply) - share its voice.
    RegisterEmotion("annoyed", {Lang::Sounds::OGG_BUBU_ANGRY1, Lang::Sounds::OGG_BUBU_ANGRY2});

    // Bored emotion
    RegisterEmotion("bored", {Lang::Sounds::OGG_BUBU_BORED1});

    // Curious emotion - used for thinking, winking, silly states
    RegisterEmotion("curious", {Lang::Sounds::OGG_BUBU_CURIOUS1});
    RegisterEmotion("thinking", {Lang::Sounds::OGG_BUBU_CURIOUS1});
    RegisterEmotion("winking", {Lang::Sounds::OGG_BUBU_CURIOUS1});
    RegisterEmotion("silly", {Lang::Sounds::OGG_BUBU_CURIOUS1});

    // Tired emotion
    RegisterEmotion("tired", {Lang::Sounds::OGG_BUBU_TIRED1});
    RegisterEmotion("sleepy", {Lang::Sounds::OGG_BUBU_TIRED1});

    // Confused keeps the bored fallback
    RegisterEmotion("confused", {Lang::Sounds::OGG_BUBU_BORED1});
    RegisterEmotion("embarrassed", BuildEmbarrassedVoices());
    RegisterEmotion("nervous", BuildOccasionalMumblingVoices());
    RegisterEmotion("anxious", BuildOccasionalMumblingVoices());

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
