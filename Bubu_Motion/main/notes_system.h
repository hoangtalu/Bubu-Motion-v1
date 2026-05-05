#pragma once

#include <string>
#include <vector>

namespace NotesSystem {

static constexpr size_t kMaxEntries = 20;

struct NoteEntry {
    std::string key;
    std::string value;
};

void Begin();

// Save or update a note. Returns false if key/value is invalid or capacity is full.
bool Save(const std::string& key, const std::string& value, std::string* error_out = nullptr);

// Retrieves a note value by key. Returns true when found.
bool Get(const std::string& key, std::string* value_out);

// Returns a snapshot copy of all note entries.
std::vector<NoteEntry> List();

// Returns compact JSON: {"count":N,"entries":[{"key":"...","value":"..."}]}
std::string ListAsJson();

}  // namespace NotesSystem
