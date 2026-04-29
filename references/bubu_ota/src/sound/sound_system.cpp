#include "sound_system.h"

#include <driver/i2s.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "logger.h"
#include "voice_detector.h"
#include "board_pins.h"
DEFINE_MODULE_LOGGER(SoundLog)

// Suppress mic for this duration after playing sound (prevents feedback)
static constexpr uint32_t MIC_SUPPRESS_MS = 100;

namespace SoundSystem {

// --------------------------------------------------------------------
// Audio format
// --------------------------------------------------------------------
static constexpr int SAMPLE_RATE = 16000;
static constexpr float kTwoPi = 6.2831853f;

// Blink sound parameters
static constexpr int BLINK_FREQ_HZ = 2000;
static constexpr float BLINK_DURATION_SEC = 0.006f;
static constexpr size_t BLINK_SAMPLES = static_cast<size_t>(SAMPLE_RATE * BLINK_DURATION_SEC);
static constexpr int16_t BLINK_PEAK = 50000;

// Eye swoosh parameters
static constexpr float SWOOSH_BASE_DURATION_SEC = 0.040f;
static constexpr float SWOOSH_MAX_DURATION_SEC  = 0.080f;
static constexpr int   SWOOSH_F_START_HZ = 1400;
static constexpr int   SWOOSH_F_END_HZ   = 700;
static constexpr int16_t SWOOSH_PEAK_MIN = 24000;
static constexpr int16_t SWOOSH_PEAK_MAX = 24000;
static constexpr size_t SWOOSH_MAX_SAMPLES = static_cast<size_t>(SAMPLE_RATE * SWOOSH_MAX_DURATION_SEC);

// Jitter noise parameters
static constexpr float JITTER_BASE_DURATION_SEC = 0.012f;
static constexpr float JITTER_MAX_DURATION_SEC  = 0.020f;
static constexpr int16_t JITTER_PEAK_MIN = 24000;
static constexpr int16_t JITTER_PEAK_MAX = 24000;
static constexpr size_t JITTER_MAX_SAMPLES = static_cast<size_t>(SAMPLE_RATE * JITTER_MAX_DURATION_SEC);

// Happy pip parameters
static constexpr float HAPPY_PIP_DURATION_SEC = 0.025f;
static constexpr int HAPPY_PIP_BASE_HZ = 900;
static constexpr float HAPPY_PIP_DETUNE = 0.03f;
static constexpr int16_t HAPPY_PIP_PEAK_MIN = 24000;
static constexpr int16_t HAPPY_PIP_PEAK_MAX = 24000;
static constexpr size_t HAPPY_PIP_SAMPLES = static_cast<size_t>(SAMPLE_RATE * HAPPY_PIP_DURATION_SEC);

// Sad sigh parameters
static constexpr float SAD_SIGH_BASE_DURATION_SEC = 0.150f;
static constexpr float SAD_SIGH_MAX_DURATION_SEC  = 0.250f;
static constexpr int   SAD_SIGH_F_START_HZ = 600;
static constexpr int   SAD_SIGH_F_END_HZ   = 300;
static constexpr int16_t SAD_SIGH_PEAK_MIN = 18000;
static constexpr int16_t SAD_SIGH_PEAK_MAX = 22000;
static constexpr size_t SAD_SIGH_MAX_SAMPLES = static_cast<size_t>(SAMPLE_RATE * SAD_SIGH_MAX_DURATION_SEC);

// Reminder chime parameters (gentle 2-tone bell)
static constexpr float CHIME_DURATION_SEC = 1.0f;
static constexpr int CHIME_F1_HZ = 100;   // First tone
static constexpr int CHIME_F2_HZ = 1000;  // Second tone (higher)
static constexpr int16_t CHIME_PEAK = 200;
static constexpr size_t CHIME_SAMPLES = static_cast<size_t>(SAMPLE_RATE * CHIME_DURATION_SEC);

// White noise parameters
static constexpr size_t WHITE_NOISE_CHUNK_SAMPLES = 512;
static constexpr size_t RAIN_CHUNK_SAMPLES = 512;
static constexpr uint32_t I2S_WRITE_TIMEOUT_MS = 20;
static constexpr uint32_t AMBIENT_IDLE_DELAY_MS = 8;

// State
static volatile bool gMuted = false;
static volatile bool gReady = false;
static volatile bool gChatStreaming = false;

// Sound effect buffers (in PSRAM)
static int16_t* gBlinkBuf = nullptr;
static int16_t* gSwooshBuf = nullptr;
static int16_t* gJitterBuf = nullptr;
static int16_t* gHappyBuf = nullptr;
static int16_t* gSadBuf = nullptr;
static int16_t* gChimeBuf = nullptr;
static int16_t* gWhiteNoiseBuf = nullptr;
static int16_t* gRainBuf = nullptr;

// White noise state
static volatile bool gWhiteNoiseActive = false;
static volatile uint8_t gWhiteNoiseVolume = 3;

// Rain (pink noise) state
static volatile bool gRainActive = false;
static volatile uint8_t gRainVolume = 3;
static float gPinkFilterState = 0.0f;
static constexpr float PINK_ALPHA = 0.12f;

// Volume mapping
// White noise is intentionally attenuated to 20% of the prior curve (80% reduction).
static constexpr int16_t WHITE_NOISE_AMPLITUDES[] = {
  0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};
static constexpr int16_t RAIN_AMPLITUDES[] = {
  0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};

// LFSR for white noise
static uint32_t gLfsr = 0xACE1u;
static inline uint32_t lfsr_next() {
  uint32_t lsb = gLfsr & 1u;
  gLfsr >>= 1;
  if (lsb) gLfsr ^= 0xB4BCD35Cu;
  return gLfsr;
}

// I2S_NUM_1 port reference (used for raw writes when Audio is not streaming)
static constexpr i2s_port_t SPK_PORT = I2S_NUM_1;

// Flag: is the I2S driver manually installed (for raw sound effects)?
static bool gRawI2sInstalled = false;
static SemaphoreHandle_t gAudioMutex = nullptr;
static TaskHandle_t gAmbientTaskHandle = nullptr;

// Install raw I2S driver on I2S_NUM_1 for direct PCM writes
static void installRawI2s() {
  if (gRawI2sInstalled) return;

  i2s_config_t cfg = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    // Larger DMA queue smooths ambient playback when the main loop jitters.
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
  }
}

