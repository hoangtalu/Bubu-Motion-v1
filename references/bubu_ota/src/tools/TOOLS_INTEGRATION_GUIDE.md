# Reminder & Note System Integration Guide

## Overview
Voice-controlled reminder and note system with on-screen notifications and sound alerts.

## Files Created
- `src/tools/reminder_system.h/cpp` - Reminder management with SPIFFS storage
- `src/tools/note_system.h/cpp` - Note management with SPIFFS storage
- `src/tools/tool_notification.h/cpp` - On-screen notification display
- Sound effect: `SoundSystem::reminderChime()` - Gentle 2-tone bell

## Integration Steps

### 1. Initialize in main.cpp

```cpp
#include "tools/reminder_system.h"
#include "tools/note_system.h"
#include "tools/tool_notification.h"

// In setup(), after DisplaySystem_init():
void setup() {
  // ... existing init code ...
  DisplaySystem_init();

  // Initialize tool systems
  ReminderSystem::begin();
  NoteSystem::begin();
  ToolNotification::begin();

  // Set reminder callback
  ReminderSystem::setReminderCallback([](const ReminderSystem::Reminder& r) {
    ToolNotification::showReminder(r.title.c_str(), r.id);
  });
}

// In loop(), add reminder check:
void loop() {
  // ... existing loop code ...

  // Check for triggered reminders (use existing Clock time)
  time_t now = time(nullptr);
  ReminderSystem::update(now);

  // ... rest of loop ...
}
```

### 2. Add Voice Commands (Gemini Function Calling)

Add these function definitions to your Gemini system prompt or function schema:

```json
{
  "name": "set_reminder",
  "description": "Set a reminder for the user at a specific time",
  "parameters": {
    "type": "object",
    "properties": {
      "title": {"type": "string", "description": "What to remind about"},
      "timestamp": {"type": "integer", "description": "Unix timestamp when to trigger"},
      "repeating": {"type": "boolean", "description": "Whether reminder repeats"},
      "interval": {"type": "integer", "description": "Days between repeats (1=daily, 7=weekly)"}
    },
    "required": ["title", "timestamp"]
  }
}

{
  "name": "add_note",
  "description": "Save a note for the user",
  "parameters": {
    "type": "object",
    "properties": {
      "title": {"type": "string", "description": "Note title"},
      "content": {"type": "string", "description": "Note content"},
      "category": {"type": "integer", "description": "0=general, 1=todo, 2=important"}
    },
    "required": ["title", "content"]
  }
}

{
  "name": "list_reminders",
  "description": "Get all active reminders",
  "parameters": {"type": "object", "properties": {}}
}

{
  "name": "list_notes",
  "description": "Get all saved notes",
  "parameters": {"type": "object", "properties": {}}
}

{
  "name": "show_note",
  "description": "Display a specific note on screen",
  "parameters": {
    "type": "object",
    "properties": {
      "note_id": {"type": "integer", "description": "ID of note to display"}
    },
    "required": ["note_id"]
  }
}
```

### 3. Handle Function Calls in chat_system.cpp

```cpp
// In your Gemini message handler, add function call processing:

void handleFunctionCall(const JsonObject& func) {
  String name = func["name"];
  JsonObject args = func["args"];

  if (name == "set_reminder") {
    String title = args["title"] | "";
    time_t timestamp = args["timestamp"] | 0;
    bool repeating = args["repeating"] | false;
    uint8_t interval = args["interval"] | 0;

    uint32_t id = ReminderSystem::addReminder(
      title.c_str(), timestamp, repeating, interval
    );

    // Send success response to Gemini
    String response = "{\"success\": true, \"id\": " + String(id) + "}";
    sendFunctionResponse(func["call_id"], response);
  }

  else if (name == "add_note") {
    String title = args["title"] | "";
    String content = args["content"] | "";
    uint8_t category = args["category"] | 0;

    uint32_t id = NoteSystem::addNote(title.c_str(), content.c_str(), category);

    String response = "{\"success\": true, \"id\": " + String(id) + "}";
    sendFunctionResponse(func["call_id"], response);
  }

  else if (name == "list_reminders") {
    auto reminders = ReminderSystem::getAllReminders();
    JsonDocument doc;
    JsonArray arr = doc["reminders"].to<JsonArray>();

    for (const auto& r : reminders) {
      if (r.active) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = r.id;
        obj["title"] = r.title;
        obj["time"] = (uint32_t)r.triggerTime;
      }
    }

    String response;
    serializeJson(doc, response);
    sendFunctionResponse(func["call_id"], response);
  }

  else if (name == "list_notes") {
    auto notes = NoteSystem::getAllNotes();
    JsonDocument doc;
    JsonArray arr = doc["notes"].to<JsonArray>();

    for (const auto& n : notes) {
      JsonObject obj = arr.add<JsonObject>();
      obj["id"] = n.id;
      obj["title"] = n.title;
      obj["content"] = n.content.substring(0, 100);  // Preview only
    }

    String response;
    serializeJson(doc, response);
    sendFunctionResponse(func["call_id"], response);
  }

  else if (name == "show_note") {
    uint32_t noteId = args["note_id"] | 0;
    auto* note = NoteSystem::getNoteById(noteId);

    if (note) {
      ToolNotification::showNote(note->title.c_str(), note->content.c_str());
      sendFunctionResponse(func["call_id"], "{\"success\": true}");
    } else {
      sendFunctionResponse(func["call_id"], "{\"success\": false, \"error\": \"Note not found\"}");
    }
  }
}
```

## Usage Examples

### User Says:
- "Remind me to take medicine at 3pm"
- "Set a reminder for my meeting tomorrow at 10am"
- "Add a note: buy milk and eggs"
- "What are my reminders?"
- "Show me my notes"
- "Show note 5" (displays note on screen)

### Gemini Parses and Calls:
```
set_reminder(title="Take medicine", timestamp=1709629200)
set_reminder(title="Meeting", timestamp=1709715600)
add_note(title="Shopping", content="buy milk and eggs", category=1)
list_reminders()
list_notes()
show_note(note_id=5)
```

### System Response:
- Reminder triggers at specified time → plays chime + shows popup
- Notes display on screen with scrollable content
- Lists returned as JSON for Gemini to format naturally

## Display Behavior

**Reminder Notification:**
- Orange border, bell emoji 🔔
- Plays 2-tone chime sound
- Auto-dismisses after 5 seconds
- Centered popup overlay

**Note Display:**
- Blue border, note emoji 📝
- Scrollable content for long notes
- Auto-dismisses after 10 seconds
- Centered popup overlay

## Storage

- Reminders: `/reminders.json` in SPIFFS
- Notes: `/notes.json` in SPIFFS
- Auto-saves on add/update/delete
- Persists across reboots

## API Reference

See header files for full API documentation:
- `reminder_system.h` - Reminder CRUD + query functions
- `note_system.h` - Note CRUD + search functions
- `tool_notification.h` - Display functions
