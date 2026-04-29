#pragma once
#include <Arduino.h>

namespace ChatScreen {
    void begin();           // Create popup text box once (call from DisplaySystem_begin)
    void show();            // Show popup if cached text exists
    void hide();            // Hide popup only (chat session continues)
    bool isVisible();       // True when popup is visible

    void updateState();     // No-op (kept for compatibility)
    void showText(const char* text);   // Cache text (safe from any core)
    void flushPendingText();           // Flush cached text to LVGL — call from Core 1 only
    void clearText();       // Clear and hide popup
    bool hasUnreadText();   // Kept for compatibility

    // Touch handlers — return true if the event was consumed
    bool handleTap(uint16_t x, uint16_t y);
    bool handleLongPress(uint16_t x, uint16_t y);
}