// Uninstall raw I2S driver (before Audio library takes over)
static void uninstallRawI2s() {
  if (!gRawI2sInstalled) return;
  i2s_driver_uninstall(SPK_PORT);
  gRawI2sInstalled = false;
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

// Write raw PCM samples to speaker (only when Audio is not streaming)
static void writeRawPcm(const int16_t* buf, size_t sampleCount) {
  if (gChatStreaming || !gReady) return;
  if (!lockAudio(I2S_WRITE_TIMEOUT_MS)) return;
  if (gChatStreaming || !gReady) {
    unlockAudio();
    return;
  }

  // Ensure raw I2S driver is installed
  if (!gRawI2sInstalled) {
    // Temporarily take over I2S_NUM_1
    installRawI2s();
  }

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buf);
  size_t totalBytes = sampleCount * sizeof(int16_t);
  size_t sentBytes = 0;
  while (sentBytes < totalBytes) {
    size_t written = 0;
    esp_err_t err = i2s_write(
        SPK_PORT,
        bytes + sentBytes,
        totalBytes - sentBytes,
        &written,
        pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS));
    if (err != ESP_OK || written == 0) break;
    sentBytes += written;
  }
  unlockAudio();
}

static bool runAmbientChunk() {
  if (!gReady || gChatStreaming || gMuted) return false;

  if (gWhiteNoiseActive) {
    int16_t amplitude = WHITE_NOISE_AMPLITUDES[gWhiteNoiseVolume];
    for (size_t i = 0; i < WHITE_NOISE_CHUNK_SAMPLES; ++i) {
      uint32_t rnd = lfsr_next();
      int32_t signed16 = static_cast<int32_t>(rnd >> 16) - 32768;
      gWhiteNoiseBuf[i] = static_cast<int16_t>((signed16 * amplitude) / 32768);
    }
    writeRawPcm(gWhiteNoiseBuf, WHITE_NOISE_CHUNK_SAMPLES);
    return true;
  }

  if (gRainActive) {
    int16_t amplitude = RAIN_AMPLITUDES[gRainVolume];
    for (size_t i = 0; i < RAIN_CHUNK_SAMPLES; ++i) {
      uint32_t rnd = lfsr_next();
      float whiteNoise = (static_cast<float>(rnd >> 16) - 32768.0f) / 32768.0f;
      gPinkFilterState = PINK_ALPHA * whiteNoise + (1.0f - PINK_ALPHA) * gPinkFilterState;
      gRainBuf[i] = static_cast<int16_t>(gPinkFilterState * static_cast<float>(amplitude));
    }
    writeRawPcm(gRainBuf, RAIN_CHUNK_SAMPLES);
    return true;
  }

  return false;
}

