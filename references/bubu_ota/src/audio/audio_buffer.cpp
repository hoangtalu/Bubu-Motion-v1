#include "audio_buffer.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace AudioBuffer {

// Global buffer and state
static int16_t* sBuffer = nullptr;
static size_t sWritePos = 0;      // Current write position in circular buffer
static size_t sSampleCount = 0;   // Total samples recorded (before wraparound)
static bool sCapturing = false;
static SemaphoreHandle_t sBufferMutex = nullptr;

void begin() {
    if (sBuffer) return;  // Already initialized

    // Allocate circular buffer in PSRAM
    sBuffer = static_cast<int16_t*>(
        heap_caps_malloc(BUFFER_SIZE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!sBuffer) {
        // Fallback: try internal RAM
        sBuffer = static_cast<int16_t*>(
            heap_caps_malloc(BUFFER_SIZE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }

    if (!sBuffer) {
        Serial.println("ERROR: AudioBuffer allocation failed!");
        return;
    }

    sWritePos = 0;
    sSampleCount = 0;
    sCapturing = false;

    // Create mutex for thread-safe access
    if (!sBufferMutex) {
        sBufferMutex = xSemaphoreCreateMutex();
    }

    Serial.printf("AudioBuffer initialized: %zu bytes (%zu samples @ 16kHz)\n",
                  BUFFER_SIZE_BYTES, SAMPLE_COUNT);
}

void startCapture() {
    if (!sBuffer) return;

    xSemaphoreTake(sBufferMutex, portMAX_DELAY);
    sWritePos = 0;
    sSampleCount = 0;
    sCapturing = true;
    xSemaphoreGive(sBufferMutex);

    Serial.println("AudioBuffer: Capture started");
}

void stopCapture() {
    if (!sBuffer) return;

    xSemaphoreTake(sBufferMutex, portMAX_DELAY);
    sCapturing = false;
    xSemaphoreGive(sBufferMutex);

    Serial.printf("AudioBuffer: Capture stopped (%zu samples)\n", sSampleCount);
}

bool isCapturing() {
    if (!sBuffer) return false;
    xSemaphoreTake(sBufferMutex, portMAX_DELAY);
    bool result = sCapturing;
    xSemaphoreGive(sBufferMutex);
    return result;
}

void addSamples(const int16_t* samples, size_t sampleCount) {
    if (!sBuffer || !sCapturing) return;

    xSemaphoreTake(sBufferMutex, portMAX_DELAY);

    for (size_t i = 0; i < sampleCount; i++) {
        // Circular buffer: wrap around if we exceed SAMPLE_COUNT
        if (sWritePos >= SAMPLE_COUNT) {
            sWritePos = 0;  // Start overwriting from beginning
        }

        sBuffer[sWritePos] = samples[i];
        sWritePos++;

        // Track total samples (even after wraparound)
        if (sSampleCount < SAMPLE_COUNT) {
            sSampleCount++;
        }
    }

    xSemaphoreGive(sBufferMutex);
}

int16_t* getBuffer() {
    return sBuffer;
}

size_t getSampleCount() {
    if (!sBuffer) return 0;
    xSemaphoreTake(sBufferMutex, portMAX_DELAY);
    size_t count = sSampleCount;
    xSemaphoreGive(sBufferMutex);
    return count;
}

void clear() {
    if (!sBuffer) return;

    xSemaphoreTake(sBufferMutex, portMAX_DELAY);
    memset(sBuffer, 0, BUFFER_SIZE_BYTES);
    sWritePos = 0;
    sSampleCount = 0;
    sCapturing = false;
    xSemaphoreGive(sBufferMutex);

    Serial.println("AudioBuffer: Cleared");
}

void end() {
    if (!sBuffer) return;

    xSemaphoreTake(sBufferMutex, portMAX_DELAY);
    heap_caps_free(sBuffer);
    sBuffer = nullptr;
    xSemaphoreGive(sBufferMutex);

    if (sBufferMutex) {
        vSemaphoreDelete(sBufferMutex);
        sBufferMutex = nullptr;
    }

    Serial.println("AudioBuffer: Cleanup complete");
}

}  // namespace AudioBuffer
