/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <esp_pthread.h>

#include "application.h"
#include "assets/lang_config.h"
#include "display.h"
#include "oled_display.h"
#include "board.h"
#include "settings.h"
#include "notes_system.h"
#include "reminder_system.h"
#include "speaker_profile.h"
#include "behavior_scheduler.h"
#include "care_system.h"
#include "level_system.h"
#include "lvgl_theme.h"
#include "lvgl_display.h"

#define TAG "MCP"

namespace {

std::string NormalizeToken(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == ' ' || ch == '-' || ch == '.' || ch == '_') {
            normalized.push_back('_');
        }
    }
    return normalized;
}

bool IsSupportedEmotion(std::string_view emotion) {
    static const std::array<std::string_view, 31> kSupportedEmotions = {{
        "neutral", "relaxed", "cool",
        "happy", "funny",
        "laughing", "confident", "loving", "kissy", "delicious", "shocked",
        "surprised",
        "sad", "crying", "worried",
        "embarrassed",
        "nervous", "anxious",
        "angry", "annoyed",
        "sleepy",
        "thinking", "winking", "silly",
        "skeptic", "skeptical",
        "doubt", "doubtful",
        "confused",
    }};
    return std::find(kSupportedEmotions.begin(), kSupportedEmotions.end(), emotion) !=
           kSupportedEmotions.end();
}

std::string SupportedEmotionsList() {
    return "neutral, relaxed, cool, happy, funny, laughing, confident, loving, kissy, "
           "delicious, shocked, surprised, sad, crying, worried, embarrassed, nervous, "
           "anxious, angry, annoyed, sleepy, thinking, winking, silly, skeptic, "
           "skeptical, doubt, doubtful, confused";
}

bool ParseTime24h(const std::string& text, int* out_hour, int* out_minute) {
    if (out_hour == nullptr || out_minute == nullptr) {
        return false;
    }

    size_t colon = text.find(':');
    if (colon == std::string::npos || text.find(':', colon + 1) != std::string::npos) {
        return false;
    }

    const std::string hour_str = text.substr(0, colon);
    const std::string minute_str = text.substr(colon + 1);
    if (hour_str.empty() || minute_str.empty()) {
        return false;
    }
    if (hour_str.size() > 2 || minute_str.size() != 2) {
        return false;
    }
    if (!std::all_of(hour_str.begin(), hour_str.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) ||
        !std::all_of(minute_str.begin(), minute_str.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }

    const int hour = std::stoi(hour_str);
    const int minute = std::stoi(minute_str);
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }

    *out_hour = hour;
    *out_minute = minute;
    return true;
}

std::string TrimCopy(std::string value) {
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

bool ParseInteger(const std::string& text, int* out_value) {
    if (out_value == nullptr) {
        return false;
    }

    const std::string trimmed = TrimCopy(text);
    if (trimmed.empty()) {
        return false;
    }

    const char* cstr = trimmed.c_str();
    char* end = nullptr;
    const long value = std::strtol(cstr, &end, 10);
    if (end == cstr || *end != '\0') {
        return false;
    }
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        return false;
    }

    *out_value = static_cast<int>(value);
    return true;
}

const char* ReminderStateToString(ReminderSystem::ReminderState state) {
    switch (state) {
        case ReminderSystem::ReminderState::kPending:
            return "pending";
        case ReminderSystem::ReminderState::kFiring:
            return "firing";
        case ReminderSystem::ReminderState::kSnoozed:
            return "snoozed";
        case ReminderSystem::ReminderState::kConfirmed:
            return "confirmed";
        case ReminderSystem::ReminderState::kCancelled:
            return "cancelled";
        default:
            return "unknown";
    }
}

