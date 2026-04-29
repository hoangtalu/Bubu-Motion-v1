#include "level_system.h"
#include "logger.h"
#include <Preferences.h>

DEFINE_MODULE_LOGGER(LevelLog)

namespace LevelSystem {

  // --- Private State ---
  Preferences preferences;
  const char* NVS_NAMESPACE = "bubu-level";
  const char* NVS_KEY_LEVEL = "level";
  const char* NVS_KEY_XP = "xp";

  int currentLevel = 1;
  int currentXP = 0;

  // --- Dirty flag + save throttle ---
  // XP is always kept in RAM. NVS is only written:
  //   a) immediately on level-up (rare, worth it)
  //   b) every SAVE_INTERVAL_MS if XP changed (handles normal feeding)
  static bool     sDirty          = false;
  static uint32_t sLastSaveMs     = 0;
  static constexpr uint32_t SAVE_INTERVAL_MS = 30000;  // 30 seconds max lag on XP persistence

  // --- Tunable constants for the XP curve ---
  const int baseXP = 50;
  const int stepXP = 25;

  // --- Forward Declarations ---
  void saveState();
  void loadState();
  void checkLevelUp();


  // --- Public API Implementation ---

  void begin() {
    loadState();
    LevelLog::printf("Level System initialized. Level: %d, XP: %d / %d\n", currentLevel, currentXP, getXPForNextLevel());
  }

  void addXP(int amount) {
    if (amount <= 0) return;

    currentXP += amount;
    LevelLog::printf("Gained %d XP. Total XP: %d / %d\n", amount, currentXP, getXPForNextLevel());
    checkLevelUp();
  }

  int getLevel() {
    return currentLevel;
  }

  int getXP() {
    return currentXP;
  }

  int getXPForNextLevel() {
    return baseXP + (currentLevel * stepXP);
  }

  bool isUnlocked(FeatureID feature) {
    int level = getLevel();
    switch (feature) {
      // Level 1: Core calm emotions are unlocked by default
      case EMO_SAD1:        return level >= 1;
      case EMO_HAPPY1:      return level >= 1;

      // Progressive Unlocks
      case EMO_EXCITED:     return level >= 2;
      case IDLE_JITTER:     return level >= 3;
      case EMO_ANGRY1:      return level >= 4;
      case IDLE_GIGGLE:     return level >= 5;

      // Legacy Emotions (spread across levels)
      case LEGACY_EMO_CYCLOP: return level >= 5;
      case LEGACY_EMO_LOVE:   return level >= 6;
      case LEGACY_EMO_DRUNK:  return level >= 7;

      case IDLE_JUDGING:    return level >= 10;
      case IDLE_SPEED_FAST: return level >= 12;

      // Removed features (permanently locked)
      case EMO_LOVE:        return false;

      default:
        return false;
    }
  }


  // --- Private Helper Functions ---

  void checkLevelUp() {
    int requiredXP = getXPForNextLevel();
    if (currentXP >= requiredXP) {
      while (currentXP >= requiredXP) {
        currentLevel++;
        currentXP -= requiredXP;
        requiredXP = getXPForNextLevel();
        LevelLog::printf("LEVEL UP! Reached Level %d\n", currentLevel);
      }
      // Level-up: save immediately (rare event, data is important)
      saveState();
      sDirty = false;
      sLastSaveMs = millis();
    } else {
      // Just mark dirty — tick() will persist when the interval fires
      sDirty = true;
    }
  }

  void saveState() {
    preferences.begin(NVS_NAMESPACE, false);
    preferences.putInt(NVS_KEY_LEVEL, currentLevel);
    preferences.putInt(NVS_KEY_XP, currentXP);
    preferences.end();
  }

  // Called from main loop (TIER 5, ~500ms). Flushes XP to NVS if dirty and
  // enough time has passed. Never blocks during feeding interactions.
  void tick() {
    if (!sDirty) return;
    uint32_t now = millis();
    if (now - sLastSaveMs >= SAVE_INTERVAL_MS) {
      saveState();
      sDirty = false;
      sLastSaveMs = now;
      LevelLog::printf("[LevelSystem] XP persisted (deferred save). Level: %d, XP: %d\n", currentLevel, currentXP);
    }
  }

  void loadState() {
    preferences.begin(NVS_NAMESPACE, true); // Read-only to start
    // Load level, default to 1 if not found
    currentLevel = preferences.getInt(NVS_KEY_LEVEL, 1);
    // Load XP, default to 0 if not found
    currentXP = preferences.getInt(NVS_KEY_XP, 0);
    preferences.end();

    // Minimum level 4 - bump up and save if below (remove after testing)
  }

} // namespace LevelSystem
