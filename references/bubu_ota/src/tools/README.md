# Reminder & Note Tools

Voice-controlled reminder and note system for your AI pet companion.

## Features

### Reminders
- ✅ Set one-time or repeating reminders (daily/weekly/monthly)
- ✅ Voice control via Gemini ("remind me to X at Y time")
- ✅ Sound alert (gentle 2-tone chime)
- ✅ On-screen popup notification
- ✅ Persistent storage (survives reboots)
- ✅ Priority levels (low/medium/high)
- ✅ Snooze functionality

### Notes
- ✅ Create, update, delete notes
- ✅ Voice control via Gemini ("add a note: X")
- ✅ Categories (general/todo/important)
- ✅ Pin important notes
- ✅ Search by keyword
- ✅ On-screen scrollable display
- ✅ Persistent storage

## File Structure

```
src/tools/
├── reminder_system.h       - Reminder API
├── reminder_system.cpp     - Reminder implementation
├── note_system.h           - Note API
├── note_system.cpp         - Note implementation
├── tool_notification.h     - Notification display API
├── tool_notification.cpp   - Notification UI
├── tools_demo.cpp          - Demo/test code
├── TOOLS_INTEGRATION_GUIDE.md  - Integration instructions
└── README.md               - This file
```

## Quick Start

### 1. Add to platformio.ini

Already included - uses existing dependencies:
- ArduinoJson (for JSON storage)
- SPIFFS (for file persistence)
- LVGL (for UI)

### 2. Initialize in main.cpp

```cpp
#include "tools/reminder_system.h"
#include "tools/note_system.h"
#include "tools/tool_notification.h"

void setup() {
  // After DisplaySystem_init() and LVGL ready:
  ReminderSystem::begin();
  NoteSystem::begin();
  ToolNotification::begin();

  // Set callback for when reminders trigger
  ReminderSystem::setReminderCallback([](const ReminderSystem::Reminder& r) {
    ToolNotification::showReminder(r.title.c_str(), r.id);
  });
}

void loop() {
  // Check for triggered reminders
  time_t now = time(nullptr);
  ReminderSystem::update(now);
}
```

### 3. Add Voice Commands

See `TOOLS_INTEGRATION_GUIDE.md` for Gemini function calling integration.

## Storage

- **Location**: SPIFFS filesystem
- **Files**: `/reminders.json`, `/notes.json`
- **Auto-save**: On every add/update/delete
- **Format**: JSON (human-readable)

## Voice Examples

```
User: "Remind me to take medicine at 3pm"
User: "Set a daily reminder to exercise at 7am"
User: "Add a note: meeting with John next week"
User: "What are my reminders?"
User: "Show my shopping list note"
User: "Delete reminder 5"
```

## API Examples

### Reminders

```cpp
// Add a one-time reminder
time_t when = time(nullptr) + 3600;  // 1 hour from now
uint32_t id = ReminderSystem::addReminder("Take medicine", when);

// Add a daily reminder
ReminderSystem::addReminder("Morning exercise", when, true, 1);

// Snooze for 10 minutes
ReminderSystem::snoozeReminder(id, 10);

// Get next upcoming reminder
auto* next = ReminderSystem::getNextReminder();
if (next) {
  Serial.printf("Next: %s at %u\n", next->title.c_str(), next->triggerTime);
}
```

### Notes

```cpp
// Add a note
uint32_t id = NoteSystem::addNote("Shopping", "Milk, eggs, bread", 1);

// Update note content
NoteSystem::updateNote(id, "Milk, eggs, bread, butter");

// Pin a note
NoteSystem::togglePin(id);

// Search notes
auto results = NoteSystem::searchNotes("milk");
for (const auto& note : results) {
  Serial.println(note.title);
}

// Display on screen
auto* note = NoteSystem::getNoteById(id);
ToolNotification::showNote(note->title.c_str(), note->content.c_str());
```

## Sound Effect

**reminderChime()** - Gentle 2-tone bell
- Duration: 350ms
- Tones: 800Hz → 1000Hz
- Envelope: Exponential decay (bell-like)
- Auto-suppresses mic to prevent feedback

## Notification UI

**Reminder Popup:**
- Orange border with bell icon 🔔
- Centered, 200x100px
- Auto-dismiss after 5 seconds

**Note Display:**
- Blue border with note icon 📝
- Centered, 220x180px
- Scrollable content
- Auto-dismiss after 10 seconds

## Testing

Use `tools_demo.cpp` for quick testing:

```cpp
#include "tools/tools_demo.cpp"

void setup() {
  ToolsDemo::enableDemo();  // Creates test data
}

// Serial commands:
// "demo-notes" - list all notes
// "demo-reminders" - list all reminders
// "demo-show1" - show note ID 1 on screen
```

## Future Enhancements

Possible additions:
- [ ] Cloud sync (Firebase/MQTT)
- [ ] Geofence reminders (location-based)
- [ ] Voice playback of notes
- [ ] Handwritten note recognition
- [ ] Reminder history/completed log
- [ ] Note attachments/images
- [ ] Shared notes (multi-device)
- [ ] Smart suggestions (ML-based)

## Dependencies

- Time source: Uses existing NTP/RTC from display system
- Display: LVGL (already integrated)
- Storage: SPIFFS (ESP32 built-in)
- Sound: SoundSystem (extended with reminderChime)
- Voice: Gemini API (via chat system)

## License

Same as parent project.
