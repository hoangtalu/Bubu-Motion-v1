#pragma once

#include <lvgl.h>
#include <Arduino.h>

namespace ToolNotification {

// Show a reminder notification on screen with sound
void showReminder(const char* title, uint32_t reminderId);

// Show a note on screen (scrollable text)
void showNote(const char* title, const char* content);

// Dismiss current notification
void dismiss();

// Initialize (call after LVGL is ready)
void begin();

}  // namespace ToolNotification