const char* FeatureIdToString(LevelSystem::FeatureID feature) {
    switch (feature) {
        case LevelSystem::IDLE_JITTER:
            return "idle_jitter";
        case LevelSystem::IDLE_GIGGLE:
            return "idle_giggle";
        case LevelSystem::IDLE_JUDGING:
            return "idle_judging";
        case LevelSystem::IDLE_SPEED_FAST:
            return "idle_speed_fast";
        case LevelSystem::EMO_EXCITED:
            return "emo_excited";
        case LevelSystem::EMO_ANGRY1:
            return "emo_angry1";
        case LevelSystem::EMO_LOVE:
            return "emo_love";
        case LevelSystem::EMO_SAD1:
            return "emo_sad1";
        case LevelSystem::EMO_HAPPY1:
            return "emo_happy1";
        case LevelSystem::LEGACY_EMO_LOVE:
            return "legacy_emo_love";
        case LevelSystem::LEGACY_EMO_CYCLOP:
            return "legacy_emo_cyclop";
        case LevelSystem::LEGACY_EMO_DRUNK:
            return "legacy_emo_drunk";
        default:
            return "unknown";
    }
}

std::string_view ResolveSoundName(std::string_view sound_name) {
    struct SoundBinding {
        std::string_view name;
        std::string_view sound;
    };

    static const std::array<SoundBinding, 52> kSoundBindings = {{
        {"0", Lang::Sounds::OGG_0},
        {"1", Lang::Sounds::OGG_1},
        {"2", Lang::Sounds::OGG_2},
        {"3", Lang::Sounds::OGG_3},
        {"4", Lang::Sounds::OGG_4},
        {"5", Lang::Sounds::OGG_5},
        {"6", Lang::Sounds::OGG_6},
        {"7", Lang::Sounds::OGG_7},
        {"8", Lang::Sounds::OGG_8},
        {"9", Lang::Sounds::OGG_9},
        {"activation", Lang::Sounds::OGG_ACTIVATION},
        {"bubu_angry1", Lang::Sounds::OGG_BUBU_ANGRY1},
        {"bubu_angry2", Lang::Sounds::OGG_BUBU_ANGRY2},
        {"bubu_blink", Lang::Sounds::OGG_BUBU_BLINK},
        {"bubu_bored1", Lang::Sounds::OGG_BUBU_BORED1},
        {"bubu_curious1", Lang::Sounds::OGG_BUBU_CURIOUS1},
        {"bubu_happy1", Lang::Sounds::OGG_BUBU_HAPPY1},
        {"bubu_happy2", Lang::Sounds::OGG_BUBU_HAPPY2},
        {"bubu_happy3", Lang::Sounds::OGG_BUBU_HAPPY3},
        {"bubu_laugh", Lang::Sounds::OGG_BUBU_LAUGH},
        {"bubu_mumble1", Lang::Sounds::OGG_BUBU_MUMBLING_1},
        {"bubu_mumble2", Lang::Sounds::OGG_BUBU_MUMBLING_2},
        {"bubu_mumble3", Lang::Sounds::OGG_BUBU_MUMBLING_3},
        {"bubu_mumble4", Lang::Sounds::OGG_BUBU_MUMBLING_4},
        {"bubu_mumbling1", Lang::Sounds::OGG_BUBU_MUMBLING_1},
        {"bubu_mumbling2", Lang::Sounds::OGG_BUBU_MUMBLING_2},
        {"bubu_mumbling3", Lang::Sounds::OGG_BUBU_MUMBLING_3},
        {"bubu_mumbling4", Lang::Sounds::OGG_BUBU_MUMBLING_4},
        {"bubu_mumbling_1", Lang::Sounds::OGG_BUBU_MUMBLING_1},
        {"bubu_mumbling_2", Lang::Sounds::OGG_BUBU_MUMBLING_2},
        {"bubu_mumbling_3", Lang::Sounds::OGG_BUBU_MUMBLING_3},
        {"bubu_mumbling_4", Lang::Sounds::OGG_BUBU_MUMBLING_4},
        {"bubu_sad1", Lang::Sounds::OGG_BUBU_SAD1},
        {"bubu_sad2", Lang::Sounds::OGG_BUBU_SAD2},
        {"bubu_sing1", Lang::Sounds::OGG_BUBU_SING1},
        {"bubu_sing2", Lang::Sounds::OGG_BUBU_SING2},
        {"bubu_sing3", Lang::Sounds::OGG_BUBU_SING3},
        {"bubu_sing4", Lang::Sounds::OGG_BUBU_SING4},
        {"bubu_tap", Lang::Sounds::OGG_BUBU_TAP},
        {"bubu_tired1", Lang::Sounds::OGG_BUBU_TIRED1},
        {"err_pin", Lang::Sounds::OGG_ERR_PIN},
        {"err_reg", Lang::Sounds::OGG_ERR_REG},
        {"exclamation", Lang::Sounds::OGG_EXCLAMATION},
        {"low_battery", Lang::Sounds::OGG_LOW_BATTERY},
        {"popup", Lang::Sounds::OGG_POPUP},
        {"success", Lang::Sounds::OGG_SUCCESS},
        {"upgrade", Lang::Sounds::OGG_UPGRADE},
        {"vibration", Lang::Sounds::OGG_VIBRATION},
        {"welcome", Lang::Sounds::OGG_WELCOME},
        {"wificonfig", Lang::Sounds::OGG_WIFICONFIG},
        {"wifi_config", Lang::Sounds::OGG_WIFICONFIG},
    }};

    const std::string normalized = NormalizeToken(sound_name);
    const auto it = std::find_if(kSoundBindings.begin(), kSoundBindings.end(),
                                 [&normalized](const SoundBinding& binding) {
                                     return binding.name == normalized;
                                 });
    if (it == kSoundBindings.end()) {
        return {};
    }
    return it->sound;
}

}  // namespace

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache 的特性。

    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.

    AddTool("self.get_device_status",
        "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n"
        "Use this tool for: \n"
        "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"
        "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                return true;
            });
    }

