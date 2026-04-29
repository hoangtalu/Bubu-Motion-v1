#include "sound_effects.h"
#include <driver/i2s.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "logger.h"
#include "voice_detector.h"
#include "board_pins.h"

DEFINE_MODULE_LOGGER(SoundEffectsLog)

namespace SoundEffects {

// ============================================================================
// Audio Configuration
// ============================================================================
static constexpr int SAMPLE_RATE = 16000;
static constexpr float kTwoPi = 6.2831853f;
static constexpr i2s_port_t SPK_PORT = I2S_NUM_1;

// ============================================================================
// State & Buffers
// ============================================================================
static volatile bool gMuted = false;
static volatile bool gReady = false;
static SemaphoreHandle_t gAudioMutex = nullptr;
static bool gRawI2sInstalled = false;

// Sound buffer (in PSRAM for large sound generation)
static int16_t* gSoundBuf = nullptr;
static constexpr size_t SOUND_BUF_SIZE = SAMPLE_RATE * 3;  // 3 seconds max

// ============================================================================
// I2S Driver Management
// ============================================================================

static void installRawI2s() {
  if (gRawI2sInstalled) return;

  i2s_config_t cfg = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = PIN_SPK_BCLK,
    .ws_io_num = PIN_SPK_LRCK,
    .data_out_num = PIN_SPK_DOUT,
    .data_in_num = -1
  };

  if (i2s_driver_install(SPK_PORT, &cfg, 0, nullptr) == ESP_OK) {
    i2s_set_pin(SPK_PORT, &pins);
    i2s_zero_dma_buffer(SPK_PORT);
    i2s_start(SPK_PORT);
    gRawI2sInstalled = true;
    SoundEffectsLog::println("[SoundFX] I2S driver installed");
  }
}

static bool lockAudio(uint32_t timeoutMs) {
  if (!gAudioMutex) return true;
  return xSemaphoreTake(gAudioMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void unlockAudio() {
  if (gAudioMutex) {
    xSemaphoreGive(gAudioMutex);
  }
}

// ============================================================================
// PCM Output
// ============================================================================

static void writeRawPcm(const int16_t* buf, size_t sampleCount) {
  if (gMuted || !gReady) return;
  if (!lockAudio(100)) return;
  if (gMuted || !gReady) {
    unlockAudio();
    return;
  }

  if (!gRawI2sInstalled) {
    installRawI2s();
  }

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buf);
  size_t totalBytes = sampleCount * sizeof(int16_t);
  size_t sentBytes = 0;

  while (sentBytes < totalBytes) {
    size_t written = 0;
    esp_err_t err = i2s_write(SPK_PORT, bytes + sentBytes, totalBytes - sentBytes, &written, pdMS_TO_TICKS(100));
    if (err != ESP_OK || written == 0) break;
    sentBytes += written;
  }

  unlockAudio();
}

// ============================================================================
// Zowie bendTones: Smooth frequency transitions via proportional stepping
// ============================================================================

static size_t bendTones(float initFreq, float finalFreq, float proportion, int durationMs, int silenceMs) {
  if (!gSoundBuf || !gReady) return 0;

  // Generate smooth frequency bend
  size_t totalSamples = 0;
  float currentFreq = initFreq;
  float phaseAccum = 0.0f;

  int soundSamples = (durationMs * SAMPLE_RATE) / 1000;
  int silenceSamples = (silenceMs * SAMPLE_RATE) / 1000;

  // Sound portion
  for (int i = 0; i < soundSamples && totalSamples < SOUND_BUF_SIZE; ++i) {
    float sample = sinf(phaseAccum) * 1000;  // Peak amplitude
    gSoundBuf[totalSamples++] = static_cast<int16_t>(sample);

    // Advance phase
    float phaseInc = (kTwoPi * currentFreq) / SAMPLE_RATE;
    phaseAccum += phaseInc;
    if (phaseAccum > kTwoPi) phaseAccum -= kTwoPi;

    // Proportional frequency change
    if (initFreq < finalFreq) {
      currentFreq *= proportion;  // Sweep up
      if (currentFreq > finalFreq) currentFreq = finalFreq;
    } else {
      currentFreq /= proportion;  // Sweep down
      if (currentFreq < finalFreq) currentFreq = finalFreq;
    }
  }

  // Silence portion
  for (int i = 0; i < silenceSamples && totalSamples < SOUND_BUF_SIZE; ++i) {
    gSoundBuf[totalSamples++] = 0;
  }

  return totalSamples;
}

// ============================================================================
// Zowie Sound Implementations (from CuteBuzzerSounds library)
// ============================================================================

void playHappy1() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing HAPPY1 (Zowie)");

  VoiceDetector::suppressFor(200);

  size_t samples = 0;
  // S_HAPPY: bendTones(1500→2500) then bendTones(2500→1500)
  samples += bendTones(1500, 2500, 1.05f, 1000, 8);
  samples += bendTones(2499, 1500, 1.05f, 1500, 8);

  writeRawPcm(gSoundBuf, samples);
}

