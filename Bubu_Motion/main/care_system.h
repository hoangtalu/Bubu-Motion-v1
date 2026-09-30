#pragma once
#include <cstdint>

#include "care_model.h"

// Bubu's needs, badges and friendship, on the device.
//
// The rules live in care_model.{h,cc} (plain C++, host-tested); this layer
// feeds them the wall clock and device events, pays XP into LevelSystem and
// persists everything to NVS. Design: docs/care-system-plan.md.
//
// Call from the main task. Getters are safe from the LVGL task too.
namespace CareSystem {

  // Stat boost constants (single source of truth)
  static constexpr int kSandwichBoost      = 30;   // Hunger (feed)
  static constexpr int kFeedBiteBoost      = kSandwichBoost / 3;  // Hunger per chomp (3 bites = full boost)
  static constexpr int kGamesBoost         = 10;   // Mood (play)
  static constexpr int kBathBoost          = 90;   // Cleanliness (bath)

  // Lifecycle
  void Begin();
  void Update();                      // once a second, main task

  // What the device reports
  void OnInteraction();               // the child touched Bubu or pressed a button: wakes it
  void FellAsleep();                  // EyeDisplay's sleep mode started: a nap, or the night
  void SetPlaying(bool playing);      // a full-screen game owns the screen

  // Care actions
  bool BeginFeed();                   // false = Bubu is full and will refuse
  void OnFeedBite();
  void OnBath();
  bool PutToBed();                    // true = for the night, false = a nap
  int OnChat();                       // a conversation started: +10 CẢM XÚC
  int RewardGame(int base);           // CẢM XÚC actually gained
  int PreviewGameReward(int base);    // what RewardGame(base) would pay now
  int RewardStudy(int base);
  void AddMood(int v);                // generic, through the ceiling

  // Stats, 0..100
  int GetHunger();                    // CÁI BỤNG (100 = full)
  int GetMood();                      // CẢM XÚC
  int GetEnergy();                    // NĂNG LƯỢNG
  int GetCleanliness();               // SẠCH SẼ
  int GetMoodCeiling();

  // Status
  bool Showing(care::Need need);      // this need is showing now
  bool NeedsAttention();              // any need showing
  bool IsCritical();                  // a stat at its floor
  bool IsTired();                     // games pay half
  bool IsExhausted();                 // games pay nothing
  bool IsBedtime();                   // sleepy time on the local clock
  bool IsAsleep();
  care::Need BubbleNeed();            // what the thought bubble should show
  care::Need PollVoiceAsk(bool can_play);  // consumes the day's ask budget
  care::Need AskBeforeGame(bool can_play); // exhausted: offer a nap, same budget
  int GetStage();                     // 0 MỚI QUEN .. 3 BẠN THÂN
  care::Trait GetTrait();

  // HUY HIỆU
  bool HasPendingBadge();
  bool PeekPendingBadge(care::Badge* out);
  bool PopPendingBadge(care::Badge* out);
  uint16_t BadgeCount(care::Badge badge);
  void BadgeProgress(care::Badge badge, int* have, int* need);
}
