#pragma once

#include <Arduino.h>

// Additive synth sound effects for bubu emotions
// Each sound is a combination of 1-3 sine waves at different frequencies
namespace SoundEffects {
  void begin();              // Initialize I2S and buffers
  void update();             // Ambient sound loop (if needed)
  void mute(bool enabled);   // Mute all sounds

  // Emotion sound effects (1-2 seconds each)
  void playHappy1();         // Double-tone ascending chirp (excited/happy)
  void playCurious();        // Questioning rise-fall tone
  void playSad1();           // Slow descending sigh
  void playAngry1();         // Petulant buzz
  void playTired();          // Drowsy warble
  void playExcited();        // Bouncy multiple tones
  void playLove();           // Gentle dual-tone melody
  void playWorried();        // Nervous quick chirps

  // Utility
  bool isMuted();
}
