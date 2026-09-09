#include "notes_system.h"

#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>

#include <cJSON.h>
#include <esp_log.h>

namespace {

static const char* TAG = "NotesSystem";
static const char* kNamespace = "notes";

std::vector<NotesSystem::NoteEntry> s_entries;
bool s_initialized = false;
std::mutex s_mutex;

std::string Trim(std::string value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string NormalizeKey(const std::string& raw_key) {
    std::string key = Trim(raw_key);
    std::string normalized;
    normalized.reserve(key.size());

    for (unsigned char ch : key) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == '_' || ch == '-' || ch == ' ' || ch == '.') {
            normalized.push_back('_');
        }
    }

    // Collapse repeated underscores.
    std::string compact;
    compact.reserve(normalized.size());
    bool prev_underscore = false;
    for (char ch : normalized) {
        if (ch == '_') {
            if (!prev_underscore) {
                compact.push_back(ch);
            }
            prev_underscore = true;
        } else {
            compact.push_back(ch);
            prev_underscore = false;
        }
    }

    // Remove leading/trailing underscores.
    while (!compact.empty() && compact.front() == '_') {
        compact.erase(compact.begin());
    }
    while (!compact.empty() && compact.back() == '_') {
        compact.pop_back();
    }

    return compact;
}

std::string SlotKey(char prefix, size_t index) {
    return std::string(1, prefix) + std::to_string(index);
}

void PersistLocked() {
    Settings settings(kNamespace, true);

    // Write the slots before the count, so a write that fails partway leaves a
    // smaller-but-consistent list rather than a count pointing at empty slots.
    size_t written = 0;
    for (size_t i = 0; i < s_entries.size(); ++i) {
        if (settings.SetString(SlotKey('k', i), s_entries[i].key) != ESP_OK ||
            settings.SetString(SlotKey('v', i), s_entries[i].value) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to persist note %zu of %zu; truncating stored list",
                     i + 1, s_entries.size());
            break;
        }
        ++written;
    }

    settings.SetInt("count", static_cast<int32_t>(written));
    settings.SetBool("has", written != 0);

    // Clear old slots that are no longer used.
    for (size_t i = written; i < NotesSystem::kMaxEntries; ++i) {
        settings.EraseKey(SlotKey('k', i));
        settings.EraseKey(SlotKey('v', i));
    }
}

void LoadLocked() {
    if (s_initialized) {
        return;
    }

    s_entries.clear();

    Settings settings(kNamespace, false);
    int32_t count = settings.GetInt("count", 0);
    if (count < 0) {
        count = 0;
    }
    if (count > static_cast<int32_t>(NotesSystem::kMaxEntries)) {
        count = static_cast<int32_t>(NotesSystem::kMaxEntries);
    }

    for (int32_t i = 0; i < count; ++i) {
        std::string key = NormalizeKey(settings.GetString(SlotKey('k', i), ""));
        std::string value = Trim(settings.GetString(SlotKey('v', i), ""));
        if (key.empty() || value.empty()) {
            continue;
        }
        s_entries.push_back({key, value});
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Loaded %d note entries", static_cast<int>(s_entries.size()));
}

}  // namespace

namespace NotesSystem {

void Begin() {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();
}

bool Save(const std::string& key, const std::string& value, std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();

    const std::string normalized_key = NormalizeKey(key);
    const std::string trimmed_value = Trim(value);

    if (normalized_key.empty()) {
        if (error_out != nullptr) {
            *error_out = "key is empty after normalization";
        }
        return false;
    }

    if (trimmed_value.empty()) {
        if (error_out != nullptr) {
            *error_out = "value must not be empty";
        }
        return false;
    }

    auto it = std::find_if(s_entries.begin(), s_entries.end(),
                           [&normalized_key](const NoteEntry& entry) {
                               return entry.key == normalized_key;
                           });

    if (it != s_entries.end()) {
        it->value = trimmed_value;
        PersistLocked();
        return true;
    }

    if (s_entries.size() >= kMaxEntries) {
        if (error_out != nullptr) {
            *error_out = "notes capacity reached (max 20 entries)";
        }
        return false;
    }

    s_entries.push_back({normalized_key, trimmed_value});
    PersistLocked();
    return true;
}

bool Get(const std::string& key, std::string* value_out) {
    if (value_out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();

    const std::string normalized_key = NormalizeKey(key);
    if (normalized_key.empty()) {
        return false;
    }

    auto it = std::find_if(s_entries.begin(), s_entries.end(),
                           [&normalized_key](const NoteEntry& entry) {
                               return entry.key == normalized_key;
                           });

    if (it == s_entries.end()) {
        return false;
    }

    *value_out = it->value;
    return true;
}

std::vector<NoteEntry> List() {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();
    return s_entries;
}

std::string ListAsJson() {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "count", static_cast<double>(s_entries.size()));

    cJSON* entries = cJSON_CreateArray();
    for (const auto& entry : s_entries) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "key", entry.key.c_str());
        cJSON_AddStringToObject(item, "value", entry.value.c_str());
        cJSON_AddItemToArray(entries, item);
    }
    cJSON_AddItemToObject(root, "entries", entries);

    char* json = cJSON_PrintUnformatted(root);
    std::string output = json != nullptr ? json : "{}";
    cJSON_free(json);
    cJSON_Delete(root);
    return output;
}

}  // namespace NotesSystem
