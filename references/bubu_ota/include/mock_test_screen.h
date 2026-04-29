#pragma once
#include <Arduino.h>
#include <cstdint>

/**
 * @file mock_test_screen.h
 * @brief Interactive mock voice testing interface
 *
 * Provides a tap-to-record and playback interface for testing voice mocking.
 * - TAP white circle → start recording (turns yellow)
 * - TAP yellow circle → stop recording, apply pitch-shift, start playback (turns green)
 * - Auto-return to white when playback completes
 */

namespace MockTestScreen {
    enum MockState {
        MOCK_WHITE,     // Ready/idle
        MOCK_YELLOW,    // Recording
        MOCK_GREEN      // Playback
    };

    /**
     * Initialize the mock test screen UI.
     * Creates the circle and sets initial state to WHITE.
     */
    void begin();

    /**
     * Open/show the mock test screen (called when user selects TEST MOCK from menu).
     */
    void open();

    /**
     * Close/hide the mock test screen and return to menu.
     */
    void close();

    /**
     * Check if mock test screen is currently open.
     */
    bool isOpen();

    /**
     * Update mock test state machine each frame.
     * Handles:
     * - Playback completion detection (GREEN → WHITE transition)
     * - Recording duration limits
     * - Audio processing
     * Call this from the main loop every frame.
     */
    void update();

    /**
     * Handle touch input on the mock test circle.
     * Returns true if the event was consumed.
     */
    bool handleTap(uint16_t x, uint16_t y);

    /**
     * Get current mock state (for display or testing).
     */
    MockState getState();
}