static void ambientTask(void*) {
  for (;;) {
    if (!runAmbientChunk()) {
      vTaskDelay(pdMS_TO_TICKS(AMBIENT_IDLE_DELAY_MS));
    }
  }
}

// Build the blink tone buffer
static void buildBlinkBuffer() {
  for (size_t n = 0; n < BLINK_SAMPLES; ++n) {
    float env = 1.0f - (static_cast<float>(n) / static_cast<float>(BLINK_SAMPLES - 1));
    float phase = (kTwoPi * BLINK_FREQ_HZ * static_cast<float>(n)) / static_cast<float>(SAMPLE_RATE);
    gBlinkBuf[n] = static_cast<int16_t>(sinf(phase) * BLINK_PEAK * env);
  }
}

void begin() {
  if (gReady) return;

  // Allocate sound effect buffers in PSRAM
  gBlinkBuf      = static_cast<int16_t*>(heap_caps_malloc(BLINK_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gSwooshBuf     = static_cast<int16_t*>(heap_caps_malloc(SWOOSH_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gJitterBuf     = static_cast<int16_t*>(heap_caps_malloc(JITTER_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gHappyBuf      = static_cast<int16_t*>(heap_caps_malloc(HAPPY_PIP_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gSadBuf        = static_cast<int16_t*>(heap_caps_malloc(SAD_SIGH_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gChimeBuf      = static_cast<int16_t*>(heap_caps_malloc(CHIME_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gWhiteNoiseBuf = static_cast<int16_t*>(heap_caps_malloc(WHITE_NOISE_CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gRainBuf       = static_cast<int16_t*>(heap_caps_malloc(RAIN_CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (!gBlinkBuf || !gSwooshBuf || !gJitterBuf || !gHappyBuf || !gSadBuf || !gChimeBuf || !gWhiteNoiseBuf || !gRainBuf) {
    SoundLog::println("[Sound] Buffer alloc failed");
    return;
  }

  buildBlinkBuffer();

  if (!gAudioMutex) {
    gAudioMutex = xSemaphoreCreateMutex();
    if (!gAudioMutex) {
      SoundLog::println("[Sound] Audio mutex alloc failed");
      return;
    }
  }

  // Install raw I2S on I2S_NUM_1 for direct PCM writes
  installRawI2s();

  gReady = true;
  if (!gAmbientTaskHandle) {
    BaseType_t taskOk = xTaskCreatePinnedToCore(
        ambientTask,
        "sound_ambient",
        4096,
        nullptr,
        1,
        &gAmbientTaskHandle,
        0);
    if (taskOk != pdPASS) {
      gAmbientTaskHandle = nullptr;
      SoundLog::println("[Sound] Ambient task create failed (fallback to loop update)");
    }
  }
  SoundLog::printf("[Sound] Audio ready: I2S_NUM_1, pins BCLK=%d LRCK=%d DOUT=%d\n",
                   PIN_SPK_BCLK, PIN_SPK_LRCK, PIN_SPK_DOUT);
}

void update() {
  if (!gReady) return;
  // Fallback path if task creation failed.
  if (!gAmbientTaskHandle) {
    runAmbientChunk();
  }
}

void blinkClink() {
  if (gMuted || !gReady || gChatStreaming) return;
  VoiceDetector::suppressFor(MIC_SUPPRESS_MS);
  writeRawPcm(gBlinkBuf, BLINK_SAMPLES);
}

void eyeSwoosh(float strength) {
  if (gMuted || !gReady || gChatStreaming) return;
  VoiceDetector::suppressFor(MIC_SUPPRESS_MS);
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  float durationSec = SWOOSH_BASE_DURATION_SEC + (SWOOSH_MAX_DURATION_SEC - SWOOSH_BASE_DURATION_SEC) * strength;
  if (durationSec > SWOOSH_MAX_DURATION_SEC) durationSec = SWOOSH_MAX_DURATION_SEC;
  size_t samples = static_cast<size_t>(durationSec * SAMPLE_RATE);
  if (samples < 8) samples = 8;
  if (samples > SWOOSH_MAX_SAMPLES) samples = SWOOSH_MAX_SAMPLES;

  int16_t peak = static_cast<int16_t>(SWOOSH_PEAK_MIN + (SWOOSH_PEAK_MAX - SWOOSH_PEAK_MIN) * strength);

  for (size_t n = 0; n < samples; ++n) {
    float t = static_cast<float>(n) / static_cast<float>(samples - 1);
    float freq = SWOOSH_F_START_HZ + (SWOOSH_F_END_HZ - SWOOSH_F_START_HZ) * t;
    float phaseInc = (kTwoPi * freq) / SAMPLE_RATE;
    float phase = phaseInc * n;
    float s = sinf(phase);
    float env;
    float attackSamples = SAMPLE_RATE * 0.003f;
    if (attackSamples < 1.0f) attackSamples = 1.0f;
    if (n < static_cast<size_t>(attackSamples)) {
      env = static_cast<float>(n) / attackSamples;
    } else {
      float decayT = static_cast<float>(n - attackSamples) / static_cast<float>(samples - attackSamples);
      env = 1.0f - decayT;
    }
    gSwooshBuf[n] = static_cast<int16_t>(s * peak * env);
  }

  writeRawPcm(gSwooshBuf, samples);
}

void eyeJitter(float strength) {
  if (gMuted || !gReady || gChatStreaming) return;
  VoiceDetector::suppressFor(MIC_SUPPRESS_MS);
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  float durationSec = JITTER_BASE_DURATION_SEC + (JITTER_MAX_DURATION_SEC - JITTER_BASE_DURATION_SEC) * strength;
  if (durationSec > JITTER_MAX_DURATION_SEC) durationSec = JITTER_MAX_DURATION_SEC;
  size_t samples = static_cast<size_t>(durationSec * SAMPLE_RATE);
  if (samples < 8) samples = 8;
  if (samples > JITTER_MAX_SAMPLES) samples = JITTER_MAX_SAMPLES;

  int16_t peak = static_cast<int16_t>(JITTER_PEAK_MIN + (JITTER_PEAK_MAX - JITTER_PEAK_MIN) * strength);
  const int buzzFreq = 1200;
  float attackSamples = SAMPLE_RATE * 0.002f;
  if (attackSamples < 1.0f) attackSamples = 1.0f;

  for (size_t n = 0; n < samples; ++n) {
    float phase = (kTwoPi * buzzFreq * static_cast<float>(n)) / static_cast<float>(SAMPLE_RATE);
    float s = sinf(phase);
    float env;
    if (n < static_cast<size_t>(attackSamples)) {
      env = static_cast<float>(n) / attackSamples;
    } else {
      float decayT = static_cast<float>(n - attackSamples) / static_cast<float>(samples - attackSamples);
      env = 1.0f - decayT;
    }
    gJitterBuf[n] = static_cast<int16_t>(s * peak * env);
  }

  writeRawPcm(gJitterBuf, samples);
}

void happyPip(float strength) {
  if (gMuted || !gReady || gChatStreaming) return;
  VoiceDetector::suppressFor(MIC_SUPPRESS_MS);
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  size_t samples = HAPPY_PIP_SAMPLES;
  int16_t peak = static_cast<int16_t>(HAPPY_PIP_PEAK_MIN + (HAPPY_PIP_PEAK_MAX - HAPPY_PIP_PEAK_MIN) * strength);

  float freqL = HAPPY_PIP_BASE_HZ * (1.0f - HAPPY_PIP_DETUNE);
  float freqR = HAPPY_PIP_BASE_HZ * (1.0f + HAPPY_PIP_DETUNE);
  float incL = (kTwoPi * freqL) / SAMPLE_RATE;
  float incR = (kTwoPi * freqR) / SAMPLE_RATE;
  float phaseL = 0.0f, phaseR = 0.0f;

  for (size_t n = 0; n < samples; ++n) {
    float t = static_cast<float>(n) / static_cast<float>(samples - 1);
    float env = 1.0f - t;
    phaseL += incL;
    if (phaseL >= kTwoPi) phaseL -= kTwoPi;
    phaseR += incR;
    if (phaseR >= kTwoPi) phaseR -= kTwoPi;
    gHappyBuf[n * 2]     = static_cast<int16_t>(sinf(phaseL) * peak * env);
    gHappyBuf[n * 2 + 1] = static_cast<int16_t>(sinf(phaseR) * peak * env);
  }

  writeRawPcm(gHappyBuf, samples * 2);
}

void sadSigh(float strength) {
  if (gMuted || !gReady || gChatStreaming) return;
  VoiceDetector::suppressFor(MIC_SUPPRESS_MS + 150);
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;

  float durationSec = SAD_SIGH_BASE_DURATION_SEC + (SAD_SIGH_MAX_DURATION_SEC - SAD_SIGH_BASE_DURATION_SEC) * strength;
  if (durationSec > SAD_SIGH_MAX_DURATION_SEC) durationSec = SAD_SIGH_MAX_DURATION_SEC;
  size_t samples = static_cast<size_t>(durationSec * SAMPLE_RATE);
  if (samples < 8) samples = 8;
  if (samples > SAD_SIGH_MAX_SAMPLES) samples = SAD_SIGH_MAX_SAMPLES;

  int16_t peak = static_cast<int16_t>(SAD_SIGH_PEAK_MIN + (SAD_SIGH_PEAK_MAX - SAD_SIGH_PEAK_MIN) * strength);

  for (size_t n = 0; n < samples; ++n) {
    float t = static_cast<float>(n) / static_cast<float>(samples - 1);
    float freq = SAD_SIGH_F_START_HZ + (SAD_SIGH_F_END_HZ - SAD_SIGH_F_START_HZ) * t;
    float phaseInc = (kTwoPi * freq) / SAMPLE_RATE;
    float phase = phaseInc * n;
    float s = sinf(phase);
    float env;
    float attackSamples = SAMPLE_RATE * 0.020f;
    if (attackSamples < 1.0f) attackSamples = 1.0f;
    if (n < static_cast<size_t>(attackSamples)) {
      env = static_cast<float>(n) / attackSamples;
    } else {
      float decayT = static_cast<float>(n - attackSamples) / static_cast<float>(samples - attackSamples);
      env = (1.0f - decayT) * (1.0f - decayT * 0.5f);
    }
    gSadBuf[n] = static_cast<int16_t>(s * peak * env);
  }

  writeRawPcm(gSadBuf, samples);
}

void reminderChime() {
  if (gMuted || !gReady || gChatStreaming) return;
  VoiceDetector::suppressFor(MIC_SUPPRESS_MS + 350);

  size_t samples = CHIME_SAMPLES;
  size_t halfSamples = samples / 2;

  // Two-tone bell: first tone (lower) then second tone (higher)
  for (size_t n = 0; n < samples; ++n) {
    float t = static_cast<float>(n) / static_cast<float>(samples - 1);
    float env = expf(-4.0f * t);  // Exponential decay like a bell

    int freq = (n < halfSamples) ? CHIME_F1_HZ : CHIME_F2_HZ;
    float phase = (kTwoPi * freq * static_cast<float>(n)) / static_cast<float>(SAMPLE_RATE);
    float s = sinf(phase);

    gChimeBuf[n] = static_cast<int16_t>(s * CHIME_PEAK * env);
  }

  writeRawPcm(gChimeBuf, samples);
}

void mute(bool enabled) {
  gMuted = enabled;
  if (enabled) {
    gWhiteNoiseActive = false;
    gRainActive = false;
  }
}

void startWhiteNoise() {
  if (!gReady || gChatStreaming) return;
  gRainActive = false;
  gWhiteNoiseActive = true;
  VoiceDetector::suppressFor(60000);  // Suppress for 60s (white noise is continuous)
  SoundLog::println("[Sound] White noise started");
}

void stopWhiteNoise() {
  if (!gReady) return;
  gWhiteNoiseActive = false;
  // Voice detection re-enabled automatically when suppression expires
  if (lockAudio(I2S_WRITE_TIMEOUT_MS)) {
    if (gRawI2sInstalled) {
      i2s_zero_dma_buffer(SPK_PORT);
    }
    unlockAudio();
  }
  SoundLog::println("[Sound] White noise stopped");
}

bool isWhiteNoisePlaying() { return gWhiteNoiseActive; }

void whiteNoiseVolumeUp() {
  if (gWhiteNoiseVolume < 10) {
    gWhiteNoiseVolume++;
    SoundLog::printf("[Sound] White noise volume: %d\n", gWhiteNoiseVolume);
  }
}

void whiteNoiseVolumeDown() {
  if (gWhiteNoiseVolume > 0) {
    gWhiteNoiseVolume--;
    SoundLog::printf("[Sound] White noise volume: %d\n", gWhiteNoiseVolume);
  }
}

uint8_t getWhiteNoiseVolume() { return gWhiteNoiseVolume; }

void startRain() {
  if (!gReady || gChatStreaming) return;
  gWhiteNoiseActive = false;
  gPinkFilterState = 0.0f;
  gRainActive = true;
  VoiceDetector::suppressFor(60000);  // Suppress for 60s (rain is continuous)
  SoundLog::println("[Sound] Rain started");
}

void stopRain() {
  if (!gReady) return;
  gRainActive = false;
  // Voice detection re-enabled automatically when suppression expires
  if (lockAudio(I2S_WRITE_TIMEOUT_MS)) {
    if (gRawI2sInstalled) {
      i2s_zero_dma_buffer(SPK_PORT);
    }
    unlockAudio();
  }
  SoundLog::println("[Sound] Rain stopped");
}

bool isRainPlaying() { return gRainActive; }

void rainVolumeUp() {
  if (gRainVolume < 10) {
    gRainVolume++;
    SoundLog::printf("[Sound] Rain volume: %d\n", gRainVolume);
  }
}

void rainVolumeDown() {
  if (gRainVolume > 0) {
    gRainVolume--;
    SoundLog::printf("[Sound] Rain volume: %d\n", gRainVolume);
  }
}

uint8_t getRainVolume() { return gRainVolume; }

void setChatStreaming(bool streaming) {
  gChatStreaming = streaming;
  if (streaming) {
    // Stop ambient sounds when chat takes over
    gWhiteNoiseActive = false;
    gRainActive = false;
    // Uninstall raw I2S so Audio library can take over I2S_NUM_1.
    if (lockAudio(100)) {
      uninstallRawI2s();
      unlockAudio();
    }
  }
}

bool isChatStreaming() { return gChatStreaming; }

}  // namespace SoundSystem
