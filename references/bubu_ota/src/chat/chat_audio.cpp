#include "chat_audio.h"
#include "chat_system.h"
#include "../audio/audio_buffer.h"
#include "../sound/sound_system.h"
#include "board_pins.h"

#include <atomic>
#include <driver/i2s.h>

// I2S mic configuration (shares I2S_NUM_0 with PetHearing)
#define MIC_PORT        I2S_NUM_0
#define MIC_SAMPLE_RATE 16000
#define MIC_DMA_BUF_LEN 512
#define MIC_READ_MULT   3       // Read MIC_DMA_BUF_LEN * 3 samples per iteration

// Speaker port for PCM playback
// Gemini Live API outputs 24kHz PCM — must match here or audio plays too slow
#define SPK_PORT        I2S_NUM_1
#define SPK_SAMPLE_RATE 24000

namespace ChatAudio {

// Queues
static QueueHandle_t sMicToServer   = nullptr;   // Mic chunks → processing task
static QueueHandle_t sPlaybackQueue = nullptr;   // Audio chunks → speaker
static QueueHandle_t sTempSilent    = nullptr;

// State
static std::atomic<bool> sMicEnabled{false};
static std::atomic<bool> sPcmSending{false};
static std::atomic<bool> sPlaybackRunning{false};
static uint16_t sSilenceThreshold = 300;
static uint32_t sTimeDetectSleep  = 0;
static uint8_t  sVolume           = 10;  // 0-21
static bool     sMicI2sInstalled  = false;

// Task handles
static TaskHandle_t sMicTaskHandle      = nullptr;
static TaskHandle_t sPlaybackTaskHandle = nullptr;

// Playback I2S state
static bool sPlaybackI2sInstalled = false;

// Helper: clear a queue and free all PCM chunks
static void clearQueueAndFree(QueueHandle_t q) {
    if (!q) return;
    PcmChunk chunk;
    while (xQueueReceive(q, &chunk, 0) == pdTRUE) {
        if (chunk.pcm) heap_caps_free(chunk.pcm);
    }
}

// Install I2S driver on I2S_NUM_0 for mic capture (RX only)
static void installMicI2s() {
    if (sMicI2sInstalled) return;

    i2s_config_t cfg = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = MIC_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num = PIN_MIC_SCK,
        .ws_io_num = PIN_MIC_WS,
        .data_out_num = -1,
        .data_in_num = PIN_MIC_SD
    };

    esp_err_t err = i2s_driver_install(MIC_PORT, &cfg, 0, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.println("[ChatAudio] Mic I2S install failed");
        return;
    }
    err = i2s_set_pin(MIC_PORT, &pins);
    if (err != ESP_OK) {
        Serial.println("[ChatAudio] Mic I2S pin config failed");
        if (err != ESP_ERR_INVALID_STATE) {
            i2s_driver_uninstall(MIC_PORT);
        }
        return;
    }

    sMicI2sInstalled = true;
    i2s_zero_dma_buffer(MIC_PORT);
    Serial.println("[ChatAudio] Mic I2S ready");
}

// Install I2S driver on I2S_NUM_1 for PCM playback
static void installPlaybackI2s() {
    if (sPlaybackI2sInstalled) return;

    i2s_config_t cfg = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SPK_SAMPLE_RATE,
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
        sPlaybackI2sInstalled = true;
    }
}

// Uninstall playback I2S driver
static void uninstallPlaybackI2s() {
    if (!sPlaybackI2sInstalled) return;
    i2s_driver_uninstall(SPK_PORT);
    sPlaybackI2sInstalled = false;
}

// Apply software volume to PCM samples
static void applyVolume(int16_t* pcm, size_t sampleCount) {
    if (sVolume == 0) {
        memset(pcm, 0, sampleCount * sizeof(int16_t));
        return;
    }
    if (sVolume >= 21) return;
    float scale = (float)sVolume / 21.0f;
    for (size_t i = 0; i < sampleCount; i++) {
        pcm[i] = (int16_t)(pcm[i] * scale);
    }
}

