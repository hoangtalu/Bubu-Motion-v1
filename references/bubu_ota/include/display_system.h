#pragma once
#include <Arduino.h>

void DisplaySystem_begin();
void DisplaySystem_update();

enum EyeEmotion {
  EYE_EMO_IDLE = 0,
  EYE_EMO_CURIOUS,
  EYE_EMO_ANGRY1,
  EYE_EMO_LOVE,
  EYE_EMO_TIRED,
  EYE_EMO_EXCITED,
  EYE_EMO_ANGRY2,
  EYE_EMO_ANGRY3,
  EYE_EMO_WORRIED1,
  EYE_EMO_CURIOUS1,
  EYE_EMO_CURIOUS2,
  EYE_EMO_SAD1,
  EYE_EMO_SAD2,
  EYE_EMO_HAPPY1,
  EYE_EMO_HAPPY2,
  EYE_EMO_COUNT
};

EyeEmotion DisplaySystem_getEmotion();
void DisplaySystem_setEmotion(EyeEmotion emotion);
void DisplaySystem_setEmotionWeight(EyeEmotion emotion, uint16_t weight);
EyeEmotion DisplaySystem_pickNextEmotionWeighted();
void DisplaySystem_startExcitedNow();
void DisplaySystem_startClean();
void DisplaySystem_startSleep();
void DisplaySystem_startWhiteNoiseSleep();
void DisplaySystem_stopWhiteNoiseSleep();
bool DisplaySystem_isWhiteNoiseSleepActive();
void DisplaySystem_startRainSleep();
void DisplaySystem_stopRainSleep();
bool DisplaySystem_isRainSleepActive();
// Care UX: show a brief happy face after successful care actions
void DisplaySystem_showCareHappy();
bool DisplaySystem_isHatching();

// Notify display system of user interaction (touch/gesture) for idle visual logic
void DisplaySystem_notifyUserInteraction(uint32_t nowMs);

// Legacy emotion system integration
enum LegacyEmotionType {
  LEGACY_EMO_LOVE = 0,
  LEGACY_EMO_CYCLOP,
  LEGACY_EMO_DRUNK
};

// Trigger a legacy emotion animation (duration in milliseconds)
void DisplaySystem_triggerLegacyEmotion(LegacyEmotionType emotion, uint32_t durationMs);

// Check if a legacy emotion is currently playing
bool DisplaySystem_isLegacyEmotionActive();

// Check if clock screensaver is active (blocks all behaviors)
bool DisplaySystem_isClockVisible();

// TEMP: cycles through all emotions in random order for visual testing — remove before release
void DisplaySystem_startEmotionTest();