void playCurious() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing CURIOUS (Zowie)");

  VoiceDetector::suppressFor(200);

  // S_CONFUSED: Similar to confused pattern
  size_t samples = 0;
  samples += bendTones(800, 1200, 1.04f, 15, 10);
  samples += bendTones(1200, 800, 1.04f, 15, 10);

  writeRawPcm(gSoundBuf, samples);
}

void playSad1() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing SAD1 (Zowie)");

  VoiceDetector::suppressFor(300);

  // S_SAD: Slow descending
  size_t samples = 0;
  samples += bendTones(1200, 600, 1.03f, 50, 10);
  samples += bendTones(600, 300, 1.03f, 40, 10);

  writeRawPcm(gSoundBuf, samples);
}

void playAngry1() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing ANGRY1 (Zowie)");

  VoiceDetector::suppressFor(200);

  // S_FART1-like: Sputtery/rasping sound
  size_t samples = 0;
  samples += bendTones(1200, 1500, 1.02f, 8, 2);
  samples += bendTones(1500, 1200, 1.02f, 8, 2);
  samples += bendTones(1300, 1600, 1.02f, 8, 2);

  writeRawPcm(gSoundBuf, samples);
}

void playTired() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing TIRED (Zowie)");

  VoiceDetector::suppressFor(600);

  // S_SLEEPING: Low drowsy wobble — lengthened to better match 1–2s visuals
  size_t samples = 0;
  samples += bendTones(100, 500, 1.02f, 120, 40);   // slow rise (~160 ms total)
  samples += bendTones(500, 100, 1.02f, 160, 200);  // slow fall with a longer exhale pause
  samples += bendTones(300, 600, 1.02f, 120, 80);   // gentle closing rise

  writeRawPcm(gSoundBuf, samples);
}

void playExcited() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing EXCITED (Zowie)");

  VoiceDetector::suppressFor(200);

  // S_SUPER_HAPPY: Excited wild sweeps
  size_t samples = 0;
  samples += bendTones(2000, 6000, 1.05f, 8, 3);
  samples += bendTones(5999, 2000, 1.05f, 13, 2);

  writeRawPcm(gSoundBuf, samples);
}

void playLove() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing LOVE (Zowie)");

  VoiceDetector::suppressFor(400);

  // S_CUDDLY: Gentle loving wobbles
  size_t samples = 0;
  samples += bendTones(700, 900, 1.03f, 16, 4);
  samples += bendTones(899, 650, 1.01f, 18, 7);

  writeRawPcm(gSoundBuf, samples);
}

void playWorried() {
  if (gMuted || !gReady) return;
  SoundEffectsLog::println("[SoundFX] Playing WORRIED (Zowie)");

  VoiceDetector::suppressFor(200);

  // S_OHOOH: Worried/surprised sound
  size_t samples = 0;
  samples += bendTones(880, 2000, 1.04f, 8, 3);
  // Quick pulsing pattern
  for (int i = 0; i < 5 && samples < SOUND_BUF_SIZE - 100; ++i) {
    samples += bendTones(1200, 1400, 1.02f, 5, 5);
  }

  writeRawPcm(gSoundBuf, samples);
}

// ============================================================================
// Public API
// ============================================================================

void begin() {
  if (gReady) return;

  // Allocate sound buffer
  gSoundBuf = static_cast<int16_t*>(
      heap_caps_malloc(SOUND_BUF_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (!gSoundBuf) {
    SoundEffectsLog::println("[SoundFX] Buffer allocation failed");
    return;
  }

  memset(gSoundBuf, 0, SOUND_BUF_SIZE * sizeof(int16_t));

  // Create audio mutex
  if (!gAudioMutex) {
    gAudioMutex = xSemaphoreCreateMutex();
    if (!gAudioMutex) {
      SoundEffectsLog::println("[SoundFX] Mutex creation failed");
      return;
    }
  }

  installRawI2s();

  gReady = true;
  SoundEffectsLog::println("[SoundFX] Zowie sound system ready!");
}

void update() {
  // Placeholder for future ambient sounds
}

void mute(bool enabled) {
  gMuted = enabled;
}

bool isMuted() {
  return gMuted;
}

}  // namespace SoundEffects
