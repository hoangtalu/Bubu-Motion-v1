#include "reminder_system.h"

#include "application.h"
#include "confirmation_evaluator.h"
#include "message_board.h"
#include "prompt_library.h"
#include "speaker_profile.h"
#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <mutex>

#include <esp_log.h>

namespace {

static const char* TAG = "ReminderSystem";
static const char* kNamespace = "reminders";

static constexpr size_t kMaxReminders = 32;
static constexpr bool kEnableSpokenReminderFlow = false;

std::mutex s_mutex;
bool s_initialized = false;
std::vector<ReminderSystem::Reminder> s_reminders;
int32_t s_next_id = 1;
int64_t s_last_tick_minute_epoch = -1;

static constexpr const char* kDefaultConfirmPhrase = "yes i understand";

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

int ClampInt(int value, int min_value, int max_value) {
    return std::max(min_value, std::min(value, max_value));
}

std::string SlotKey(char prefix, size_t index) {
    return std::string(1, prefix) + std::to_string(index);
}

ReminderSystem::ReminderState NormalizeState(int state_raw) {
    if (state_raw < static_cast<int>(ReminderSystem::ReminderState::kPending) ||
        state_raw > static_cast<int>(ReminderSystem::ReminderState::kCancelled)) {
        return ReminderSystem::ReminderState::kPending;
    }
    return static_cast<ReminderSystem::ReminderState>(state_raw);
}

ReminderSystem::Reminder NormalizeReminder(const ReminderSystem::Reminder& input, int32_t fallback_id) {
    ReminderSystem::Reminder out = input;

    out.id = out.id > 0 ? out.id : fallback_id;
    out.message = Trim(out.message);
    if (out.message.empty()) {
        out.message = "you have something important to remember";
    }

    out.target_hour = ClampInt(out.target_hour, 0, 23);
    out.target_minute = ClampInt(out.target_minute, 0, 59);
    out.confirmation_phrase = Trim(out.confirmation_phrase);
    out.snooze_interval_min = ClampInt(out.snooze_interval_min, 1, 180);
    out.snooze_count = std::max(0, out.snooze_count);
    out.target_speaker = Trim(out.target_speaker);
    out.state = NormalizeState(static_cast<int>(out.state));
    return out;
}

void RecomputeNextIdLocked() {
    int32_t max_id = 0;
    for (const auto& reminder : s_reminders) {
        if (reminder.id > max_id) {
            max_id = reminder.id;
        }
    }
    s_next_id = std::max<int32_t>(1, max_id + 1);
}

bool SaveLocked(std::string* error_out) {
    if (s_reminders.size() > kMaxReminders) {
        if (error_out != nullptr) {
            *error_out = "too many reminders in queue";
        }
        return false;
    }

    Settings settings(kNamespace, true);
    settings.SetInt("count", static_cast<int32_t>(s_reminders.size()));
    settings.SetInt("next_id", s_next_id);

    for (size_t i = 0; i < s_reminders.size(); ++i) {
        const auto normalized = NormalizeReminder(s_reminders[i], static_cast<int32_t>(i + 1));
        s_reminders[i] = normalized;

        settings.SetInt(SlotKey('i', i), normalized.id);
        settings.SetString(SlotKey('m', i), normalized.message);
        settings.SetInt(SlotKey('h', i), normalized.target_hour);
        settings.SetInt(SlotKey('n', i), normalized.target_minute);
        settings.SetString(SlotKey('p', i), normalized.confirmation_phrase);
        settings.SetInt(SlotKey('z', i), normalized.snooze_interval_min);
        settings.SetInt(SlotKey('c', i), normalized.snooze_count);
        settings.SetInt(SlotKey('s', i), static_cast<int>(normalized.state));
        settings.SetString(SlotKey('t', i), normalized.target_speaker);
    }

    for (size_t i = s_reminders.size(); i < kMaxReminders; ++i) {
        settings.EraseKey(SlotKey('i', i));
        settings.EraseKey(SlotKey('m', i));
        settings.EraseKey(SlotKey('h', i));
        settings.EraseKey(SlotKey('n', i));
        settings.EraseKey(SlotKey('p', i));
        settings.EraseKey(SlotKey('z', i));
        settings.EraseKey(SlotKey('c', i));
        settings.EraseKey(SlotKey('s', i));
        settings.EraseKey(SlotKey('t', i));
    }

    return true;
}

bool ShouldEvaluateState(ReminderSystem::ReminderState state) {
    return state == ReminderSystem::ReminderState::kPending ||
           state == ReminderSystem::ReminderState::kSnoozed;
}

size_t FindReminderIndexByIdLocked(int32_t id) {
    for (size_t i = 0; i < s_reminders.size(); ++i) {
        if (s_reminders[i].id == id) {
            return i;
        }
    }
    return s_reminders.size();
}

int32_t FindActiveFiringReminderIdLocked() {
    for (const auto& reminder : s_reminders) {
        if (reminder.state == ReminderSystem::ReminderState::kFiring) {
            return reminder.id;
        }
    }
    return 0;
}

std::string ResolveReminderName(const ReminderSystem::Reminder& reminder) {
    const std::string target = Trim(reminder.target_speaker);
    if (!target.empty()) {
        return target;
    }

    const std::string owner = SpeakerProfile::Identify();
    if (owner != "unknown") {
        return owner;
    }
    return "";
}

bool FireLocked(size_t index) {
    if (index >= s_reminders.size()) {
        return false;
    }

    auto& reminder = s_reminders[index];
    bool spoken_started = false;
    if (kEnableSpokenReminderFlow) {
        PromptContext context;
        context.name = ResolveReminderName(reminder);
        context.reminder_message = reminder.message;
        context.confirm_phrase = reminder.confirmation_phrase.empty()
                                     ? kDefaultConfirmPhrase
                                     : reminder.confirmation_phrase;

        const std::string seed_prompt = PromptLibrary::Get(PromptCategory::kReminderFire, context);
        if (!seed_prompt.empty()) {
            spoken_started = Application::GetInstance().InitiateConversation(seed_prompt);
        }
        if (!spoken_started) {
            ESP_LOGW(TAG, "Reminder %d spoken flow unavailable, using local message board only",
                     static_cast<int>(reminder.id));
        }
    }

    reminder.state = ReminderSystem::ReminderState::kFiring;
    std::string save_error;
    if (!SaveLocked(&save_error)) {
        ESP_LOGW(TAG, "Reminder %d fired but save failed: %s", static_cast<int>(reminder.id),
                 save_error.c_str());
    }
    MessageBoard::OpenReminder(reminder.id, reminder.message, reminder.target_hour, reminder.target_minute);
    ESP_LOGI(TAG, "Reminder fired: id=%d at %02d:%02d (spoken=%s)", static_cast<int>(reminder.id),
             reminder.target_hour, reminder.target_minute, spoken_started ? "on" : "off");
    return true;
}

bool LoadLocked(std::string* error_out) {
    Settings settings(kNamespace, false);
    int32_t count = settings.GetInt("count", 0);
    count = ClampInt(count, 0, static_cast<int>(kMaxReminders));

    std::vector<ReminderSystem::Reminder> loaded;
    loaded.reserve(static_cast<size_t>(count));
    int32_t fallback_id = 1;

    for (int32_t i = 0; i < count; ++i) {
        ReminderSystem::Reminder reminder;
        reminder.id = settings.GetInt(SlotKey('i', i), 0);
        reminder.message = settings.GetString(SlotKey('m', i), "");
        reminder.target_hour = settings.GetInt(SlotKey('h', i), 9);
        reminder.target_minute = settings.GetInt(SlotKey('n', i), 0);
        reminder.confirmation_phrase = settings.GetString(SlotKey('p', i), "");
        reminder.snooze_interval_min = settings.GetInt(SlotKey('z', i), 5);
        reminder.snooze_count = settings.GetInt(SlotKey('c', i), 0);
        reminder.state = NormalizeState(settings.GetInt(SlotKey('s', i), 0));
        reminder.target_speaker = settings.GetString(SlotKey('t', i), "");

        reminder = NormalizeReminder(reminder, fallback_id++);
        loaded.push_back(reminder);
    }

    s_reminders = std::move(loaded);
    RecomputeNextIdLocked();

    const int32_t persisted_next_id = settings.GetInt("next_id", s_next_id);
    if (persisted_next_id > s_next_id) {
        s_next_id = persisted_next_id;
    }

    ESP_LOGI(TAG, "Loaded %d reminders (next_id=%d)",
             static_cast<int>(s_reminders.size()), static_cast<int>(s_next_id));
    (void)error_out;
    return true;
}

void EnsureInitializedLocked() {
    if (s_initialized) {
        return;
    }
    std::string error;
    if (!LoadLocked(&error)) {
        ESP_LOGW(TAG, "Initial load failed: %s", error.c_str());
    }
    s_initialized = true;
}

}  // namespace

