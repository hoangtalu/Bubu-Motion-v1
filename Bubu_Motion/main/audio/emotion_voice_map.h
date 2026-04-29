/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <random>

/**
 * @brief Emotion-to-voice mapping system
 *
 * Maps emotional states to voice lines with support for multiple voices per emotion.
 * Provides round-robin selection to ensure voice variety.
 */
class EmotionVoiceMap {
public:
    /**
     * @brief Initialize the emotion-voice mapping
     *
     * Sets up the mapping between emotions and voice files from lang_config
     */
    void Initialize();

    /**
     * @brief Get a voice for an emotion
     *
     * Returns a voice from the mapped emotion. If the emotion has multiple voices,
     * uses round-robin selection to vary which voice is played.
     *
     * @param emotion The emotion string (e.g., "happy", "sad", "angry")
     * @return Voice data (std::string_view) or empty string if emotion not found
     */
    std::string_view GetVoiceForEmotion(const std::string& emotion);

    /**
     * @brief Get a voice by direct name lookup
     *
     * Allows playing voices independently of emotions
     *
     * @param voice_name The voice identifier (e.g., "happy1", "sad2")
     * @return Voice data (std::string_view) or empty string if not found
     */
    std::string_view GetVoiceByName(const std::string& voice_name);

    /**
     * @brief Get all supported emotions
     *
     * @return Vector of emotion strings that have associated voices
     */
    std::vector<std::string> GetSupportedEmotions() const;

private:
    struct EmotionVoiceEntry {
        std::vector<std::string_view> voices;  // Available voice options
        size_t last_played_index = 0;          // For round-robin selection
    };

    // Emotion → voice mapping
    std::map<std::string, EmotionVoiceEntry> emotion_map_;

    // Direct voice name → voice data mapping
    std::map<std::string, std::string_view> voice_name_map_;

    // Random number generator for voice selection
    std::mt19937 rng_;

    /**
     * @brief Register an emotion with its voice options
     *
     * @param emotion The emotion name
     * @param voices Vector of voice data strings
     */
    void RegisterEmotion(const std::string& emotion,
                        const std::vector<std::string_view>& voices);

    /**
     * @brief Register a voice by name for direct access
     *
     * @param name The voice name
     * @param voice_data The voice data
     */
    void RegisterVoice(const std::string& name, std::string_view voice_data);
};
