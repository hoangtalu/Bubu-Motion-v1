#include "behavior_scheduler.h"

#include "application.h"
#include "care_system.h"
#include "display/menu_system.h"
#include "prompt_library.h"
#include "settings.h"
#include "speaker_profile.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <mutex>

#include <esp_log.h>
#include <esp_timer.h>

namespace {

static const char* TAG = "BehaviorScheduler";
static const char* kNamespace = "behavior";
static const char* kIdleTimeoutKey = "idle_timeout_min";

static constexpr int kDefaultIdleTimeoutMin = 60;
static constexpr int kMinIdleTimeoutMin = 5;
static constexpr int kMaxIdleTimeoutMin = 240;

static constexpr uint64_t kTickIntervalMs = 60ULL * 1000ULL;
static constexpr uint64_t kMinInitiationGapMs = 2ULL * 60ULL * 1000ULL;
static constexpr uint64_t kCarePromptCooldownMs = 15ULL * 60ULL * 1000ULL;
static constexpr uint64_t kGreetingIdleGateMs = 5ULL * 60ULL * 1000ULL;

std::mutex s_mutex;
bool s_initialized = false;
int s_idle_timeout_min = kDefaultIdleTimeoutMin;
uint64_t s_last_interaction_ms = 0;
uint64_t s_last_tick_ms = 0;
uint64_t s_last_initiation_ms = 0;
uint64_t s_last_care_prompt_ms = 0;
uint64_t s_last_idle_prompt_ms = 0;
int s_last_greeting_day_key = -1;
int s_last_greeting_mask = 0;  // bit 0 = morning, bit 1 = evening

uint64_t NowMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

int ClampIdleTimeout(int minutes) {
    return std::clamp(minutes, kMinIdleTimeoutMin, kMaxIdleTimeoutMin);
}

void LoadConfigLocked() {
    Settings settings(kNamespace, false);
    s_idle_timeout_min = ClampIdleTimeout(settings.GetInt(kIdleTimeoutKey, kDefaultIdleTimeoutMin));
}

void SaveConfigLocked() {
    Settings settings(kNamespace, true);
    settings.SetInt(kIdleTimeoutKey, s_idle_timeout_min);
}

void EnsureInitializedLocked() {
    if (s_initialized) {
        return;
    }
    LoadConfigLocked();
    s_last_interaction_ms = NowMs();
    s_last_tick_ms = 0;
    s_last_initiation_ms = 0;
    s_last_care_prompt_ms = 0;
    s_last_idle_prompt_ms = 0;
    s_last_greeting_day_key = -1;
    s_last_greeting_mask = 0;
    s_initialized = true;
}

int DayKey(const tm& local_tm) {
    return (local_tm.tm_year + 1900) * 1000 + local_tm.tm_yday;
}

enum GreetingSlot {
    kGreetingNone = 0,
    kGreetingMorning = 1,
    kGreetingEvening = 2,
};

GreetingSlot ResolveGreetingSlot(int hour24) {
    if (hour24 >= 6 && hour24 < 12) {
        return kGreetingMorning;
    }
    if (hour24 >= 17 && hour24 < 22) {
        return kGreetingEvening;
    }
    return kGreetingNone;
}

CareStatType ResolveCriticalStat() {
    struct StatValue {
        CareStatType type;
        int value;
    };
    const StatValue stats[] = {
        {CareStatType::kHunger, CareSystem::GetHunger()},
        {CareStatType::kMood, CareSystem::GetMood()},
        {CareStatType::kEnergy, CareSystem::GetEnergy()},
        {CareStatType::kCleanliness, CareSystem::GetCleanliness()},
    };

    int min_value = 101;
    CareStatType min_type = CareStatType::kNone;
    for (const auto& item : stats) {
        if (item.value < min_value) {
            min_value = item.value;
            min_type = item.type;
        }
    }
    return min_type;
}

PromptContext BuildPromptContext() {
    PromptContext context;
    const std::string speaker = SpeakerProfile::Identify();
    if (speaker != "unknown") {
        context.name = speaker;
    }
    return context;
}

void MarkConversationStartedLocked(uint64_t now_ms, bool reset_idle_timer) {
    s_last_initiation_ms = now_ms;
    if (reset_idle_timer) {
        s_last_interaction_ms = now_ms;
    }
}

bool TryStartConversationLocked(PromptCategory category,
                                PromptContext context,
                                uint64_t now_ms,
                                bool reset_idle_timer) {
    if (now_ms - s_last_initiation_ms < kMinInitiationGapMs) {
        return false;
    }

    const std::string seed_prompt = PromptLibrary::Get(category, context);
    if (seed_prompt.empty()) {
        return false;
    }

    if (!Application::GetInstance().InitiateConversation(seed_prompt)) {
        return false;
    }

    MarkConversationStartedLocked(now_ms, reset_idle_timer);
    return true;
}

}  // namespace

