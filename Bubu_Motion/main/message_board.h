#pragma once

#include <cstdint>
#include <string>

class Display;

namespace MessageBoard {

// Initialize one-time LVGL board overlay.
void Begin(Display* display);

// Open generic board text.
void Open(const std::string& title, const std::string& body, int32_t reminder_id = 0);

// Open reminder board (tap is used to dismiss the firing reminder).
void OpenReminder(int32_t reminder_id, const std::string& message, int hour, int minute);

// Open bind-required board. Payload is persisted until ClearBindCode() is called.
void OpenBindCode(const std::string& message, const std::string& code);

// Clear bind-required state and stop auto-reopen loop.
void ClearBindCode();

// Close board.
void Close();

// Board visibility.
bool IsOpen();

// Handle tap. Returns true if board consumed the tap.
// When it returns true and dismissed_reminder_id > 0, caller should dismiss that reminder.
bool HandleTap(uint16_t x, uint16_t y, int32_t* dismissed_reminder_id);

}  // namespace MessageBoard
