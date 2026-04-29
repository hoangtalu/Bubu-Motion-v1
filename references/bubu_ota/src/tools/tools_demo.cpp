// Demo/test file for reminder and note systems
// Include this in your project to test the tools

#include "reminder_system.h"
#include "note_system.h"
#include "tool_notification.h"
#include <Arduino.h>

namespace ToolsDemo {

// Demo: Set up some test reminders and notes
void setupDemoData() {
  // Clear existing data
  ReminderSystem::clearAll();
  NoteSystem::clearAll();

  // Add a reminder for 10 seconds from now
  time_t now = time(nullptr);
  ReminderSystem::addReminder("Test reminder!", now + 10, false, 0, 1);

  // Add a daily reminder
  ReminderSystem::addReminder("Daily task", now + 30, true, 1, 2);

  // Add some notes
  NoteSystem::addNote("Shopping List", "Milk, eggs, bread, butter", 1);
  NoteSystem::addNote("Ideas", "Build a robot pet with AI voice assistant", 0);
  NoteSystem::addNote("Important", "Remember to backup the code!", 2);

  Serial.println("[Demo] Test data created");
  Serial.printf("[Demo] Reminders: %d\n", ReminderSystem::getPendingCount());
  Serial.printf("[Demo] Notes: %d\n", NoteSystem::getCount());
}

// Demo: List all reminders
void listReminders() {
  auto reminders = ReminderSystem::getAllReminders();
  Serial.printf("\n=== Reminders (%d) ===\n", reminders.size());

  for (const auto& r : reminders) {
    if (r.active) {
      Serial.printf("ID %u: %s (at %u) %s\n",
                    r.id, r.title.c_str(), (uint32_t)r.triggerTime,
                    r.repeating ? "[REPEAT]" : "");
    }
  }
}

// Demo: List all notes
void listNotes() {
  auto notes = NoteSystem::getAllNotes();
  Serial.printf("\n=== Notes (%d) ===\n", notes.size());

  for (const auto& n : notes) {
    Serial.printf("ID %u: %s\n", n.id, n.title.c_str());
    Serial.printf("  Content: %s\n", n.content.c_str());
    Serial.printf("  Category: %d, Pinned: %s\n",
                  n.category, n.pinned ? "YES" : "NO");
  }
}

// Demo: Show a note on screen
void showNoteDemo(uint32_t noteId) {
  auto* note = NoteSystem::getNoteById(noteId);
  if (note) {
    ToolNotification::showNote(note->title.c_str(), note->content.c_str());
    Serial.printf("[Demo] Showing note: %s\n", note->title.c_str());
  } else {
    Serial.printf("[Demo] Note %u not found\n", noteId);
  }
}

// Call this in your setup() to enable demo mode
void enableDemo() {
  Serial.println("\n=== TOOLS DEMO MODE ===");
  setupDemoData();
  listReminders();
  listNotes();

  // Set reminder callback
  ReminderSystem::setReminderCallback([](const ReminderSystem::Reminder& r) {
    Serial.printf("[Demo] REMINDER TRIGGERED: %s\n", r.title.c_str());
    ToolNotification::showReminder(r.title.c_str(), r.id);
  });
}

}  // namespace ToolsDemo

/*
  USAGE IN main.cpp:

  #include "tools/tools_demo.cpp"

  void setup() {
    // ... your normal setup ...
    ToolsDemo::enableDemo();  // Enable demo mode
  }

  void loop() {
    // ... your normal loop ...
    time_t now = time(nullptr);
    ReminderSystem::update(now);  // Check reminders
  }

  // Serial commands (in processSerial()):
  if (cmd == "demo-notes") ToolsDemo::listNotes();
  if (cmd == "demo-reminders") ToolsDemo::listReminders();
  if (cmd == "demo-show1") ToolsDemo::showNoteDemo(1);
*/