#ifdef HAVE_LVGL
    auto display = board.GetDisplay();
    if (display) {
        AddTool("self.screen.show_message",
            "Show a temporary text message on the screen during conversation. "
            "Args: `text` and `duration_ms`.",
            PropertyList({
                Property("text", kPropertyTypeString),
                Property("duration_ms", kPropertyTypeInteger, 4000, 500, 30000)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                const auto text = properties["text"].value<std::string>();
                const auto duration_ms = properties["duration_ms"].value<int>();
                display->ShowNotification(text, duration_ms);
                return std::string("message shown");
            });

        AddTool("self.screen.set_emotion",
            std::string("Change the eye expression during conversation. "
                        "Valid emotions: ") + SupportedEmotionsList() + ".",
            PropertyList({
                Property("emotion", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto emotion = NormalizeToken(properties["emotion"].value<std::string>());
                if (!IsSupportedEmotion(emotion)) {
                    throw std::runtime_error("Unsupported emotion: " + emotion);
                }
                display->SetEmotion(emotion.c_str());
                return std::string("emotion set to " + emotion);
            });
    }

    if (display && display->GetTheme() != nullptr) {
        AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto theme_name = properties["theme"].value<std::string>();
                auto& theme_manager = LvglThemeManager::GetInstance();
                auto theme = theme_manager.GetTheme(theme_name);
                if (theme != nullptr) {
                    display->SetTheme(theme);
                    return true;
                }
                return false;
            });
    }

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Always remember you have a camera. If the user asks you to see something, use this tool to take a photo and then explain it.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                // Lower the priority to do the camera capture
                TaskPriorityReset priority_reset(1);

                if (!camera->Capture()) {
                    throw std::runtime_error("Failed to capture photo");
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });
    }