namespace ReminderSystem {

void Begin() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
}

bool Save(std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    return SaveLocked(error_out);
}

bool Load(std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_initialized = true;
    return LoadLocked(error_out);
}

std::vector<Reminder> List() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    return s_reminders;
}

bool GetById(int32_t id, Reminder* out) {
    if (out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    if (id <= 0) {
        return false;
    }
    const size_t index = FindReminderIndexByIdLocked(id);
    if (index >= s_reminders.size()) {
        return false;
    }
    *out = s_reminders[index];
    return true;
}

void SetAll(const std::vector<Reminder>& reminders) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    s_reminders.clear();
    s_reminders.reserve(std::min(reminders.size(), kMaxReminders));
    int32_t fallback_id = 1;
    for (const auto& reminder : reminders) {
        if (s_reminders.size() >= kMaxReminders) {
            break;
        }
        s_reminders.push_back(NormalizeReminder(reminder, fallback_id++));
    }
    RecomputeNextIdLocked();
}

bool Add(Reminder reminder, int32_t* out_id, std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    if (s_reminders.size() >= kMaxReminders) {
        if (error_out != nullptr) {
            *error_out = "reminder capacity reached";
        }
        return false;
    }

    reminder.id = s_next_id++;
    reminder = NormalizeReminder(reminder, reminder.id);
    s_reminders.push_back(reminder);

    std::string save_error;
    if (!SaveLocked(&save_error)) {
        s_reminders.pop_back();
        if (error_out != nullptr) {
            *error_out = "save failed: " + save_error;
        }
        return false;
    }

    if (out_id != nullptr) {
        *out_id = reminder.id;
    }
    return true;
}

