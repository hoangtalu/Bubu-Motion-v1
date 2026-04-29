#pragma once
#include <cstdint>

namespace LevelSystem {

  enum FeatureID {
    IDLE_JITTER,
    IDLE_GIGGLE,
    IDLE_JUDGING,
    IDLE_SPEED_FAST,
    EMO_EXCITED,
    EMO_ANGRY1,
    EMO_LOVE,
    EMO_SAD1,
    EMO_HAPPY1,
    LEGACY_EMO_LOVE,
    LEGACY_EMO_CYCLOP,
    LEGACY_EMO_DRUNK,
    FEATURE_COUNT
  };

  void Begin();
  void AddXP(int amount);
  void Tick();  // Call periodically; flushes deferred NVS write (max once/30s)

  int  GetLevel();
  int  GetXP();
  int  GetXPForNextLevel();
  bool IsUnlocked(FeatureID feature);
}
