#pragma once
#include <Arduino.h>

/**
 * VoiceDetector - Minimal voice activity detection for chat auto-trigger
 *
 * Simplified replacement for pet_hearing system.
 * Provides only essential functionality:
 * - Simple threshold-based voice detection
 * - Mic suppression during speaker playback
 * - Chat mode blocking
 * - Enable/disable for test tools (default disabled)
 *
 * Removed from pet_hearing:
 * - Mood tracking (replaced by Gemini AI emotion control)
 * - Complex voice analysis (rhythm/variance/transient scores)
 * - Audio visualization data
 * - Callback system
 */

namespace VoiceDetector {
    /**
     * Initialize I2S microphone (I2S_NUM_0)
     * @return true if initialization successful
     */
    bool begin();

    /**
     * Sample microphone and detect voice activity
     * Call this regularly from main loop
     */
    void update();

    /**
     * Check if voice was detected recently
     * @return true if voice detected in last 3 seconds
     */
    bool heardVoice();

    /**
     * Suppress voice detection for specified duration
     * Used to prevent feedback during speaker playback
     * @param ms Duration in milliseconds to suppress detection
     */
    void suppressFor(uint32_t ms);

    /**
     * Enable/disable detection during chat sessions
     * @param active true to disable detection (chat active), false to enable
     */
    void setChatMode(bool active);

    /**
     * Enable/disable the detector; when disabled update() is a no-op.
     * Default: disabled at boot.
     */
    void enable();
    void disable();
}
