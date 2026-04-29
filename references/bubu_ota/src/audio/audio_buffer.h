#pragma once

#include <cstdint>
#include <cstring>

/**
 * @file audio_buffer.h
 * @brief Circular audio buffer for capturing and replaying microphone audio.
 *
 * Captures I2S microphone samples into a PSRAM circular buffer.
 * Supports recording, playback, and pitch-shifting of captured audio.
 */

namespace AudioBuffer {

// Configuration
static constexpr size_t BUFFER_DURATION_SECONDS = 3;           // 2-3 seconds of audio
static constexpr size_t SAMPLE_RATE = 16000;                   // 16 kHz
static constexpr size_t BYTES_PER_SAMPLE = 2;                  // 16-bit = 2 bytes
static constexpr size_t BUFFER_SIZE_BYTES =
    SAMPLE_RATE * BUFFER_DURATION_SECONDS * BYTES_PER_SAMPLE;  // 96 KB for 3 seconds
static constexpr size_t SAMPLE_COUNT = BUFFER_SIZE_BYTES / BYTES_PER_SAMPLE;

/**
 * Initialize the audio buffer.
 * Allocates circular buffer in PSRAM on first call.
 */
void begin();

/**
 * Start capturing audio samples into the buffer.
 * Resets write pointer and begins recording.
 */
void startCapture();

/**
 * Stop capturing audio samples.
 * Does not clear buffer—data can still be read.
 */
void stopCapture();

/**
 * Return true if currently capturing audio.
 */
bool isCapturing();

/**
 * Add audio samples to the circular buffer.
 * Called by microphone task to feed samples.
 *
 * @param samples 16-bit signed audio samples
 * @param sampleCount Number of samples to add
 */
void addSamples(const int16_t* samples, size_t sampleCount);

/**
 * Get a pointer to the captured audio buffer.
 * Returns the full buffer region that was recorded.
 *
 * @return Pointer to int16_t samples, or nullptr if no buffer
 */
int16_t* getBuffer();

/**
 * Get the number of samples captured.
 * Returns actual captured length, up to SAMPLE_COUNT.
 *
 * @return Number of int16_t samples in buffer
 */
size_t getSampleCount();

/**
 * Clear the buffer and reset capture state.
 */
void clear();

/**
 * Cleanup: free PSRAM buffer if allocated.
 */
void end();

}  // namespace AudioBuffer
