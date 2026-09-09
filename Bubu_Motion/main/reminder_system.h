#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ReminderSystem {

enum class ReminderState : int {
    kPending = 0,
    kFiring = 1,
    kSnoozed = 2,
    kConfirmed = 3,
    kCancelled = 4,
};

struct Reminder {
    int32_t id = 0;
    std::string message;
    int target_hour = 9;
    int target_minute = 0;
    std::string confirmation_phrase;
    int snooze_interval_min = 5;
    int snooze_count = 0;
    ReminderState state = ReminderState::kPending;
    std::string target_speaker;
};

// Load reminders from NVS and prepare runtime state.
void Begin();

// Persist the full current queue to NVS.
bool Save(std::string* error_out = nullptr);

// Force reload from NVS into runtime queue.
bool Load(std::string* error_out = nullptr);

// Current reminder snapshot.
std::vector<Reminder> List();

// Lookup reminder by id in current runtime queue.
bool GetById(int32_t id, Reminder* out);

// Replace full queue in memory (normalizes missing/invalid fields).
void SetAll(const std::vector<Reminder>& reminders);

// Add reminder, assign monotonic ID, and persist immediately.
bool Add(Reminder reminder, int32_t* out_id = nullptr, std::string* error_out = nullptr);

// Update an existing reminder by ID and persist immediately.
bool Update(const Reminder& reminder, std::string* error_out = nullptr);

// Cancel reminder by ID (remove from active queue) and persist immediately.
bool Cancel(int32_t id, std::string* error_out = nullptr);

// Return currently active FIRING reminder id, or 0 when none.
int32_t GetActiveFiringReminderId();

// Handle user response for an active reminder id.
// If confirmation phrase matches -> dismiss, else -> snooze.
bool OnResponse(int32_t id, const std::string& response_text, std::string* error_out = nullptr);

// Snooze active reminder by ID and persist immediately.
bool Snooze(int32_t id, std::string* error_out = nullptr);

// Dismiss active reminder by ID and persist immediately.
bool Dismiss(int32_t id, std::string* error_out = nullptr);

// Evaluate due reminders and trigger local reminder UI when needed.
void Tick();

}  // namespace ReminderSystem