namespace BehaviorScheduler {

void Begin() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    ESP_LOGI(TAG, "Ready (idle_timeout_min=%d)", s_idle_timeout_min);
}

void SetLastInteractionTime() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    s_last_interaction_ms = NowMs();
}

int GetIdleTimeoutMinutes() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    return s_idle_timeout_min;
}

bool SetIdleTimeoutMinutes(int minutes) {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    const int clamped = ClampIdleTimeout(minutes);
    if (clamped == s_idle_timeout_min) {
        return true;
    }

    s_idle_timeout_min = clamped;
    SaveConfigLocked();
    return true;
}

void Tick() {
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();

    const uint64_t now_ms = NowMs();
    if (s_last_tick_ms != 0 && (now_ms - s_last_tick_ms) < kTickIntervalMs) {
        return;
    }
    s_last_tick_ms = now_ms;

    auto& app = Application::GetInstance();
    if (!app.IsProtocolReady()) {
        return;
    }
    if (app.GetDeviceState() != kDeviceStateIdle) {
        return;
    }
    if (MenuSystem::IsAnyOpen()) {
        return;
    }

    // Priority 1 (due reminders) is handled by ReminderSystem::Tick() in Application clock loop.

    // Priority 2: care critical prompt.
    if (CareSystem::IsCritical() && (now_ms - s_last_care_prompt_ms) >= kCarePromptCooldownMs) {
        PromptContext context = BuildPromptContext();
        context.stat = ResolveCriticalStat();
        if (TryStartConversationLocked(PromptCategory::kCareReminder, context, now_ms, false)) {
            s_last_care_prompt_ms = now_ms;
            ESP_LOGI(TAG, "Triggered care-critical proactive conversation");
            return;
        }
    }

    // Priority 3: idle timeout prompt.
    const uint64_t idle_timeout_ms = static_cast<uint64_t>(s_idle_timeout_min) * 60ULL * 1000ULL;
    const uint64_t idle_elapsed_ms = now_ms - s_last_interaction_ms;
    if (idle_elapsed_ms >= idle_timeout_ms &&
        (now_ms - s_last_idle_prompt_ms) >= idle_timeout_ms) {
        PromptContext context = BuildPromptContext();
        if (TryStartConversationLocked(PromptCategory::kIdleTooLong, context, now_ms, true)) {
            s_last_idle_prompt_ms = now_ms;
            ESP_LOGI(TAG, "Triggered idle proactive conversation (idle_timeout_min=%d)",
                     s_idle_timeout_min);
            return;
        }
    }

    // Priority 4: morning/evening greeting.
    time_t now_sec = time(nullptr);
    tm local_tm = {};
    localtime_r(&now_sec, &local_tm);

    const int day_key = DayKey(local_tm);
    if (day_key != s_last_greeting_day_key) {
        s_last_greeting_day_key = day_key;
        s_last_greeting_mask = 0;
    }

    if (idle_elapsed_ms < kGreetingIdleGateMs) {
        return;
    }

    const GreetingSlot slot = ResolveGreetingSlot(local_tm.tm_hour);
    int slot_bit = 0;
    PromptCategory category = PromptCategory::kMorning;
    std::string label;
    if (slot == kGreetingMorning) {
        slot_bit = 1 << 0;
        category = PromptCategory::kMorning;
        label = "morning";
    } else if (slot == kGreetingEvening) {
        slot_bit = 1 << 1;
        category = PromptCategory::kEvening;
        label = "evening";
    } else {
        return;
    }

    if ((s_last_greeting_mask & slot_bit) != 0) {
        return;
    }

    PromptContext context = BuildPromptContext();
    context.time_of_day = label;
    if (TryStartConversationLocked(category, context, now_ms, false)) {
        s_last_greeting_mask |= slot_bit;
        ESP_LOGI(TAG, "Triggered %s greeting proactive conversation", label.c_str());
    }
}

}  // namespace BehaviorScheduler
