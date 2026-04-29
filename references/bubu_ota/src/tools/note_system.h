#pragma once

#include <Arduino.h>
#include <vector>

namespace NoteSystem {

struct Note {
  uint32_t id;
  String title;
  String content;
  time_t created;
  time_t modified;
  uint8_t category;  // 0=general, 1=todo, 2=important
  bool pinned;
};

// Core functions
void begin();        // Load notes from SPIFFS
void shutdown();     // Save and cleanup

// Note management
uint32_t addNote(const char* title, const char* content,
                 uint8_t category = 0);
bool updateNote(uint32_t id, const char* content);
bool removeNote(uint32_t id);
bool togglePin(uint32_t id);
void clearAll();

// Query
int getCount();
Note* getNoteById(uint32_t id);
std::vector<Note> getAllNotes();
std::vector<Note> getPinnedNotes();
std::vector<Note> searchNotes(const char* query);  // Search title/content

// Storage
bool save();  // Save to SPIFFS manually (auto-saves on add/remove/update)
bool load();  // Load from SPIFFS manually

}  // namespace NoteSystem
