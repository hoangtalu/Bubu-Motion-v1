#pragma once

#include <Arduino.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>

struct PcmChunk {
    int16_t* pcm;
    size_t bytes;
};

namespace ChatAudio {
    void begin();               // Create queues
    void shutdown();            // Delete queues

    // Mic control (FreeRTOS task)
    void startMicTask();        // Create mic capture task
    void enableMic();           // Enable mic capture in task
    void disableMic();          // Disable mic capture (task sleeps)
    bool isMicEnabled();

    // PCM playback (FreeRTOS task — plays PCM via I2S)
    void startPlaybackTask();   // Create playback task (once)
    void startPlayback();       // Begin playback (install I2S, set flag)
    void stopPlayback();        // Stop playback, flush queue, release I2S
    bool isPlaybackActive();    // True if playback task is writing audio

    // Volume control
    void setVolume(uint8_t vol);

    // Queue access
    QueueHandle_t getMicQueue();         // Mic → processing (PcmChunk items)
    QueueHandle_t getPlaybackQueue();    // Processing → speaker (PcmChunk items)
    QueueHandle_t getSilentQueue();

    // Send PCM chunk to playback queue (caller relinquishes ownership of pcm pointer)
    void sendToPlayback(const PcmChunk& chunk);

    // VAD state
    uint16_t getSilenceThreshold();
    void setSilenceThreshold(uint16_t threshold);

    // Helpers
    void clearQueues();

    // PCM sending state
    bool isPcmSending();
    void setPcmSending(bool v);
}
