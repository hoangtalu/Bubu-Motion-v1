#include "reminder_system.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "logger.h"

DEFINE_MODULE_LOGGER(ReminderLog)

namespace ReminderSystem {

static std::vector<Reminder> sReminders;
static ReminderCallback sCallback = nullptr;
static uint32_t sNextId = 1;
static const char* REMINDER_FILE = "/reminders.json";

// Helper: find reminder by ID
static int findIndexById(uint32_t id) {
  for (size_t i = 0; i < sReminders.size(); ++i) {
    if (sReminders[i].id == id) return i;
  }
  return -1;
}

void begin() {
  if (!SPIFFS.begin(true)) {
    ReminderLog::println("[Reminder] SPIFFS mount failed");
    return;
  }
  load();
  ReminderLog::printf("[Reminder] Loaded %d reminders\n", sReminders.size());
}

void shutdown() {
  save();
  sReminders.clear();
}

bool load() {
  if (!SPIFFS.exists(REMINDER_FILE)) {
    ReminderLog::println("[Reminder] No saved reminders");
    return true;
  }

  File file = SPIFFS.open(REMINDER_FILE, "r");
  if (!file) {
    ReminderLog::println("[Reminder] Failed to open file");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    ReminderLog::printf("[Reminder] JSON parse error: %s\n", err.c_str());
    return false;
  }

  sReminders.clear();
  JsonArray arr = doc["reminders"].as<JsonArray>();
  for (JsonObject obj : arr) {
    Reminder r;
    r.id = obj["id"] | 0;
    r.title = obj["title"] | "";
    r.triggerTime = obj["trigger"] | 0;
    r.repeating = obj["repeat"] | false;
    r.repeatInterval = obj["interval"] | 0;
    r.active = obj["active"] | true;
    r.priority = obj["priority"] | 1;
    sReminders.push_back(r);
    if (r.id >= sNextId) sNextId = r.id + 1;
  }

  return true;
}

bool save() {
  JsonDocument doc;
  JsonArray arr = doc["reminders"].to<JsonArray>();

  for (const auto& r : sReminders) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = r.id;
    obj["title"] = r.title;
    obj["trigger"] = (uint32_t)r.triggerTime;
    obj["repeat"] = r.repeating;
    obj["interval"] = r.repeatInterval;
    obj["active"] = r.active;
    obj["priority"] = r.priority;
  }

  File file = SPIFFS.open(REMINDER_FILE, "w");
  if (!file) {
    ReminderLog::println("[Reminder] Failed to save");
    return false;
  }

  serializeJson(doc, file);
  file.close();
  return true;
}

void update(time_t currentTime) {
  if (currentTime == 0) return;  // No valid time yet

  bool needsSave = false;

  for (auto& r : sReminders) {
    if (!r.active) continue;
    if (currentTime < r.triggerTime) continue;

    // Reminder triggered!
    ReminderLog::printf("[Reminder] Triggered: %s\n", r.title.c_str());

    if (sCallback) {
      sCallback(r);
    }

    // Handle repeating reminders
    if (r.repeating && r.repeatInterval > 0) {
      // Reschedule for next occurrence
      r.triggerTime += r.repeatInterval * 24 * 60 * 60;  // Add days in seconds
      ReminderLog::printf("[Reminder] Rescheduled to: %u\n", (uint32_t)r.triggerTime);
      needsSave = true;
    } else {
      // One-time reminder, deactivate
      r.active = false;
      needsSave = true;
    }
  }

  // Only save if something actually changed
  if (needsSave) {
    save();
  }
}

uint32_t addReminder(const char* title, time_t triggerTime,
                     bool repeating, uint8_t interval, uint8_t priority) {
  Reminder r;
  r.id = sNextId++;
  r.title = title;
  r.triggerTime = triggerTime;
  r.repeating = repeating;
  r.repeatInterval = interval;
  r.active = true;
  r.priority = priority;

  sReminders.push_back(r);
  save();

  ReminderLog::printf("[Reminder] Added: %s (ID %u) at %u\n",
                      title, r.id, (uint32_t)triggerTime);
  return r.id;
}

bool removeReminder(uint32_t id) {
  int idx = findIndexById(id);
  if (idx < 0) return false;

  sReminders.erase(sReminders.begin() + idx);
  save();
  ReminderLog::printf("[Reminder] Removed ID %u\n", id);
  return true;
}

bool snoozeReminder(uint32_t id, uint32_t minutes) {
  int idx = findIndexById(id);
  if (idx < 0) return false;

  sReminders[idx].triggerTime += minutes * 60;
  save();
  ReminderLog::printf("[Reminder] Snoozed ID %u for %u minutes\n", id, minutes);
  return true;
}

void clearAll() {
  sReminders.clear();
  save();
  ReminderLog::println("[Reminder] Cleared all");
}

int getPendingCount() {
  int count = 0;
  time_t now = time(nullptr);
  for (const auto& r : sReminders) {
    if (r.active && r.triggerTime > now) count++;
  }
  return count;
}

Reminder* getNextReminder() {
  Reminder* next = nullptr;
  time_t now = time(nullptr);

  for (auto& r : sReminders) {
    if (!r.active || r.triggerTime <= now) continue;
    if (!next || r.triggerTime < next->triggerTime) {
      next = &r;
    }
  }
  return next;
}

std::vector<Reminder> getAllReminders() {
  return sReminders;
}

Reminder* getReminderById(uint32_t id) {
  int idx = findIndexById(id);
  if (idx < 0) return nullptr;
  return &sReminders[idx];
}

void setReminderCallback(ReminderCallback callback) {
  sCallback = callback;
}

}  // namespace ReminderSystem
