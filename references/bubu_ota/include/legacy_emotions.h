#pragma once
#include <Arduino.h>

// Legacy emotions ported from old emotion_engine system
// These use the current display system's LovyanGFX sprites

namespace LegacyEmotions {

enum EmotionType {
  LEGACY_LOVE,
  LEGACY_CYCLOP,
  LEGACY_DRUNK
};

// Initialize the legacy emotions system (call once in setup)
void begin();

// Update loop (call from display_system when a legacy emotion is active)
void update(uint32_t nowMs);

// Trigger a specific legacy emotion (duration in milliseconds)
void trigger(EmotionType emotion, uint32_t durationMs);

// Check if a legacy emotion is currently playing
bool isActive();

// Get the currently playing emotion (only valid if isActive() returns true)
EmotionType getCurrentEmotion();

// Render the current frame to the provided sprite
// Returns true if the emotion is still active, false if finished
bool renderFrame(void* sprite, uint32_t nowMs);

} // namespace LegacyEmotions
