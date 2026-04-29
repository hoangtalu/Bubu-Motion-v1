#include "pitch_shifter.h"
#include <Arduino.h>
#include <cmath>

namespace PitchShifter {

size_t getOutputSampleCount(size_t inputCount, float pitchFactor) {
    if (pitchFactor <= 0.0f) return 0;
    // Output samples: input_count / pitch_factor
    // Higher pitch = fewer output samples (shorter duration)
    return static_cast<size_t>(inputCount / pitchFactor);
}

size_t pitchShiftAudio(
    const int16_t* inputSamples,
    size_t inputCount,
    float pitchFactor,
    int16_t* outputBuffer,
    size_t maxOutputSamples) {

    if (!inputSamples || !outputBuffer || inputCount == 0 || pitchFactor <= 0.0f) {
        return 0;
    }

    // Calculate expected output length
    size_t expectedOutputCount = getOutputSampleCount(inputCount, pitchFactor);
    if (expectedOutputCount > maxOutputSamples) {
        Serial.printf("PitchShifter: Output buffer too small (%zu > %zu)\n",
                      expectedOutputCount, maxOutputSamples);
        return 0;
    }

    /*
     * Algorithm: Sample-rate conversion with linear interpolation
     *
     * We read from input at rate R, write to output at rate R * pitchFactor
     * This effectively raises pitch by pitchFactor.
     *
     * Example for pitchFactor = 1.5:
     *   - Input indices: 0, 1, 2, 3, 4, 5, ...
     *   - Read positions: 0.0, 0.667, 1.333, 2.0, 2.667, 3.333, ...
     *   - Each read fetches between two input samples via linear interpolation
     *
     * Example for pitchFactor = 2.0 (simpler):
     *   - Read positions: 0.0, 0.5, 1.0, 1.5, 2.0, ...
     *   - Takes every other input sample (with interpolation for odd reads)
     */

    size_t outputIdx = 0;
    float readPos = 0.0f;  // Position in input buffer (can be fractional)
    const float readStep = 1.0f / pitchFactor;  // How much to advance per output sample

    while (outputIdx < expectedOutputCount && readPos < static_cast<float>(inputCount)) {
        // Get integer and fractional parts of read position
        int inputIdx = static_cast<int>(readPos);
        float fraction = readPos - inputIdx;

        // Boundary check
        if (inputIdx >= static_cast<int>(inputCount) - 1) {
            break;
        }

        // Linear interpolation between two adjacent input samples
        int16_t sample0 = inputSamples[inputIdx];
        int16_t sample1 = inputSamples[inputIdx + 1];

        // Interpolate: output = sample0 * (1 - fraction) + sample1 * fraction
        float interpolated = sample0 * (1.0f - fraction) + sample1 * fraction;

        // Clamp to int16_t range to prevent clipping
        if (interpolated > 32767.0f) {
            outputBuffer[outputIdx] = 32767;
        } else if (interpolated < -32768.0f) {
            outputBuffer[outputIdx] = -32768;
        } else {
            outputBuffer[outputIdx] = static_cast<int16_t>(interpolated);
        }

        outputIdx++;
        readPos += readStep;
    }

    if (outputIdx > 0) {
        Serial.printf("PitchShifter: %zu → %zu samples (%.2fx pitch)\n",
                      inputCount, outputIdx, pitchFactor);
    }

    return outputIdx;
}

}  // namespace PitchShifter
