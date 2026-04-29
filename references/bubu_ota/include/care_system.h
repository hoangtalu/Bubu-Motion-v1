#pragma once
#include <Arduino.h>

namespace CareSystem {
  // Centralized stat deltas (single source of truth).
  static constexpr int kSandwichBoost = 30;        // Hunger option (Sandwich)
  static constexpr int kGamesBoost = 10;           // Mood option (Games)
  static constexpr int kSleepBoost = 90;           // Energy option (Sleep)
  static constexpr int kBathBoost = 90;            // Cleanliness option (Bath)
  static constexpr int kCleanAnimBoost = 90;       // Clean animation reward
  static constexpr int kGameRewardPerHit = 10;     // Tap the Greens per hit
  static constexpr int kGameWrongTapMood = -1;     // Wrong tap penalty
  static constexpr int kGameWrongTapEnergy = -5;   // Wrong tap penalty

  enum StatId {
    STAT_HUNGER = 0,
    STAT_MOOD,
    STAT_ENERGY,
    STAT_CLEANLINESS
  };

  void begin();
  void update();  // call frequently; internal tick is 60s
  void setDecaySuspended(bool suspended);

  void addHunger(int v);
  void addMood(int v);
  void addEnergy(int v);
  void addCleanliness(int v);

  int getHunger();
  int getMood();
  int getEnergy();
  int getCleanliness();

  bool needsAttention();   // any stat in [20..39]
  bool isCritical();       // any stat == 0
}