bool Update(const Reminder& reminder, std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    if (reminder.id <= 0) {
        if (error_out != nullptr) {
            *error_out = "invalid id";
        }
        return false;
    }

    const size_t index = FindReminderIndexByIdLocked(reminder.id);
    if (index >= s_reminders.size()) {
        if (error_out != nullptr) {
            *error_out = "reminder not found";
        }
        return false;
    }

    const Reminder previous = s_reminders[index];
    Reminder normalized = NormalizeReminder(reminder, previous.id);
    normalized.id = previous.id;
    s_reminders[index] = normalized;

    std::string save_error;
    if (!SaveLocked(&save_error)) {
        s_reminders[index] = previous;
        if (error_out != nullptr) {
            *error_out = "save failed: " + save_error;
        }
        return false;
    }

    return true;
}

bool Cancel(int32_t id, std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    if (id <= 0) {
        if (error_out != nullptr) {
            *error_out = "invalid id";
        }
        return false;
    }

    auto it = std::find_if(s_reminders.begin(), s_reminders.end(),
                           [id](const Reminder& reminder) {
                               return reminder.id == id;
                           });
    if (it == s_reminders.end()) {
        if (error_out != nullptr) {
            *error_out = "reminder not found";
        }
        return false;
    }

    const Reminder removed = *it;
    const auto removed_index = static_cast<size_t>(std::distance(s_reminders.begin(), it));
    s_reminders.erase(it);

    std::string save_error;
    if (!SaveLocked(&save_error)) {
        s_reminders.insert(s_reminders.begin() + static_cast<std::ptrdiff_t>(removed_index), removed);
        if (error_out != nullptr) {
            *error_out = "save failed: " + save_error;
        }
        return false;
    }

    MessageBoard::Close();
    return true;
}

int32_t GetActiveFiringReminderId() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    return FindActiveFiringReminderIdLocked();
}

