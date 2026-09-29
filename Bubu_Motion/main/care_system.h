#pragma once
#include <cstdint>

namespace CareSystem {

  // Stat boost constants (single source of truth)
  static constexpr int kSandwichBoost      = 30;   // Hunger (feed)
  static constexpr int kFeedBiteBoost      = kSandwichBoost / 3;  // Hunger per chomp (3 bites = full boost)
  static constexpr int kGamesBoost         = 10;   // Mood (play)
  static constexpr int kSleepBoost         = 90;   // Energy (sleep)
  static constexpr int kBathBoost          = 90;   // Cleanliness (bath)
  static constexpr int kCleanAnimBoost     = 90;   // Clean animation reward

  enum StatId {
    STAT_HUNGER = 0,
    STAT_MOOD,
    STAT_ENERGY,
    STAT_CLEANLINESS
  };

  // Lifecycle
  void Begin();
  void Update();                      // Call frequently; internal tick is 60s
  void SetDecaySuspended(bool s);     // Pause decay (e.g. during animations)

  // Modifiers
  void AddHunger(int v);
  void AddMood(int v);
  void AddEnergy(int v);
  void AddCleanliness(int v);

  // Getters
  int GetHunger();
  int GetMood();
  int GetEnergy();
  int GetCleanliness();

  // Status flags
  bool NeedsAttention();  // Any stat in [20..39]
  bool IsCritical();      // Any stat == 0
}
