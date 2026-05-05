#include "speaker_profile.h"

#include "settings.h"

#include <cctype>
#include <mutex>

namespace {

static const char* kNamespace = "speaker";
static const char* kOwnerKey = "owner_name";

std::mutex s_mutex;
bool s_initialized = false;
std::string s_owner_name;

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

std::string NormalizeDisplayName(const std::string& raw_name) {
    const std::string trimmed = Trim(raw_name);
    std::string normalized;
    normalized.reserve(trimmed.size());

    bool previous_space = false;
    for (unsigned char ch : trimmed) {
        if (std::isspace(ch)) {
            if (!previous_space && !normalized.empty()) {
                normalized.push_back(' ');
            }
            previous_space = true;
            continue;
        }

        previous_space = false;
        normalized.push_back(static_cast<char>(ch));
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return normalized;
}

void LoadLocked() {
    if (s_initialized) {
        return;
    }

    Settings settings(kNamespace, false);
    s_owner_name = NormalizeDisplayName(settings.GetString(kOwnerKey, ""));
    s_initialized = true;
}

void PersistLocked() {
    Settings settings(kNamespace, true);
    if (s_owner_name.empty()) {
        settings.EraseKey(kOwnerKey);
        return;
    }
    settings.SetString(kOwnerKey, s_owner_name);
}

}  // namespace

namespace SpeakerProfile {

void Begin() {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();
}

bool Register(const std::string& name, std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();

    const std::string normalized_name = NormalizeDisplayName(name);
    if (normalized_name.empty()) {
        if (error_out != nullptr) {
            *error_out = "name is empty";
        }
        return false;
    }

    if (normalized_name.size() > 48) {
        if (error_out != nullptr) {
            *error_out = "name too long (max 48 characters)";
        }
        return false;
    }

    s_owner_name = normalized_name;
    PersistLocked();
    return true;
}

std::string Identify() {
    std::lock_guard<std::mutex> lock(s_mutex);
    LoadLocked();
    return s_owner_name.empty() ? "unknown" : s_owner_name;
}

}  // namespace SpeakerProfile
