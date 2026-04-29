#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>

namespace ReminderSystem {

struct Reminder {
  uint32_t id;
  String title;
  time_t triggerTime;     // Unix timestamp (UTC)
  bool repeating;
  uint8_t repeatInterval; // 0=none, 1=daily, 7=weekly, 30=monthly
  bool active;
  uint8_t priority;       // 0=low, 1=medium, 2=high
};

// Callback type: called when a reminder triggers
typedef std::function<void(const Reminder&)> ReminderCallback;

// Core functions
void begin();                    // Load reminders from SPIFFS
void update(time_t currentTime); // Check for triggered reminders (call in main loop)
void shutdown();                 // Save and cleanup

// Reminder management
uint32_t addReminder(const char* title, time_t triggerTime,
                     bool repeating = false, uint8_t interval = 0,
                     uint8_t priority = 1);
bool removeReminder(uint32_t id);
bool snoozeReminder(uint32_t id, uint32_t minutes);  // Postpone by N minutes
void clearAll();

// Query
int getPendingCount();           // Count of active, future reminders
Reminder* getNextReminder();     // Next upcoming reminder (or nullptr)
std::vector<Reminder> getAllReminders();
Reminder* getReminderById(uint32_t id);

// Notification callback
void setReminderCallback(ReminderCallback callback);

// Storage
bool save();  // Save to SPIFFS manually (auto-saves on add/remove)
bool load();  // Load from SPIFFS manually

}  // namespace ReminderSystem
