#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @file pitch_shifter.h
 * @brief Real-time pitch-shifting for audio samples.
 *
 * Implements sample-rate conversion to raise pitch (mocking effect).
 * Uses simple linear interpolation for quality without excessive CPU load.
 */

namespace PitchShifter {

/**
 * Pitch-shift audio samples using sample-rate conversion with interpolation.
 *
 * Higher pitch factor = higher pitch (mocking effect)
 * - pitchFactor 1.5 = 1.5x pitch (chipmunk-like)
 * - pitchFactor 2.0 = 2.0x pitch (very high)
 *
 * @param inputSamples 16-bit signed input audio
 * @param inputCount Number of input samples
 * @param pitchFactor Pitch scaling (1.5 to 2.0 recommended)
 * @param outputBuffer Where to write pitch-shifted samples (must be pre-allocated)
 * @param maxOutputSamples Maximum samples that can fit in outputBuffer
 *
 * @return Number of samples written to outputBuffer, or 0 on error
 */
size_t pitchShiftAudio(
    const int16_t* inputSamples,
    size_t inputCount,
    float pitchFactor,
    int16_t* outputBuffer,
    size_t maxOutputSamples);

/**
 * Calculate expected output sample count for given input and pitch factor.
 * Useful for pre-allocating output buffer.
 *
 * @param inputCount Number of input samples
 * @param pitchFactor Pitch scaling (1.5 to 2.0)
 * @return Expected number of output samples
 */
size_t getOutputSampleCount(size_t inputCount, float pitchFactor);

}  // namespace PitchShifter
