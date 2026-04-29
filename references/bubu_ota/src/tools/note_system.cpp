#include "note_system.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "logger.h"

DEFINE_MODULE_LOGGER(NoteLog)

namespace NoteSystem {

static std::vector<Note> sNotes;
static uint32_t sNextId = 1;
static const char* NOTE_FILE = "/notes.json";

// Helper: find note by ID
static int findIndexById(uint32_t id) {
  for (size_t i = 0; i < sNotes.size(); ++i) {
    if (sNotes[i].id == id) return i;
  }
  return -1;
}

void begin() {
  if (!SPIFFS.begin(true)) {
    NoteLog::println("[Note] SPIFFS mount failed");
    return;
  }
  load();
  NoteLog::printf("[Note] Loaded %d notes\n", sNotes.size());
}

void shutdown() {
  save();
  sNotes.clear();
}

bool load() {
  if (!SPIFFS.exists(NOTE_FILE)) {
    NoteLog::println("[Note] No saved notes");
    return true;
  }

  File file = SPIFFS.open(NOTE_FILE, "r");
  if (!file) {
    NoteLog::println("[Note] Failed to open file");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    NoteLog::printf("[Note] JSON parse error: %s\n", err.c_str());
    return false;
  }

  sNotes.clear();
  JsonArray arr = doc["notes"].as<JsonArray>();
  for (JsonObject obj : arr) {
    Note n;
    n.id = obj["id"] | 0;
    n.title = obj["title"] | "";
    n.content = obj["content"] | "";
    n.created = obj["created"] | 0;
    n.modified = obj["modified"] | 0;
    n.category = obj["category"] | 0;
    n.pinned = obj["pinned"] | false;
    sNotes.push_back(n);
    if (n.id >= sNextId) sNextId = n.id + 1;
  }

  return true;
}

bool save() {
  JsonDocument doc;
  JsonArray arr = doc["notes"].to<JsonArray>();

  for (const auto& n : sNotes) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = n.id;
    obj["title"] = n.title;
    obj["content"] = n.content;
    obj["created"] = (uint32_t)n.created;
    obj["modified"] = (uint32_t)n.modified;
    obj["category"] = n.category;
    obj["pinned"] = n.pinned;
  }

  File file = SPIFFS.open(NOTE_FILE, "w");
  if (!file) {
    NoteLog::println("[Note] Failed to save");
    return false;
  }

  serializeJson(doc, file);
  file.close();
  return true;
}

uint32_t addNote(const char* title, const char* content, uint8_t category) {
  Note n;
  n.id = sNextId++;
  n.title = title;
  n.content = content;
  n.created = time(nullptr);
  n.modified = n.created;
  n.category = category;
  n.pinned = false;

  sNotes.push_back(n);
  save();

  NoteLog::printf("[Note] Added: %s (ID %u)\n", title, n.id);
  return n.id;
}

bool updateNote(uint32_t id, const char* content) {
  int idx = findIndexById(id);
  if (idx < 0) return false;

  sNotes[idx].content = content;
  sNotes[idx].modified = time(nullptr);
  save();

  NoteLog::printf("[Note] Updated ID %u\n", id);
  return true;
}

bool removeNote(uint32_t id) {
  int idx = findIndexById(id);
  if (idx < 0) return false;

  sNotes.erase(sNotes.begin() + idx);
  save();

  NoteLog::printf("[Note] Removed ID %u\n", id);
  return true;
}

bool togglePin(uint32_t id) {
  int idx = findIndexById(id);
  if (idx < 0) return false;

  sNotes[idx].pinned = !sNotes[idx].pinned;
  save();

  NoteLog::printf("[Note] Toggled pin ID %u: %s\n", id,
                  sNotes[idx].pinned ? "pinned" : "unpinned");
  return true;
}

void clearAll() {
  sNotes.clear();
  save();
  NoteLog::println("[Note] Cleared all");
}

int getCount() {
  return sNotes.size();
}

Note* getNoteById(uint32_t id) {
  int idx = findIndexById(id);
  if (idx < 0) return nullptr;
  return &sNotes[idx];
}

std::vector<Note> getAllNotes() {
  return sNotes;
}

std::vector<Note> getPinnedNotes() {
  std::vector<Note> pinned;
  for (const auto& n : sNotes) {
    if (n.pinned) pinned.push_back(n);
  }
  return pinned;
}

std::vector<Note> searchNotes(const char* query) {
  std::vector<Note> results;
  String q = String(query);
  q.toLowerCase();

  for (const auto& n : sNotes) {
    String title = n.title;
    String content = n.content;
    title.toLowerCase();
    content.toLowerCase();

    if (title.indexOf(q) >= 0 || content.indexOf(q) >= 0) {
      results.push_back(n);
    }
  }

  return results;
}

}  // namespace NoteSystem
