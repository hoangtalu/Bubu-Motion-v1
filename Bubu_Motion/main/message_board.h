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

// --- Tutor step cards (docs/tutor-mode-plan.md §6) -------------------------
//
// A step card holds the numbers a child is working with while Bubu guides by
// voice. Up to kMaxStepCards cards live per problem; the stack survives the
// card being closed and the conversation ending, and is dropped by EndSteps().

constexpr int kMaxStepCards = 4;

// Append one card and show it. `label` may be empty: the board then numbers the
// card itself ("bước N") so the model cannot get the count wrong. `note` may be
// empty. Returns false and fills *error when a slot is too wide for its band or
// the fonts have no glyph for a character — the caller reports that back to the
// model, which rewrites it shorter. Text is never clipped.
bool ShowStep(const std::string& label, const std::string& expr, const std::string& note,
              std::string* error);

// Drop the card stack and close the board if it is showing cards.
void EndSteps();

// Previous / next pushed card. Returns true if the swipe was consumed, which is
// only while cards are on screen.
bool HandleSwipe(bool forward);

}  // namespace MessageBoard