bool Snooze(int32_t id, std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    if (id <= 0) {
        if (error_out != nullptr) {
            *error_out = "invalid id";
        }
        return false;
    }

    const size_t index = FindReminderIndexByIdLocked(id);
    if (index >= s_reminders.size()) {
        if (error_out != nullptr) {
            *error_out = "reminder not found";
        }
        return false;
    }

    Reminder previous = s_reminders[index];
    Reminder& reminder = s_reminders[index];

    const time_t now_sec = time(nullptr);
    if (now_sec <= 0) {
        if (error_out != nullptr) {
            *error_out = "clock unavailable";
        }
        return false;
    }

    const int64_t next_sec = static_cast<int64_t>(now_sec) +
                             static_cast<int64_t>(reminder.snooze_interval_min) * 60;
    const time_t next_time = static_cast<time_t>(next_sec);
    tm next_tm = {};
    localtime_r(&next_time, &next_tm);

    reminder.target_hour = next_tm.tm_hour;
    reminder.target_minute = next_tm.tm_min;
    reminder.snooze_count = std::max(0, reminder.snooze_count) + 1;
    reminder.state = ReminderState::kSnoozed;

    std::string save_error;
    if (!SaveLocked(&save_error)) {
        s_reminders[index] = previous;
        if (error_out != nullptr) {
            *error_out = "save failed: " + save_error;
        }
        return false;
    }

    MessageBoard::Close();
    ESP_LOGI(TAG, "Reminder snoozed: id=%d next=%02d:%02d count=%d",
             static_cast<int>(reminder.id), reminder.target_hour, reminder.target_minute,
             reminder.snooze_count);
    return true;
}

bool Dismiss(int32_t id, std::string* error_out) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    if (id <= 0) {
        if (error_out != nullptr) {
            *error_out = "invalid id";
        }
        return false;
    }

    const size_t index = FindReminderIndexByIdLocked(id);
    if (index >= s_reminders.size()) {
        if (error_out != nullptr) {
            *error_out = "reminder not found";
        }
        return false;
    }

    Reminder removed = s_reminders[index];
    s_reminders[index].state = ReminderState::kConfirmed;
    removed = s_reminders[index];
    s_reminders.erase(s_reminders.begin() + static_cast<std::ptrdiff_t>(index));

    std::string save_error;
    if (!SaveLocked(&save_error)) {
        s_reminders.insert(s_reminders.begin() + static_cast<std::ptrdiff_t>(index), removed);
        if (error_out != nullptr) {
            *error_out = "save failed: " + save_error;
        }
        return false;
    }

    MessageBoard::Close();
    const time_t now_sec = time(nullptr);
    tm now_tm = {};
    if (now_sec > 0) {
        localtime_r(&now_sec, &now_tm);
        ESP_LOGI(TAG, "Reminder dismissed: id=%d at %04d-%02d-%02d %02d:%02d:%02d",
                 static_cast<int>(id),
                 now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
                 now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
    } else {
        ESP_LOGI(TAG, "Reminder dismissed: id=%d", static_cast<int>(id));
    }
    return true;
}

bool OnResponse(int32_t id, const std::string& response_text, std::string* error_out) {
    std::string required_phrase = kDefaultConfirmPhrase;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        EnsureInitializedLocked();

        if (id <= 0) {
            if (error_out != nullptr) {
                *error_out = "invalid id";
            }
            return false;
        }

        const size_t index = FindReminderIndexByIdLocked(id);
        if (index >= s_reminders.size()) {
            if (error_out != nullptr) {
                *error_out = "reminder not found";
            }
            return false;
        }

        const Reminder& reminder = s_reminders[index];
        if (reminder.state != ReminderState::kFiring) {
            if (error_out != nullptr) {
                *error_out = "reminder is not firing";
            }
            return false;
        }

        if (!reminder.confirmation_phrase.empty()) {
            required_phrase = reminder.confirmation_phrase;
        }
    }

    if (ConfirmationEvaluator::Matches(response_text, required_phrase)) {
        return Dismiss(id, error_out);
    }
    return Snooze(id, error_out);
}

void Tick() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    auto& app = Application::GetInstance();
    if (!app.IsProtocolReady()) {
        return;
    }
    if (app.GetDeviceState() != kDeviceStateIdle) {
        return;
    }

    const time_t now_sec = time(nullptr);
    if (now_sec <= 0) {
        return;
    }
    const int64_t minute_epoch = static_cast<int64_t>(now_sec / 60);
    if (minute_epoch == s_last_tick_minute_epoch) {
        return;
    }
    s_last_tick_minute_epoch = minute_epoch;

    tm local_tm = {};
    localtime_r(&now_sec, &local_tm);
    const int hour = local_tm.tm_hour;
    const int minute = local_tm.tm_min;

    for (size_t i = 0; i < s_reminders.size(); ++i) {
        const auto& reminder = s_reminders[i];
        if (!ShouldEvaluateState(reminder.state)) {
            continue;
        }
        if (reminder.target_hour != hour || reminder.target_minute != minute) {
            continue;
        }
        if (FireLocked(i)) {
            return;
        }
    }
}

}  // namespace ReminderSystem
