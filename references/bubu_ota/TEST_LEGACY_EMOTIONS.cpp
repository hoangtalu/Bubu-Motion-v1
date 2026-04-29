/**
 * TEST_LEGACY_EMOTIONS.cpp
 *
 * Simple test code to cycle through the legacy emotions.
 *
 * HOW TO USE:
 * 1. Add this line at the top of main.cpp (after includes):
 *    void testLegacyEmotions();
 *
 * 2. Add this line at the END of the loop() function in main.cpp:
 *    testLegacyEmotions();
 *
 * 3. Upload and watch the emotions cycle every 10 seconds
 */

#include "display_system.h"

void testLegacyEmotions() {
  static uint32_t lastTriggerTime = 0;
  static int currentEmotionIndex = 0;
  static bool emotionTriggered = false;

  uint32_t now = millis();

  // Wait 10 seconds between emotions
  if (now - lastTriggerTime > 10000) {
    // Only trigger if no legacy emotion is currently playing
    if (!DisplaySystem_isLegacyEmotionActive()) {
      // Cycle through the three emotions
      LegacyEmotionType emotions[] = {
        LEGACY_EMO_LOVE,    // 0: Heart eyes with cheek blush
        LEGACY_EMO_CYCLOP,  // 1: Single giant eye
        LEGACY_EMO_DRUNK    // 2: Whirlpool eyes
      };

      const char* names[] = {
        "LOVE",
        "CYCLOP",
        "DRUNK"
      };

      // Trigger the current emotion for 8 seconds
      DisplaySystem_triggerLegacyEmotion(emotions[currentEmotionIndex], 8000);

      Serial.printf("[TEST] Triggered %s emotion\n", names[currentEmotionIndex]);

      // Move to next emotion
      currentEmotionIndex = (currentEmotionIndex + 1) % 3;
      lastTriggerTime = now;
      emotionTriggered = true;
    }
  }

  // Optional: Log when emotion finishes
  if (emotionTriggered && !DisplaySystem_isLegacyEmotionActive()) {
    Serial.println("[TEST] Emotion finished, back to normal eyes");
    emotionTriggered = false;
  }
}

/**
 * ALTERNATIVE: Manual trigger via Serial commands
 *
 * Add this to loop() instead if you want to control emotions via Serial Monitor:
 */
void testLegacyEmotionsSerial() {
  if (Serial.available()) {
    char cmd = Serial.read();

    switch(cmd) {
      case 'l':
      case 'L':
        Serial.println("[TEST] Triggering LOVE emotion");
        DisplaySystem_triggerLegacyEmotion(LEGACY_EMO_LOVE, 8000);
        break;

      case 'c':
      case 'C':
        Serial.println("[TEST] Triggering CYCLOP emotion");
        DisplaySystem_triggerLegacyEmotion(LEGACY_EMO_CYCLOP, 8000);
        break;

      case 'd':
      case 'D':
        Serial.println("[TEST] Triggering DRUNK emotion");
        DisplaySystem_triggerLegacyEmotion(LEGACY_EMO_DRUNK, 8000);
        break;

      case '?':
      case 'h':
        Serial.println("=== Legacy Emotions Test ===");
        Serial.println("Commands:");
        Serial.println("  L - Trigger LOVE emotion");
        Serial.println("  C - Trigger CYCLOP emotion");
        Serial.println("  D - Trigger DRUNK emotion");
        Serial.println("  ? - Show this help");
        Serial.println("===========================");
        break;
    }
  }
}
