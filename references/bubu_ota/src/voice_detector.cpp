#include "voice_detector.h"
#include "board_pins.h"
#include <driver/i2s.h>

#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define VOICE_THRESHOLD 300   // RMS threshold for voice detection
#define VOICE_TIMEOUT_MS 3000 // Consider voice "heard" for 3 seconds

namespace VoiceDetector {
    // Internal state
    static bool _initialized = false;
    static bool _enabled = false;
    static bool _chatModeActive = false;
    static uint32_t _lastVoiceTime = 0;
    static uint32_t _suppressUntil = 0;

    bool begin() {
        // Initialize I2S_NUM_0 as RX-only for microphone input
        // Speaker uses separate I2S_NUM_1 port (managed by SoundSystem)
        i2s_config_t cfg = {
            .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate = SAMPLE_RATE,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 4,
            .dma_buf_len = 128,
            .use_apll = false,
            .tx_desc_auto_clear = false,
            .fixed_mclk = 0
        };

        i2s_pin_config_t pins = {
            .bck_io_num = PIN_MIC_SCK,
            .ws_io_num = PIN_MIC_WS,
            .data_out_num = -1,  // No TX on mic port
            .data_in_num = PIN_MIC_SD
        };

        esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            Serial.println("[VoiceDetector] I2S driver install failed!");
            return false;
        }
        if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
            Serial.println("[VoiceDetector] I2S pin config failed!");
            i2s_driver_uninstall(I2S_PORT);
            return false;
        }

        _initialized = true;
        Serial.println("[VoiceDetector] I2S mic initialized (I2S_NUM_0 RX-only)");
        return true;
    }

    void update() {
        if (!_initialized || !_enabled) return;

        // Skip processing if in chat mode or suppressed
        if (_chatModeActive || millis() < _suppressUntil) {
            return;
        }

        // Read I2S samples (non-blocking)
        int16_t samples[128];
        size_t bytes_read = 0;
        i2s_read(I2S_PORT, samples, sizeof(samples), &bytes_read, 0);

        if (bytes_read == 0) return;

        // Calculate RMS level
        int64_t sum = 0;
        int sample_count = bytes_read / 2;
        for (int i = 0; i < sample_count; i++) {
            int32_t val = samples[i];
            sum += val * val;
        }
        int32_t rms = (int32_t)sqrt(sum / sample_count);

        // Simple threshold detection
        if (rms > VOICE_THRESHOLD) {
            _lastVoiceTime = millis();
            // Uncomment for debugging:
            // Serial.printf("[VoiceDetector] Voice detected (RMS: %d)\n", rms);
        }
    }

    bool heardVoice() {
        if (!_initialized) return false;
        return (millis() - _lastVoiceTime) < VOICE_TIMEOUT_MS;
    }

    void suppressFor(uint32_t ms) {
        _suppressUntil = millis() + ms;
    }

    void setChatMode(bool active) {
        _chatModeActive = active;
    }

    void enable() {
        _enabled = true;
    }

    void disable() {
        _enabled = false;
    }
}