#endif

    AddTool("self.audio_speaker.play_sound",
        "Play a validated audio cue. Supported names include popup, success, vibration, "
        "exclamation, activation, upgrade, welcome, wificonfig, low_battery, err_pin, "
        "err_reg, digits 0-9, and Bubu cues like bubu_happy1, bubu_laugh, bubu_sad1, "
        "bubu_mumble1, and bubu_tired1.",
        PropertyList({
            Property("sound_name", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const auto requested = properties["sound_name"].value<std::string>();
            const auto resolved = ResolveSoundName(requested);
            if (resolved.empty()) {
                throw std::runtime_error("Unsupported sound_name: " + NormalizeToken(requested));
            }
            Application::GetInstance().PlaySound(resolved);
            return std::string("playing sound " + NormalizeToken(requested));
        });

    AddTool("self.get_time",
        "Get current device local time and date as structured data.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;

            const time_t now = time(nullptr);
            struct tm local_tm = {};
            struct tm utc_tm = {};
            localtime_r(&now, &local_tm);
            gmtime_r(&now, &utc_tm);

            char date_buf[16] = {0};
            char time_buf[16] = {0};
            char iso_local_buf[40] = {0};
            strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &local_tm);
            strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &local_tm);
            strftime(iso_local_buf, sizeof(iso_local_buf), "%Y-%m-%dT%H:%M:%S", &local_tm);

            const time_t local_epoch = mktime(&local_tm);
            const time_t utc_as_local_epoch = mktime(&utc_tm);
            const int timezone_offset_min = static_cast<int>(difftime(local_epoch, utc_as_local_epoch) / 60.0);
            const bool time_valid = local_tm.tm_year >= (2025 - 1900);

            cJSON* json = cJSON_CreateObject();
            cJSON_AddNumberToObject(json, "unix_epoch_sec", static_cast<double>(now));
            cJSON_AddStringToObject(json, "date", date_buf);
            cJSON_AddStringToObject(json, "time", time_buf);
            cJSON_AddStringToObject(json, "iso_local", iso_local_buf);
            cJSON_AddNumberToObject(json, "timezone_offset_min", timezone_offset_min);
            cJSON_AddBoolToObject(json, "time_valid", time_valid);
            return json;
        });

    AddTool("self.behavior.get_config",
        "Get proactive behavior scheduler config.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            cJSON* json = cJSON_CreateObject();
            cJSON_AddNumberToObject(json, "idle_timeout_min",
                                    BehaviorScheduler::GetIdleTimeoutMinutes());
            return json;
        });

    AddTool("self.behavior.set_idle_timeout_minutes",
        "Set proactive idle-talk timeout in minutes (range 5..240).",
        PropertyList({
            Property("minutes", kPropertyTypeInteger, 5, 240)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const int minutes = properties["minutes"].value<int>();
            if (!BehaviorScheduler::SetIdleTimeoutMinutes(minutes)) {
                throw std::runtime_error("set_idle_timeout failed");
            }
            return std::string("idle_timeout_min set");
        });

    AddTool("self.get_care_stats",
        "Get current CareSystem stats and status flags.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;

            const int hunger = CareSystem::GetHunger();
            const int mood = CareSystem::GetMood();
            const int energy = CareSystem::GetEnergy();
            const int cleanliness = CareSystem::GetCleanliness();
            const int min_stat = std::min(std::min(hunger, mood), std::min(energy, cleanliness));

            cJSON* json = cJSON_CreateObject();
            cJSON_AddNumberToObject(json, "hunger", hunger);
            cJSON_AddNumberToObject(json, "mood", mood);
            cJSON_AddNumberToObject(json, "energy", energy);
            cJSON_AddNumberToObject(json, "cleanliness", cleanliness);
            cJSON_AddNumberToObject(json, "min_stat", min_stat);
            cJSON_AddBoolToObject(json, "needs_attention", CareSystem::NeedsAttention());
            cJSON_AddBoolToObject(json, "is_critical", CareSystem::IsCritical());
            return json;
        });

    AddTool("self.get_level_info",
        "Get current level progression and feature unlock states.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;

            const int level = LevelSystem::GetLevel();
            const int xp = LevelSystem::GetXP();
            const int xp_for_next = LevelSystem::GetXPForNextLevel();
            const int xp_remaining = std::max(0, xp_for_next - xp);
            const double progress_pct =
                (xp_for_next > 0) ? (100.0 * static_cast<double>(xp) / static_cast<double>(xp_for_next)) : 0.0;

            cJSON* json = cJSON_CreateObject();
            cJSON_AddNumberToObject(json, "level", level);
            cJSON_AddNumberToObject(json, "xp", xp);
            cJSON_AddNumberToObject(json, "xp_for_next_level", xp_for_next);
            cJSON_AddNumberToObject(json, "xp_remaining", xp_remaining);
            cJSON_AddNumberToObject(json, "progress_percent", progress_pct);

            cJSON* unlocks = cJSON_CreateArray();
            cJSON_AddItemToObject(json, "feature_unlocks", unlocks);
            for (int i = 0; i < static_cast<int>(LevelSystem::FEATURE_COUNT); ++i) {
                const auto feature = static_cast<LevelSystem::FeatureID>(i);
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "feature", FeatureIdToString(feature));
                cJSON_AddBoolToObject(item, "unlocked", LevelSystem::IsUnlocked(feature));
                cJSON_AddItemToArray(unlocks, item);
            }
            return json;
        });

    AddTool("self.remember",
        "Save or update a memory key/value pair for later recall. "
        "Keys are normalized and storage is capped at 20 entries.",
        PropertyList({
            Property("key", kPropertyTypeString),
            Property("value", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const auto key = properties["key"].value<std::string>();
            const auto value = properties["value"].value<std::string>();
            std::string error;
            if (!NotesSystem::Save(key, value, &error)) {
                throw std::runtime_error("remember failed: " + error);
            }
            return std::string("saved");
        });

    AddTool("self.recall",
        "Recall a saved memory value by key.",
        PropertyList({
            Property("key", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const auto key = properties["key"].value<std::string>();
            std::string value;
            if (!NotesSystem::Get(key, &value)) {
                throw std::runtime_error("recall failed: key not found");
            }
            return value;
        });

    AddTool("self.create_reminder",
        "Create and persist a timed reminder. "
        "Args: message, target_time_24h (HH:MM), confirmation_phrase, "
        "snooze_interval_min, and optional target_speaker.",
        PropertyList({
            Property("message", kPropertyTypeString),
            Property("target_time_24h", kPropertyTypeString),
            Property("confirmation_phrase", kPropertyTypeString, std::string("yes i understand")),
            Property("snooze_interval_min", kPropertyTypeInteger, 5, 1, 180),
            Property("target_speaker", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const auto message = properties["message"].value<std::string>();
            const auto target_time = properties["target_time_24h"].value<std::string>();
            const auto confirmation_phrase = properties["confirmation_phrase"].value<std::string>();
            const auto snooze_interval_min = properties["snooze_interval_min"].value<int>();
            const auto target_speaker = properties["target_speaker"].value<std::string>();

            int target_hour = 0;
            int target_minute = 0;
            if (!ParseTime24h(target_time, &target_hour, &target_minute)) {
                throw std::runtime_error("create_reminder failed: target_time_24h must be HH:MM (24h)");
            }

            ReminderSystem::Reminder reminder;
            reminder.message = message;
            reminder.target_hour = target_hour;
            reminder.target_minute = target_minute;
            reminder.confirmation_phrase = confirmation_phrase;
            reminder.snooze_interval_min = snooze_interval_min;
            reminder.target_speaker = target_speaker;

            int32_t reminder_id = 0;
            std::string error;
            if (!ReminderSystem::Add(reminder, &reminder_id, &error)) {
                throw std::runtime_error("create_reminder failed: " + error);
            }

            cJSON* json = cJSON_CreateObject();
            cJSON_AddNumberToObject(json, "id", reminder_id);
            cJSON_AddStringToObject(json, "message", reminder.message.c_str());
            cJSON_AddNumberToObject(json, "target_hour", target_hour);
            cJSON_AddNumberToObject(json, "target_minute", target_minute);
            cJSON_AddStringToObject(json, "target_time_24h", target_time.c_str());
            cJSON_AddStringToObject(json, "confirmation_phrase", confirmation_phrase.c_str());
            cJSON_AddNumberToObject(json, "snooze_interval_min", snooze_interval_min);
            cJSON_AddStringToObject(json, "target_speaker", target_speaker.c_str());
            return json;
        });

    AddTool("self.cancel_reminder",
        "Cancel an existing reminder by id.",
        PropertyList({
            Property("id", kPropertyTypeInteger, 1, std::numeric_limits<int>::max())
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const int32_t id = static_cast<int32_t>(properties["id"].value<int>());

            std::string error;
            if (!ReminderSystem::Cancel(id, &error)) {
                throw std::runtime_error("cancel_reminder failed: " + error);
            }

            cJSON* json = cJSON_CreateObject();
            cJSON_AddNumberToObject(json, "id", static_cast<double>(id));
            cJSON_AddStringToObject(json, "status", "cancelled");
            return json;
        });

    AddTool("self.edit_reminder",
        "Edit a single field on an existing reminder and persist changes. "
        "Valid fields: snooze_interval_min, confirmation_phrase, target_time_24h.",
        PropertyList({
            Property("id", kPropertyTypeInteger, 1, std::numeric_limits<int>::max()),
            Property("field", kPropertyTypeString),
            Property("new_value", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const int32_t id = static_cast<int32_t>(properties["id"].value<int>());
            const std::string field = NormalizeToken(properties["field"].value<std::string>());
            const std::string new_value_raw = properties["new_value"].value<std::string>();

            auto reminders = ReminderSystem::List();
            auto it = std::find_if(reminders.begin(), reminders.end(),
                                   [id](const ReminderSystem::Reminder& reminder) {
                                       return reminder.id == id;
                                   });
            if (it == reminders.end()) {
                throw std::runtime_error("edit_reminder failed: reminder not found");
            }

            ReminderSystem::Reminder updated = *it;
            if (field == "snooze_interval_min") {
                int value = 0;
                if (!ParseInteger(new_value_raw, &value) || value < 1 || value > 180) {
                    throw std::runtime_error(
                        "edit_reminder failed: snooze_interval_min must be an integer in [1, 180]");
                }
                updated.snooze_interval_min = value;
            } else if (field == "confirmation_phrase") {
                const std::string phrase = TrimCopy(new_value_raw);
                if (phrase.empty()) {
                    throw std::runtime_error(
                        "edit_reminder failed: confirmation_phrase cannot be empty");
                }
                updated.confirmation_phrase = phrase;
            } else if (field == "target_time_24h") {
                int hour = 0;
                int minute = 0;
                if (!ParseTime24h(new_value_raw, &hour, &minute)) {
                    throw std::runtime_error(
                        "edit_reminder failed: target_time_24h must be HH:MM (24h)");
                }
                updated.target_hour = hour;
                updated.target_minute = minute;
            } else {
                throw std::runtime_error(
                    "edit_reminder failed: unsupported field (valid: snooze_interval_min, confirmation_phrase, target_time_24h)");
            }

            std::string error;
            if (!ReminderSystem::Update(updated, &error)) {
                throw std::runtime_error("edit_reminder failed: " + error);
            }

            cJSON* json = cJSON_CreateObject();
            cJSON_AddNumberToObject(json, "id", static_cast<double>(updated.id));
            cJSON_AddStringToObject(json, "field", field.c_str());
            cJSON_AddStringToObject(json, "status", "updated");
            cJSON_AddNumberToObject(json, "target_hour", updated.target_hour);
            cJSON_AddNumberToObject(json, "target_minute", updated.target_minute);
            cJSON_AddStringToObject(json, "confirmation_phrase", updated.confirmation_phrase.c_str());
            cJSON_AddNumberToObject(json, "snooze_interval_min", updated.snooze_interval_min);
            return json;
        });

    AddTool("self.get_reminders",
        "List all reminders currently persisted on device.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            const auto reminders = ReminderSystem::List();

            cJSON* root = cJSON_CreateObject();
            cJSON* list = cJSON_CreateArray();
            cJSON_AddItemToObject(root, "reminders", list);
            cJSON_AddNumberToObject(root, "count", static_cast<double>(reminders.size()));

            for (const auto& reminder : reminders) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddNumberToObject(item, "id", static_cast<double>(reminder.id));
                cJSON_AddStringToObject(item, "message", reminder.message.c_str());
                cJSON_AddNumberToObject(item, "target_hour", reminder.target_hour);
                cJSON_AddNumberToObject(item, "target_minute", reminder.target_minute);

                char target_time_24h[8];
                std::snprintf(target_time_24h, sizeof(target_time_24h), "%02d:%02d",
                              reminder.target_hour, reminder.target_minute);
                cJSON_AddStringToObject(item, "target_time_24h", target_time_24h);

                cJSON_AddStringToObject(item, "state", ReminderStateToString(reminder.state));
                cJSON_AddNumberToObject(item, "snooze_interval_min", reminder.snooze_interval_min);
                cJSON_AddNumberToObject(item, "snooze_count", reminder.snooze_count);
                cJSON_AddStringToObject(item, "confirmation_phrase", reminder.confirmation_phrase.c_str());
                cJSON_AddStringToObject(item, "target_speaker", reminder.target_speaker.c_str());
                cJSON_AddItemToArray(list, item);
            }

            return root;
        });

    AddTool("self.speaker_profile.register",
        "Register or update the single-owner profile from explicit self-identification. "
        "Use this when the user says statements like 'my name is ...' or 'call me ...'. "
        "This is self-reported identity storage, not biometric voice recognition.",
        PropertyList({
            Property("name", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const auto name = properties["name"].value<std::string>();
            std::string error;
            if (!SpeakerProfile::Register(name, &error)) {
                throw std::runtime_error("speaker_profile.register failed: " + error);
            }
            return std::string("owner profile saved");
        });

    AddTool("self.get_current_speaker",
        "Get the current speaker name in single-owner mode. Returns the saved owner name "
        "if registered, otherwise returns 'unknown'. This is based on explicit registration, "
        "not biometric speaker recognition.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;

            cJSON* json = cJSON_CreateObject();
            const std::string name = SpeakerProfile::Identify();
            cJSON_AddStringToObject(json, "name", name.c_str());
            cJSON_AddBoolToObject(json, "is_registered", name != "unknown");
            cJSON_AddStringToObject(json, "mode", "self_reported_single_owner");
            return json;
        });

    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools() {
    // System tools
    AddUserOnlyTool("self.get_system_info",
        "Get the system information",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            return board.GetSystemInfoJson();
        });

    AddUserOnlyTool("self.reboot", "Reboot the system",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            app.Schedule([&app]() {
                ESP_LOGW(TAG, "User requested reboot");
                vTaskDelay(pdMS_TO_TICKS(1000));

                app.Reboot();
            });
            return true;
        });

    // Firmware upgrade
    AddUserOnlyTool("self.upgrade_firmware", "Upgrade firmware from a specific URL. This will download and install the firmware, then reboot the device.",
        PropertyList({
            Property("url", kPropertyTypeString, "The URL of the firmware binary file to download and install")
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            ESP_LOGI(TAG, "User requested firmware upgrade from URL: %s", url.c_str());
            
            auto& app = Application::GetInstance();
            app.Schedule([url, &app]() {
                bool success = app.UpgradeFirmware(url);
                if (!success) {
                    ESP_LOGE(TAG, "Firmware upgrade failed");
                }
            });
            
            return true;
        });

    // Display control
#ifdef HAVE_LVGL
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display) {
        AddUserOnlyTool("self.screen.get_info", "Information about the screen, including width, height, etc.",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "width", display->width());
                cJSON_AddNumberToObject(json, "height", display->height());
                if (dynamic_cast<OledDisplay*>(display)) {
                    cJSON_AddBoolToObject(json, "monochrome", true);
                } else {
                    cJSON_AddBoolToObject(json, "monochrome", false);
                }
                return json;
            });

#if CONFIG_LV_USE_SNAPSHOT
        AddUserOnlyTool("self.screen.snapshot", "Snapshot the screen and upload it to a specific URL",
            PropertyList({
                Property("url", kPropertyTypeString),
                Property("quality", kPropertyTypeInteger, 80, 1, 100)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto quality = properties["quality"].value<int>();

                std::string jpeg_data;
                if (!display->SnapshotToJpeg(jpeg_data, quality)) {
                    throw std::runtime_error("Failed to snapshot screen");
                }

                ESP_LOGI(TAG, "Upload snapshot %u bytes to %s", jpeg_data.size(), url.c_str());
                
                // 构造multipart/form-data请求体
                std::string boundary = "----ESP32_SCREEN_SNAPSHOT_BOUNDARY";
                
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
                if (!http->Open("POST", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                {
                    // 文件字段头部
                    std::string file_header;
                    file_header += "--" + boundary + "\r\n";
                    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"screenshot.jpg\"\r\n";
                    file_header += "Content-Type: image/jpeg\r\n";
                    file_header += "\r\n";
                    http->Write(file_header.c_str(), file_header.size());
                }

                // JPEG数据
                http->Write((const char*)jpeg_data.data(), jpeg_data.size());

                {
                    // multipart尾部
                    std::string multipart_footer;
                    multipart_footer += "\r\n--" + boundary + "--\r\n";
                    http->Write(multipart_footer.c_str(), multipart_footer.size());
                }
                http->Write("", 0);

                if (http->GetStatusCode() != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(http->GetStatusCode()));
                }
                std::string result = http->ReadAll();
                http->Close();
                ESP_LOGI(TAG, "Snapshot screen result: %s", result.c_str());
                return true;
            });
        
        AddUserOnlyTool("self.screen.preview_image", "Preview an image on the screen",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

                if (!http->Open("GET", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                int status_code = http->GetStatusCode();
                if (status_code != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(status_code));
                }

                size_t content_length = http->GetBodyLength();
                char* data = (char*)heap_caps_malloc(content_length, MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    throw std::runtime_error("Failed to allocate memory for image: " + url);
                }
                size_t total_read = 0;
                while (total_read < content_length) {
                    int ret = http->Read(data + total_read, content_length - total_read);
                    if (ret < 0) {
                        heap_caps_free(data);
                        throw std::runtime_error("Failed to download image: " + url);
                    }
                    if (ret == 0) {
                        break;
                    }
                    total_read += ret;
                }
                http->Close();

                auto image = std::make_unique<LvglAllocatedImage>(data, content_length);
                display->SetPreviewImage(std::move(image));
                return true;
            });
#endif // CONFIG_LV_USE_SNAPSHOT
    }
#endif // HAVE_LVGL

    // Assets download url
    auto& assets = Assets::GetInstance();
    if (assets.partition_valid()) {
        AddUserOnlyTool("self.assets.set_download_url", "Set the download url for the assets",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                Settings settings("assets", true);
                settings.SetString("download_url", url);
                return true;
            });
    }
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        bool list_user_only_tools = false;
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
            auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
            if (cJSON_IsBool(with_user_tools)) {
                list_user_only_tools = with_user_tools->valueint == 1;
            }
        }
        GetToolsList(id_int, cursor_str, list_user_only_tools);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }

        if (!list_user_only_tools && (*it)->user_only()) {
            ++it;
            continue;
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Use main thread to call the tool
    auto& app = Application::GetInstance();
    app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
}
