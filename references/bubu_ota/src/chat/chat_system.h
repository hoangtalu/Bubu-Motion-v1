#pragma once

#include <Arduino.h>

// V2 chat states - native audio pipeline (PCM -> Gemini -> text, no WebSocket)
enum ChatState {
    CHAT_DISABLED,      // Feature is off
    CHAT_DISCONNECTED,  // No session established (waiting for first eye tap)
    CHAT_CONNECTING,    // Testing Gemini API connection - indicator: "..."
    CHAT_IDLE,          // Session live, waiting for user - indicator: "!!!"
    CHAT_LISTENING,     // Recording user speech - indicator: "?"
    CHAT_WAITING,       // Audio -> Gemini in progress - indicator: "..."
    CHAT_SPEAKING,      // (unused in text-only mode) - indicator: "???"
};

namespace ChatSystem {
    void begin();
    void update();      // Call from main loop every frame

    ChatState getState();

    // Eye-tap triggers (called from display_system touch handler)
    void connectFromMenu();  // First tap: test Gemini, show "!!!" when ready
    void startListening();   // Tap while IDLE: start recording
    void stopListening();    // Tap while LISTENING: stop mic, trigger processing
    void interrupt();        // Tap while WAITING/SPEAKING: abort
    void endSession();       // Long tap eye: clear history, go to DISCONNECTED

    // Lifecycle
    void enable();
    void disable();
    void exitChatScreen();

    bool isEnabled();
    bool isActive();    // True if LISTENING, WAITING, or SPEAKING

    // Legacy no-op callbacks (kept for compatibility with existing callers)
    void onSetupComplete();
    void onGeminiAudioStart();
    void onTurnComplete();
    void onDisconnected();
    void onVolume(uint8_t vol);

    // Legacy text send (no-op in V2 audio pipeline; kept for menu_system compat)
    void sendText(const char* text);

    // Testing functions
    void testMockingNow();  // Trigger mocking audio playback immediately (for testing)
}