// ------------------------------------------------------------------
// Mic capture task (runs on Core 1, priority 3)
// ------------------------------------------------------------------
static void micTaskFunc(void* param) {
    int16_t readBuf[MIC_DMA_BUF_LEN * MIC_READ_MULT];
    size_t bytesRead;

    while (true) {
        if (!sMicEnabled.load()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Read from I2S mic (16-bit, 16kHz, mono)
        esp_err_t err = i2s_read(MIC_PORT, readBuf, sizeof(readBuf), &bytesRead, portMAX_DELAY);
        if (err != ESP_OK || bytesRead == 0) continue;

        if (AudioBuffer::isCapturing()) {
            AudioBuffer::addSamples(readBuf, bytesRead / sizeof(int16_t));
        }

        // Allocate chunk in PSRAM
        int16_t* pcm = static_cast<int16_t*>(heap_caps_malloc(bytesRead, MALLOC_CAP_SPIRAM));
        if (!pcm) continue;
        memcpy(pcm, readBuf, bytesRead);

        PcmChunk chunk = {pcm, bytesRead};

        if (xQueueSend(sMicToServer, &chunk, 0) != pdTRUE) {
            heap_caps_free(pcm);  // Queue full, drop chunk
        }
    }
}

// ------------------------------------------------------------------
// PCM playback task (runs on Core 1, priority 2)
// Reads PcmChunk items from sPlaybackQueue and writes them to I2S speaker.
// ------------------------------------------------------------------
static void playbackTaskFunc(void* param) {
    while (true) {
        if (!sPlaybackRunning.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!sPlaybackQueue) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        PcmChunk chunk;
        if (xQueueReceive(sPlaybackQueue, &chunk, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (chunk.pcm && chunk.bytes > 0) {
                size_t sampleCount = chunk.bytes / sizeof(int16_t);
                applyVolume(chunk.pcm, sampleCount);

                size_t offset = 0;
                while (offset < chunk.bytes && sPlaybackRunning.load()) {
                    size_t toWrite = chunk.bytes - offset;
                    if (toWrite > 1024) toWrite = 1024;
                    size_t written = 0;
                    i2s_write(SPK_PORT, (uint8_t*)chunk.pcm + offset,
                              toWrite, &written, pdMS_TO_TICKS(100));
                    offset += written;
                }
                heap_caps_free(chunk.pcm);
            }
        }
    }
}

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

void begin() {
    sMicToServer   = xQueueCreate(64,  sizeof(PcmChunk));
    sPlaybackQueue = xQueueCreate(128, sizeof(PcmChunk));
    sTempSilent    = xQueueCreate(16,  sizeof(PcmChunk));

    installMicI2s();
}

void shutdown() {
    disableMic();
    stopPlayback();
    clearQueues();

    if (sMicToServer)   { vQueueDelete(sMicToServer);   sMicToServer   = nullptr; }
    if (sPlaybackQueue) { vQueueDelete(sPlaybackQueue); sPlaybackQueue = nullptr; }
    if (sTempSilent)    { vQueueDelete(sTempSilent);    sTempSilent    = nullptr; }
}

void startMicTask() {
    if (sMicTaskHandle) return;
    installMicI2s();
    xTaskCreatePinnedToCore(micTaskFunc, "chat_mic", 1024 * 16,
                            nullptr, 3, &sMicTaskHandle, 1);
}

void enableMic() {
    clearQueues();
    i2s_zero_dma_buffer(MIC_PORT);
    sMicEnabled.store(true);
    sPcmSending.store(false);
    sTimeDetectSleep = millis();
}

void disableMic() {
    sMicEnabled.store(false);
    sPcmSending.store(false);
}

bool isMicEnabled() {
    return sMicEnabled.load();
}

void startPlaybackTask() {
    if (sPlaybackTaskHandle) return;
    xTaskCreatePinnedToCore(playbackTaskFunc, "chat_play", 1024 * 8,
                            nullptr, 2, &sPlaybackTaskHandle, 1);
}

void startPlayback() {
    SoundSystem::setChatStreaming(true);  // Stops ambient sounds, frees I2S_NUM_1
    installPlaybackI2s();
    sPlaybackRunning.store(true);
}

void stopPlayback() {
    sPlaybackRunning.store(false);

    // Flush remaining playback chunks
    clearQueueAndFree(sPlaybackQueue);

    uninstallPlaybackI2s();
    SoundSystem::setChatStreaming(false);
}

bool isPlaybackActive() {
    if (!sPlaybackRunning.load()) return false;
    if (!sPlaybackQueue) return false;
    return uxQueueMessagesWaiting(sPlaybackQueue) > 0;
}

void setVolume(uint8_t vol) {
    sVolume = vol;
}

QueueHandle_t getMicQueue()      { return sMicToServer; }
QueueHandle_t getPlaybackQueue() { return sPlaybackQueue; }
QueueHandle_t getSilentQueue()   { return sTempSilent; }

uint16_t getSilenceThreshold()              { return sSilenceThreshold; }
void setSilenceThreshold(uint16_t threshold){ sSilenceThreshold = threshold; }

void clearQueues() {
    clearQueueAndFree(sMicToServer);
    clearQueueAndFree(sPlaybackQueue);
    clearQueueAndFree(sTempSilent);
}

bool isPcmSending()        { return sPcmSending.load(); }
void setPcmSending(bool v) { sPcmSending.store(v); }

void sendToPlayback(const PcmChunk& chunk) {
    if (!sPlaybackQueue) {
        Serial.println("[ChatAudio] ERROR: Playback queue not initialized");
        if (chunk.pcm) heap_caps_free(chunk.pcm);
        return;
    }

    // Ensure playback task is running
    if (!isPlaybackActive()) {
        startPlayback();
    }

    // Send chunk to playback queue
    // xQueueSend will fail silently if queue is full (non-blocking)
    if (xQueueSend(sPlaybackQueue, &chunk, 0) != pdTRUE) {
        Serial.println("[ChatAudio] WARNING: Playback queue full, dropping chunk");
        if (chunk.pcm) heap_caps_free(chunk.pcm);
    }
}

}  // namespace ChatAudio
