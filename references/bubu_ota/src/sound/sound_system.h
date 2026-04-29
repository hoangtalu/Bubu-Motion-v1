#pragma once

#include <Arduino.h>

// Sound system using raw I2S on I2S_NUM_1 for programmatic sound effects.
namespace SoundSystem {
  void begin();              // Init I2S_NUM_1 and allocate sound buffers
  void update();             // Generate ambient sounds (white noise, rain)

  // One-shot sound effects (generated programmatically, written via Audio I2S)
  void blinkClink();
  void eyeSwoosh(float strength);
  void eyeJitter(float strength);
  void happyPip(float strength);
  void sadSigh(float strength);
  void reminderChime();         // Gentle notification sound for reminders
  void mute(bool enabled);

  // Continuous ambient sounds for sleep mode
  void startWhiteNoise();
  void stopWhiteNoise();
  bool isWhiteNoisePlaying();
  void whiteNoiseVolumeUp();
  void whiteNoiseVolumeDown();
  uint8_t getWhiteNoiseVolume();

  // Rain sound (pink noise)
  void startRain();
  void stopRain();
  bool isRainPlaying();
  void rainVolumeUp();
  void rainVolumeDown();
  uint8_t getRainVolume();

  // Chat system coordination
  void setChatStreaming(bool streaming);  // When true, sound effects are suppressed
  bool isChatStreaming();
}
