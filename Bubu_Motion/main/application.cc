#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "care_system.h"
#include "level_system.h"
#include "speaker_profile.h"
#include "reminder_system.h"
#include "display/menu_system.h"
#include "message_board.h"
#include "time_sync.h"

#include <cctype>
#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include <string_view>

#define TAG "Application"

namespace {

constexpr uint64_t kListeningNoSpeechTimeoutMs = 8000;
constexpr uint64_t kListeningServerReplyTimeoutMs = 15000;
// Mic-stall watchdog: how long listening may run with nothing at all coming
// off the microphone before the session is torn down. Applies to every
// listening mode, unlike the two above. Generous enough to clear the
// AudioInputTask warmup delay and an AutoStop playback-queue drain.
constexpr uint64_t kListeningMicStallTimeoutMs = 5000;
// Grace before declaring a channel-less listening state dead. Only needs to
// cover the gap between entering listening and the channel being reported open;
// it is not a retry window, because nothing retries.
constexpr uint64_t kListeningNoChannelTimeoutMs = 3000;
// How often the device asks the console whether its assets bundle (and so its
// wake word) changed. Counted in 1 Hz clock ticks. 15 minutes is a compromise:
// short enough that a parent changing the wake word in the portal sees it apply
// while they still remember doing it, long enough that it is not a meaningful
// load on the console or the battery.
constexpr int kAssetsRefreshIntervalSeconds = 15 * 60;
constexpr uint32_t kStopListeningDrainMs = 200;
constexpr uint32_t kStopListeningPostDisableDrainMs = 120;
constexpr std::string_view kLocalCommandPrefix = "__local_cmd__:";

std::string NormalizeLocalCommandAction(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

std::string JsonStringOrEmpty(const cJSON* object, const char* key) {
    if (object == nullptr || key == nullptr) {
        return "";
    }
    auto value = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
        return "";
    }
    return std::string(value->valuestring);
}

}  // namespace


Application::Application() {
    event_group_ = xEventGroupCreate();
    ai_chat_queue_ = xQueueCreate(16, sizeof(AiChatQueueItem));

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

}

Application::~Application() {
    if (ai_chat_task_handle_ != nullptr) {
        vTaskDelete(ai_chat_task_handle_);
    }
    if (ai_chat_queue_ != nullptr) {
        AiChatQueueItem pending;
        while (xQueueReceive(ai_chat_queue_, &pending, 0) == pdTRUE) {
            cJSON_Delete(pending.root);
        }
        vQueueDelete(ai_chat_queue_);
    }
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        last_vad_speaking_.store(speaking);
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        // SFX policy: the overlay lane is hard-muted for the whole AI session
        // (Connecting -> Listening -> Speaking) and only resumes once the
        // device is back to Idle. Done here rather than in
        // HandleStateChangedEvent because that runs later, on the main task,
        // and the AI's TTS starts arriving the moment the state flips.
        bool ai_session = new_state == kDeviceStateConnecting ||
                          new_state == kDeviceStateListening ||
                          new_state == kDeviceStateSpeaking;
        audio_service_.SetSfxMuted(ai_session);
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Before anything formats a time: the clock holds UTC, TZ makes it local.
    TimeSync::Begin();

    // Initialize care and level systems
    LevelSystem::Begin();
    CareSystem::Begin();
    SpeakerProfile::Begin();
    ReminderSystem::Begin();

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();
    StartAiChatRuntime();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::StartupIdle:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_STARTUP_IDLE);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_NETWORK_STARTUP_IDLE |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_STARTUP_IDLE) {
            HandleNetworkStartupIdleEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            int sent_packets = 0;
            int dropped_packets = 0;
            bool send_failed = false;
            // Always drain the send queue completely, even after a failure.
            //
            // The uplink is realtime, so anything still queued behind a failed send
            // is already stale and worth dropping. More importantly, leaving the
            // queue at MAX_SEND_PACKETS_IN_QUEUE stalls OpusCodecTask -- and that
            // task is the only caller of on_send_queue_available, which is the only
            // thing that sets MAIN_EVENT_SEND_AUDIO. Bailing out early therefore
            // wedges the uplink permanently: the device sits in listening forever
            // while AudioService logs "Encode queue full" once per frame.
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (send_failed || protocol_ == nullptr) {
                    ++dropped_packets;
                    continue;
                }
                if (!protocol_->SendAudio(std::move(packet))) {
                    send_failed = true;
                    ++dropped_packets;
                    continue;
                }
                ++sent_packets;
            }
            if (sent_packets > 0 || dropped_packets > 0) {
                ESP_LOGI(TAG, "[AI-TX-AUDIO] sent=%d dropped=%d failed=%s",
                         sent_packets, dropped_packets, send_failed ? "true" : "false");
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                if (last_vad_speaking_.load()) {
                    listening_voice_detected_.store(true);
                }
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
            // Every input path (touch, long press, buttons) is dispatched
            // through Schedule(), so this is the choke point where the active
            // screen can change. Re-derive it here rather than waiting for the
            // next clock tick, otherwise closing a menu leaves the eyes hidden
            // for up to a second.
            Board::GetInstance().GetDisplay()->RefreshScreen();
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
            // Timer-driven transitions (idle -> clock -> sleep) land here.
            display->RefreshScreen();

            // Care + level system tick (every second; internal throttles handle actual rates)
            CareSystem::Update();
            LevelSystem::Tick();
            ReminderSystem::Tick();
            MenuSystem::Render();
            CheckListeningInactivityTimeout();

            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }

            // Ask the console every 15 minutes whether our assets bundle changed.
            // Riding the existing 1 Hz tick avoids a second timer; the work
            // itself is pushed to a short-lived task, never done here.
            if (clock_ticks_ % kAssetsRefreshIntervalSeconds == 0) {
                MaybeRefreshAssetsBundle();
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    TimeSync::StartSntp();
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkStartupIdleEvent() {
    auto state = GetDeviceState();
    if (state != kDeviceStateStarting) {
        return;
    }

    ESP_LOGI(TAG, "Startup completed without auto-connecting to WiFi");
    SetDeviceState(kDeviceStateIdle);

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    auto display = board.GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() {
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    });
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new firmware version
    CheckNewVersion();

    // Apply or download assets after OTA config has had a chance to provide assets_url.
    CheckAssetsVersion();

    // Initialize the protocol
    InitializeProtocol();

    // From here on ota_ is no longer owned by this task, so the periodic
    // assets refresh below is allowed to use it.
    activation_done_ = true;

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::StartAiChatRuntime() {
    if (ai_chat_task_handle_ != nullptr) {
        return;
    }
    if (ai_chat_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to start AI chat runtime: queue not initialized");
        return;
    }

    BaseType_t ok = xTaskCreate([](void* arg) {
        auto* app = static_cast<Application*>(arg);
        app->AiChatRuntimeTask();
        vTaskDelete(NULL);
    }, "ai_chat_runtime", 4096 * 3, this, 3, &ai_chat_task_handle_);
    if (ok != pdPASS) {
        ai_chat_task_handle_ = nullptr;
        ESP_LOGE(TAG, "Failed to create ai_chat_runtime task");
    }
}

void Application::EnqueueIncomingJson(const cJSON* root) {
    if (root == nullptr || ai_chat_queue_ == nullptr) {
        return;
    }

    cJSON* copied = cJSON_Duplicate(root, 1);
    if (copied == nullptr) {
        ESP_LOGE(TAG, "Failed to duplicate incoming JSON");
        return;
    }

    AiChatQueueItem item;
    item.root = copied;
    item.enqueued_ms = esp_timer_get_time() / 1000ULL;
    if (xQueueSend(ai_chat_queue_, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "AI chat queue full, dropping incoming message");
        cJSON_Delete(copied);
        ai_chat_drop_count_.fetch_add(1);
        return;
    }

    auto type = cJSON_GetObjectItem(root, "type");
    const char* type_str = cJSON_GetStringValue(type);
    if (type_str != nullptr) {
        ESP_LOGI(TAG, "[AI-RX-ENQUEUE] type=%s q_depth=%u drops=%u",
                 type_str,
                 static_cast<unsigned>(uxQueueMessagesWaiting(ai_chat_queue_)),
                 static_cast<unsigned>(ai_chat_drop_count_.load()));
    }
}

void Application::AiChatRuntimeTask() {
    while (true) {
        AiChatQueueItem item;
        if (xQueueReceive(ai_chat_queue_, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (item.root == nullptr) {
            continue;
        }
        uint64_t now_ms = esp_timer_get_time() / 1000ULL;
        uint64_t queue_wait_ms = (item.enqueued_ms > 0 && now_ms >= item.enqueued_ms)
                                     ? (now_ms - item.enqueued_ms)
                                     : 0;
        ProcessIncomingJsonMessage(item.root, queue_wait_ms);
        cJSON_Delete(item.root);
    }
}

void Application::ProcessIncomingJsonMessage(const cJSON* root, uint64_t queue_wait_ms) {
    auto display = Board::GetInstance().GetDisplay();
    auto type = cJSON_GetObjectItem(root, "type");
    const char* type_str = cJSON_GetStringValue(type);
    if (type_str == nullptr) {
        ESP_LOGW(TAG, "Incoming JSON missing type");
        return;
    }
    const char* state_name = DeviceStateMachine::GetStateName(GetDeviceState());
    if (state_name == nullptr) {
        state_name = "unknown";
    }
    const unsigned long queue_wait_ms_log = static_cast<unsigned long>(queue_wait_ms);
    ESP_LOGI(TAG, "[AI-RX-DISPATCH] type=%s queue_wait_ms=%lu state=%s",
             type_str,
             queue_wait_ms_log,
             state_name);

    if (strcmp(type_str, "tts") == 0) {
        auto state = cJSON_GetObjectItem(root, "state");
        const char* tts_state = cJSON_GetStringValue(state);
        if (tts_state == nullptr) {
            ESP_LOGW(TAG, "TTS message missing state");
            return;
        }
        if (strcmp(tts_state, "start") == 0) {
            Schedule([this]() {
                // These arrive over the wire but are acted on later, off the
                // main loop queue, and the channel can close in between. A
                // server that announces an error over TTS and then drops the
                // connection is exactly that case: the messages outlive the
                // session that produced them.
                if (protocol_ == nullptr || !protocol_->IsAudioChannelOpened()) {
                    ESP_LOGW(TAG, "Ignoring tts start: audio channel is not open");
                    return;
                }
                aborted_ = false;
                SetDeviceState(kDeviceStateSpeaking);
            });
        } else if (strcmp(tts_state, "stop") == 0) {
            Schedule([this]() {
                if (GetDeviceState() != kDeviceStateSpeaking) {
                    return;
                }
                // Without this the device lands in listening with nothing to
                // listen to and no way out: the mic-stall watchdog sees a
                // healthy mic, and the no-speech timeout is disarmed for the
                // rest of the session as soon as VAD fires once.
                if (protocol_ == nullptr || !protocol_->IsAudioChannelOpened()) {
                    ESP_LOGW(TAG, "tts stop with no open audio channel, going idle");
                    SetDeviceState(kDeviceStateIdle);
                    return;
                }
                if (listening_mode_ == kListeningModeManualStop) {
                    SetDeviceState(kDeviceStateIdle);
                } else {
                    SetDeviceState(kDeviceStateListening);
                }
            });
        } else if (strcmp(tts_state, "sentence_start") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, "<< %s", text->valuestring);
                Schedule([display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("assistant", message.c_str());
                });
            }
        }
    } else if (strcmp(type_str, "stt") == 0) {
        auto text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            ESP_LOGI(TAG, ">> %s", text->valuestring);
            std::string message(text->valuestring);

            const int32_t firing_reminder_id = ReminderSystem::GetActiveFiringReminderId();
            if (firing_reminder_id > 0) {
                std::string reminder_error;
                if (!ReminderSystem::OnResponse(firing_reminder_id, message, &reminder_error)) {
                    ESP_LOGW(TAG, "Reminder response handling failed (id=%d): %s",
                             static_cast<int>(firing_reminder_id), reminder_error.c_str());
                } else {
                    ESP_LOGI(TAG, "Reminder response handled locally (id=%d)",
                             static_cast<int>(firing_reminder_id));
                }
            }
            Schedule([display, message = std::move(message)]() {
                display->SetChatMessage("user", message.c_str());
            });
        }
    } else if (strcmp(type_str, "llm") == 0) {
        auto emotion = cJSON_GetObjectItem(root, "emotion");
        if (cJSON_IsString(emotion)) {
            Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                display->SetEmotion(emotion_str.c_str());
                // The face changes immediately; the voice line only plays if
                // we happen to be Idle by the time this runs. Emotion lines
                // arriving mid-response are dropped rather than deferred to
                // Idle — a deferred voice lands on whatever face the
                // autonomous Care Emotion scheduler has moved on to, which is
                // worse than no voice at all. See PlayEmotionalVoice.
                PlayEmotionalVoice(emotion_str);
            });
        }
    } else if (strcmp(type_str, "loading") == 0) {
        if (GetDeviceState() == kDeviceStateListening) {
            auto now_ms = esp_timer_get_time() / 1000ULL;
            listening_server_loading_ms_.store(now_ms);
        }
    } else if (strcmp(type_str, "session_end") == 0) {
        listening_server_loading_ms_.store(0);
        Schedule([this]() {
            if (protocol_ && protocol_->IsAudioChannelOpened()) {
                protocol_->CloseAudioChannel();
            } else {
                SetDeviceState(kDeviceStateIdle);
            }
        });
    } else if (strcmp(type_str, "mcp") == 0) {
        auto payload = cJSON_GetObjectItem(root, "payload");
        if (cJSON_IsObject(payload)) {
            McpServer::GetInstance().ParseMessage(payload);
        }
    } else if (strcmp(type_str, "system") == 0) {
        auto command = cJSON_GetObjectItem(root, "command");
        const char* command_str = cJSON_GetStringValue(command);
        if (command_str != nullptr) {
            ESP_LOGI(TAG, "System command: %s", command_str);
            if (strcmp(command_str, "reboot") == 0) {
                Schedule([this]() {
                    Reboot();
                });
            } else {
                ESP_LOGW(TAG, "Unknown system command: %s", command_str);
            }
        }
    } else if (strcmp(type_str, "alert") == 0) {
        auto status = cJSON_GetObjectItem(root, "status");
        auto message = cJSON_GetObjectItem(root, "message");
        auto emotion = cJSON_GetObjectItem(root, "emotion");
        if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
            const std::string status_text(status->valuestring);
            const std::string message_text(message->valuestring);
            const bool detect_wakeword_reject =
                status_text == "ERROR" &&
                (message_text.find("only for wake words") != std::string::npos ||
                 message_text.find("仅用于唤醒词") != std::string::npos);

            if (detect_wakeword_reject) {
                const int32_t firing_id = ReminderSystem::GetActiveFiringReminderId();
                if (firing_id > 0) {
                    ReminderSystem::Reminder reminder;
                    std::string reminder_message = "Reminder is active. Please confirm by voice.";
                    if (ReminderSystem::GetById(firing_id, &reminder) &&
                        !reminder.message.empty()) {
                        reminder_message = "Reminder: " + reminder.message;
                    }

                    ESP_LOGW(TAG, "Proactive detect rejected by server for reminder id=%d, falling back to local notify/listen",
                             static_cast<int>(firing_id));
                    Schedule([this, display, reminder_message = std::move(reminder_message)]() {
                        display->ShowNotification(reminder_message.c_str(), 5000);
                        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
                        if (GetDeviceState() == kDeviceStateConnecting) {
                            SetListeningMode(GetDefaultListeningMode());
                        }
                    });
                    return;
                }
            }
            Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
        } else {
            ESP_LOGW(TAG, "Alert command requires status, message and emotion");
        }
    } else if (strcmp(type_str, "device_bind_required") == 0) {
        std::string bind_message = JsonStringOrEmpty(root, "message");
        if (bind_message.empty()) {
            bind_message = JsonStringOrEmpty(root, "detail");
        }
        if (bind_message.empty()) {
            bind_message = JsonStringOrEmpty(root, "text");
        }

        std::string bind_code = JsonStringOrEmpty(root, "code");
        if (bind_code.empty()) {
            bind_code = JsonStringOrEmpty(root, "bind_code");
        }
        if (bind_code.empty()) {
            bind_code = JsonStringOrEmpty(root, "activation_code");
        }
        if (bind_code.empty()) {
            bind_code = JsonStringOrEmpty(root, "binding_number");
        }

        if (bind_message.empty()) {
            bind_message = Lang::Strings::ACTIVATION;
        }

        ESP_LOGW(TAG, "Device requires bind: message='%s' code='%s'",
                 bind_message.c_str(),
                 bind_code.empty() ? "(none)" : bind_code.c_str());
        Schedule([this, bind_message = std::move(bind_message), bind_code = std::move(bind_code)]() {
            UpdateBindRequiredState(bind_message, bind_code);
        });
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    } else if (strcmp(type->valuestring, "custom") == 0) {
        auto payload = cJSON_GetObjectItem(root, "payload");
        char* printed_root = cJSON_PrintUnformatted(root);
        if (printed_root != nullptr) {
            ESP_LOGI(TAG, "Received custom message: %s", printed_root);
            cJSON_free(printed_root);
        }
        if (cJSON_IsObject(payload)) {
            char* printed_payload = cJSON_PrintUnformatted(payload);
            if (printed_payload != nullptr) {
                std::string payload_str(printed_payload);
                cJSON_free(printed_payload);
                Schedule([display, payload_str = std::move(payload_str)]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            }
        } else {
            ESP_LOGW(TAG, "Invalid custom message format: missing payload");
        }
#endif
    } else {
        ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
    }
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    // The OTA check re-offers the same download_url on every boot (it has no
    // concept of "already installed" of its own -- see main/ota.cc,
    // StoreAssetsDownloadUrl), so without this check a device would
    // re-download and overwrite its entire assets partition every single
    // boot forever, even when nothing actually changed.
    if (!download_url.empty() && download_url == settings.GetString("applied_url")) {
        ESP_LOGI(TAG, "Assets URL already applied, skipping re-download: %s", download_url.c_str());
        settings.EraseKey("download_url");
        download_url.clear();
    }

    if (!download_url.empty()) {
        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        // Empty when the server offered no hash; Download() then falls back to the
        // bundle's own 16-bit checksum and says so in the log.
        std::string download_sha256 = settings.GetString("download_sha256");
        bool success = assets.Download(download_url, download_sha256,
                                       [this, display](int progress, size_t speed) -> void {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            Schedule([display, message = std::string(buffer)]() {
                display->SetChatMessage("system", message.c_str());
            });
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }

        settings.EraseKey("download_url");
        settings.EraseKey("download_sha256");
        // Remember this exact URL was applied so future boots don't redo it.
        // Only reached when Download() returned true, which now means the bytes
        // matched the server's SHA-256 as well as the bundle's own checksum -- so
        // a corrupt download is retried on the next boot instead of being latched
        // as installed.
        settings.SetString("applied_url", download_url);
    }

    // Apply assets
    assets.Apply();
    // MenuSystem::Begin() (called well before this, during early boot) already
    // resolved every menu/care icon into a raw pointer against whatever the
    // partition held *then*. Download() just remapped it to new content, so
    // those pointers are stale -- not dangling (partition_valid() guards that
    // window elsewhere), just wrong: every icon would silently render garbage
    // or vanish until the next full boot re-resolved them from scratch.
    // Reproduced on hardware 2026-09-10 (all menu icons gone after the first
    // OTA assets load, fine again after a reboot) before this call was added.
    MenuSystem::RefreshIcons();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::MaybeRefreshAssetsBundle() {
    // The assets bundle carries the wake word model, and the console chooses it
    // per device. CheckNewVersion() only ever runs once, at boot, so without
    // this poll a wake word changed in the portal would not reach a device until
    // it happened to be power-cycled.
    if (!activation_done_ || ota_ == nullptr) {
        return;
    }
    if (assets_refresh_running_.exchange(true)) {
        return;  // a previous poll is still in flight
    }
    // Never interrupt a child mid-conversation. Re-checked after the HTTP call
    // too, since that takes seconds and the state can change under us.
    if (GetDeviceState() != kDeviceStateIdle) {
        assets_refresh_running_ = false;
        return;
    }

    BaseType_t ok = xTaskCreate([](void* arg) {
        auto* app = static_cast<Application*>(arg);
        app->AssetsRefreshTask();
        app->assets_refresh_running_ = false;
        vTaskDelete(NULL);
    }, "assets_refresh", 4096 * 2, this, 1, nullptr);
    if (ok != pdPASS) {
        assets_refresh_running_ = false;
        ESP_LOGW(TAG, "Failed to create assets_refresh task; will retry next tick");
    }
}

void Application::AssetsRefreshTask() {
    // Ota::CheckVersion() re-reads the console config; StoreAssetsDownloadUrl()
    // inside it stamps assets/download_url only when the console offers a bundle
    // that is not the one already applied.
    if (ota_->CheckVersion() != ESP_OK) {
        ESP_LOGD(TAG, "Assets refresh: version check failed, will retry later");
        return;
    }

    Settings settings("assets", false);
    std::string download_url = settings.GetString("download_url");
    if (download_url.empty() || download_url == settings.GetString("applied_url")) {
        return;
    }

    if (GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGI(TAG, "New assets bundle pending, but device is busy; deferring");
        return;
    }

    // Deliberately reboot instead of downloading here. CheckAssetsVersion() on
    // the boot path already owns the download, together with its progress UI,
    // its failure alert and the partition-erase window; duplicating the riskiest
    // routine in the firmware for a second caller is not worth it.
    ESP_LOGI(TAG, "New assets bundle offered (%s), rebooting to install it",
             download_url.c_str());
    Reboot();
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        ClearBindRequiredState();
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            if (!audio_service_.PushPacketToDecodeQueue(std::move(packet))) {
                ESP_LOGW(TAG, "[AI-RX-AUDIO] decode queue full, dropping packet");
            }
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    protocol_->OnIncomingJson([this](const cJSON* root) {
        EnqueueIncomingJson(root);
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // Append the code to the message so it's visible on the screen
    std::string full_message = message;
    if (!code.empty() && full_message.find(code) == std::string::npos) {
        full_message += "\n\n" + code;
    }
    Alert(Lang::Strings::ACTIVATION, full_message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);
    UpdateBindRequiredState(message, code);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::UpdateBindRequiredState(const std::string& message, const std::string& code) {
    bind_required_message_ = message;
    bind_required_code_ = code;
    has_bind_required_payload_ = !bind_required_message_.empty() || !bind_required_code_.empty();
    MessageBoard::OpenBindCode(bind_required_message_, bind_required_code_);
}

void Application::ClearBindRequiredState() {
    if (!has_bind_required_payload_) {
        MessageBoard::ClearBindCode();
        return;
    }
    bind_required_message_.clear();
    bind_required_code_.clear();
    has_bind_required_payload_ = false;
    MessageBoard::ClearBindCode();
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (CanPlayIdleOnlySfx()) {
        PlayEmotionalVoice(emotion);
        if (!sound.empty()) {
            audio_service_.PlaySound(sound);
        }
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetChatMessage("system", "");
    }
}

bool Application::CanPlayIdleOnlySfx() {
    return GetDeviceState() == kDeviceStateIdle && audio_service_.IsIdle();
}

void Application::PlayEmotionalVoice(const std::string& emotion) {
    // Rides the overlay lane (AudioService::PlayEmotionalVoice ->
    // PlayOverlaySound), so it follows the same idle-only rule: silent for the
    // whole AI session, back on once the device returns to Idle. It used to
    // play mid-response over the ducked voice lane so the voice always matched
    // the face on screen; with ducking gone, an emotion line during a response
    // would just talk over the AI, so it is dropped instead. Same
    // CanPlayIdleOnlySfx() check as PlayOverlaySound — see the note there
    // about the playback queue still draining after the state flips to Idle.
    if (!CanPlayIdleOnlySfx()) {
        ESP_LOGD(TAG, "Skip emotion voice outside idle state");
        return;
    }
    audio_service_.PlayEmotionalVoice(emotion);
}

void Application::RewardMoodForAiChatUse() {
    CareSystem::AddMood(10);
    ESP_LOGI(TAG, "AI chat used, mood +10");
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::EndConversation() {
    Schedule([this]() {
        const auto state = GetDeviceState();
        if (state != kDeviceStateConnecting &&
            state != kDeviceStateListening &&
            state != kDeviceStateSpeaking) {
            return;
        }

        if (state == kDeviceStateSpeaking) {
            AbortSpeaking(kAbortReasonNone);
        }

        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
            return;
        }

        SetDeviceState(kDeviceStateIdle);
    });
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            pending_ai_chat_mood_reward_ = true;
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, mode]() {
                ContinueOpenAudioChannel(mode);
            });
            return;
        }
        RewardMoodForAiChatUse();
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            pending_ai_chat_mood_reward_ = false;
            return;
        }
    }

    ESP_LOGI(TAG, "Audio channel opened, entering listening mode=%d session=%s",
             static_cast<int>(mode),
             protocol_ ? protocol_->session_id().c_str() : "(none)");
    if (pending_ai_chat_mood_reward_) {
        pending_ai_chat_mood_reward_ = false;
        RewardMoodForAiChatUse();
    }
    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            pending_ai_chat_mood_reward_ = true;
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this]() {
                ContinueOpenAudioChannel(kListeningModeManualStop);
            });
            return;
        }
        RewardMoodForAiChatUse();
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        RewardMoodForAiChatUse();
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        // Third tap behavior: if we are already waiting for server reply in this session,
        // cancel current turn and resume listening without closing the audio channel.
        if (listening_server_loading_ms_.load() != 0) {
            listening_server_loading_ms_.store(0);
            listening_mode_ = kListeningModeManualStop;
            listening_voice_detected_.store(false);
            listening_started_ms_.store(esp_timer_get_time() / 1000ULL);
            listening_diag_last_log_ms_.store(0);

            auto display = Board::GetInstance().GetDisplay();
            if (display != nullptr) {
                display->SetStatus(Lang::Strings::LISTENING);
            }

            if (protocol_) {
                protocol_->SendAbortSpeaking(kAbortReasonNone);
                protocol_->SendStartListening(listening_mode_);
            }
            audio_service_.ResetDecoder();
            audio_service_.EnableVoiceProcessing(true);
            ESP_LOGI(TAG, "[AI-CANCEL-TURN] tap_cancel_wait -> resume_listening session=%s",
                     protocol_ ? protocol_->session_id().c_str() : "(none)");
            return;
        }

        auto display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            display->SetStatus(Lang::Strings::PLEASE_WAIT);
        }

        // Keep processor running briefly to capture trailing speech before stop.
        vTaskDelay(pdMS_TO_TICKS(kStopListeningDrainMs));
        if (GetDeviceState() != kDeviceStateListening) {
            return;
        }

        audio_service_.EnableVoiceProcessing(false);
        // Let encoder push remaining frames into send queue.
        vTaskDelay(pdMS_TO_TICKS(kStopListeningPostDisableDrainMs));

        int flushed_packets = 0;
        bool flush_failed = false;
        while (auto packet = audio_service_.PopPacketFromSendQueue()) {
            if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                flush_failed = true;
                break;
            }
            ++flushed_packets;
        }
        ESP_LOGI(TAG, "[AI-STOP-FLUSH] drained_ms=%u post_disable_ms=%u flushed=%d failed=%s",
                 static_cast<unsigned>(kStopListeningDrainMs),
                 static_cast<unsigned>(kStopListeningPostDisableDrainMs),
                 flushed_packets,
                 flush_failed ? "true" : "false");

        uint32_t last_uplink_seq = protocol_ ? protocol_->GetLastUplinkSequence() : 0;
        ESP_LOGI(TAG, "[AI-STOP-SEQ] last_uplink_seq=%lu flushed=%d",
                 static_cast<unsigned long>(last_uplink_seq),
                 flushed_packets);

        if (protocol_) {
            protocol_->SendStopListening();
        }
        listening_server_loading_ms_.store(esp_timer_get_time() / 1000ULL);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    auto wake_word = audio_service_.GetLastWakeWord();
    if (wake_word.rfind(kLocalCommandPrefix.data(), 0) == 0) {
        std::string action = NormalizeLocalCommandAction(wake_word.substr(kLocalCommandPrefix.size()));
        if (action == "smile") {
            auto display = Board::GetInstance().GetDisplay();
            if (display != nullptr) {
                display->SetEmotion("happy");
            }
            ESP_LOGI(TAG, "Local voice action executed: smile -> happy emotion");
        } else if (action == "angry") {
            auto display = Board::GetInstance().GetDisplay();
            if (display != nullptr) {
                display->SetEmotion("angry");
            }
            ESP_LOGI(TAG, "Local voice action executed: angry -> angry emotion");
        } else {
            ESP_LOGW(TAG, "Unsupported local voice action: %s", action.c_str());
        }
        return;
    }

    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();

    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();
        auto wake_word = audio_service_.GetLastWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            pending_ai_chat_mood_reward_ = true;
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update),
            // then continue with OpenAudioChannel which may block for ~1 second
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        SetDeviceState(kDeviceStateConnecting);
        RewardMoodForAiChatUse();
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Clear send queue to avoid sending residues to server
        while (audio_service_.PopPacketFromSendQueue());

        if (state == kDeviceStateListening) {
            RewardMoodForAiChatUse();
            protocol_->SendStartListening(GetDefaultListeningMode());
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // Play popup sound and start listening again
            RewardMoodForAiChatUse();
            play_popup_on_listening_ = true;
            SetListeningMode(GetDefaultListeningMode());
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            // Both callers have already moved the device to Connecting, and
            // OpenAudioChannel has failure paths that return false WITHOUT
            // raising OnNetworkError — CreateWebSocket() returning null under
            // internal-SRAM pressure is the likely one on this board. Nothing
            // else ever leaves Connecting (no timeout, no error event), so
            // without this the device sits on "Connecting" until it is power
            // cycled. Go back to Idle so the next wake word can be tried.
            ESP_LOGE(TAG, "Failed to open audio channel, returning to idle");
            audio_service_.EnableWakeWordDetection(true);
            pending_ai_chat_mood_reward_ = false;
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }

#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);

    // Set flag to play popup sound after state changes to listening
    play_popup_on_listening_ = true;
    if (pending_ai_chat_mood_reward_) {
        pending_ai_chat_mood_reward_ = false;
        RewardMoodForAiChatUse();
    }
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    if (pending_ai_chat_mood_reward_) {
        pending_ai_chat_mood_reward_ = false;
        RewardMoodForAiChatUse();
    }
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            pending_ai_chat_mood_reward_ = false;
            listening_started_ms_.store(0);
            listening_voice_detected_.store(false);
            listening_server_loading_ms_.store(0);
            listening_diag_last_log_ms_.store(0);
            display->SetStatus(Lang::Strings::STANDBY);
            display->ClearChatMessages();  // Clear messages first
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            listening_started_ms_.store(0);
            listening_voice_detected_.store(false);
            listening_server_loading_ms_.store(0);
            listening_diag_last_log_ms_.store(0);
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            listening_started_ms_.store(esp_timer_get_time() / 1000ULL);
            listening_voice_detected_.store(false);
            listening_server_loading_ms_.store(0);
            listening_diag_last_log_ms_.store(0);
            display->SetStatus(Lang::Strings::LISTENING);

            // Make sure the audio processor is running
            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                // For auto mode, wait for playback queue to be empty before enabling voice processing
                // This prevents audio truncation when STOP arrives late due to network jitter
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }
                
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            // Enable wake word detection in listening mode (configured via Kconfig)
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
            // Disable wake word detection in listening mode
            audio_service_.EnableWakeWordDetection(false);
#endif
            
            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            pending_ai_chat_mood_reward_ = false;
            listening_started_ms_.store(0);
            listening_voice_detected_.store(false);
            listening_server_loading_ms_.store(0);
            listening_diag_last_log_ms_.store(0);
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            pending_ai_chat_mood_reward_ = false;
            listening_started_ms_.store(0);
            listening_voice_detected_.store(false);
            listening_server_loading_ms_.store(0);
            listening_diag_last_log_ms_.store(0);
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::CheckListeningInactivityTimeout() {
    if (GetDeviceState() != kDeviceStateListening) {
        return;
    }

    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    AudioDebugSnapshot snapshot = audio_service_.GetDebugSnapshot();
    LogListeningDebugReport(now_ms, snapshot);

    // Listening with no audio channel is never valid: there is nobody to send
    // the audio to. This check comes first deliberately, because every timeout
    // below it can be disarmed -- the mic-stall one by a healthy mic, the
    // server-reply one by never having been armed, and the no-speech one
    // permanently by a single VAD hit. Observed on hardware 2026-09-09: the
    // device sat here for over 50 s encoding into a closed socket.
    uint64_t listening_started_for_channel_ms = listening_started_ms_.load();
    if (listening_started_for_channel_ms != 0 &&
        now_ms - listening_started_for_channel_ms >= kListeningNoChannelTimeoutMs &&
        (protocol_ == nullptr || !protocol_->IsAudioChannelOpened())) {
        ESP_LOGW(TAG, "Listening with no open audio channel for %lums, returning to idle",
                 static_cast<unsigned long>(now_ms - listening_started_for_channel_ms));
        listening_started_ms_.store(0);
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    // Mic-stall watchdog. AudioInputTask parks on its event group whenever
    // neither the wake word nor the audio processor is running, so if the AFE
    // processor fails to start there is nothing reading the codec at all — the
    // session sits in listening with a dead mic and no way out. Neither
    // timeout below covers that: the no-speech one is AutoStop-only, and the
    // server-reply one never arms because the server has nothing to reply to.
    // Tearing the session down here also gives AudioService a fresh attempt at
    // initializing the processor on the next listening entry.
    uint64_t listening_started_for_mic_ms = listening_started_ms_.load();
    if (listening_started_for_mic_ms != 0 &&
        now_ms - listening_started_for_mic_ms >= kListeningMicStallTimeoutMs) {
        uint64_t mic_idle_ms = snapshot.last_input_ms > 0 && now_ms >= snapshot.last_input_ms
                                   ? (now_ms - snapshot.last_input_ms)
                                   : 999999999ULL;
        if (mic_idle_ms >= kListeningMicStallTimeoutMs) {
            ESP_LOGW(TAG,
                     "Mic stalled in listening (no input for %lums, processor=%s, wake=%s), "
                     "ending session",
                     static_cast<unsigned long>(mic_idle_ms),
                     snapshot.audio_processor_running ? "true" : "false",
                     snapshot.wake_word_running ? "true" : "false");
            listening_started_ms_.store(0);
            if (protocol_ && protocol_->IsAudioChannelOpened()) {
                protocol_->CloseAudioChannel();
            } else {
                SetDeviceState(kDeviceStateIdle);
            }
            return;
        }
    }

    uint64_t loading_started_ms = listening_server_loading_ms_.load();
    if (loading_started_ms != 0 &&
        now_ms - loading_started_ms >= kListeningServerReplyTimeoutMs) {
        ESP_LOGW(TAG, "Server reply timeout after loading, closing audio channel");
        listening_server_loading_ms_.store(0);
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        } else {
            SetDeviceState(kDeviceStateIdle);
        }
        return;
    }

    if (listening_mode_ != kListeningModeAutoStop) {
        return;
    }

    if (listening_voice_detected_.load()) {
        return;
    }

    uint64_t listening_started_ms = listening_started_ms_.load();
    if (listening_started_ms == 0) {
        return;
    }

    if (now_ms - listening_started_ms < kListeningNoSpeechTimeoutMs) {
        return;
    }

    ESP_LOGW(TAG, "Listening timed out without speech, returning to idle");
    listening_started_ms_.store(0);
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    } else {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::LogListeningDebugReport(uint64_t now_ms, const AudioDebugSnapshot& snapshot) {
    uint64_t last_log_ms = listening_diag_last_log_ms_.load();
    if (last_log_ms != 0 && (now_ms - last_log_ms) < 2000) {
        return;
    }
    listening_diag_last_log_ms_.store(now_ms);

    bool protocol_open = protocol_ && protocol_->IsAudioChannelOpened();
    uint64_t mic_age_ms = snapshot.last_input_ms > 0 && now_ms >= snapshot.last_input_ms
                              ? (now_ms - snapshot.last_input_ms)
                              : 999999999ULL;
    uint64_t listening_age_ms = listening_started_ms_.load() > 0 && now_ms >= listening_started_ms_.load()
                                    ? (now_ms - listening_started_ms_.load())
                                    : 0;
    uint64_t loading_age_ms = listening_server_loading_ms_.load() > 0 && now_ms >= listening_server_loading_ms_.load()
                                  ? (now_ms - listening_server_loading_ms_.load())
                                  : 0;
    bool mic_receiving = snapshot.last_input_ms > 0 && mic_age_ms <= 1500 && snapshot.last_input_peak > 0;
    unsigned queue_depth = ai_chat_queue_ ? static_cast<unsigned>(uxQueueMessagesWaiting(ai_chat_queue_)) : 0;
    const unsigned long listening_age_ms_log = static_cast<unsigned long>(listening_age_ms);
    const unsigned long loading_age_ms_log = static_cast<unsigned long>(loading_age_ms);
    const unsigned long mic_age_ms_log = static_cast<unsigned long>(mic_age_ms);
    ESP_LOGI(TAG,
             "[AI-LISTEN-DIAG] mode=%d proto_open=%s listen_age_ms=%lu loading_age_ms=%lu "
             "mic_receiving=%s mic_age_ms=%lu mic_peak=%d mic_avg=%d vad=%s input_en=%s output_en=%s "
             "wake=%s processor=%s ai_q=%u ai_drop=%u enc_q=%u dec_q=%u send_q=%u play_q=%u",
             static_cast<int>(listening_mode_),
             protocol_open ? "true" : "false",
             listening_age_ms_log,
             loading_age_ms_log,
             mic_receiving ? "true" : "false",
             mic_age_ms_log,
             snapshot.last_input_peak,
             snapshot.last_input_avg_abs,
             snapshot.voice_detected ? "true" : "false",
             snapshot.input_enabled ? "true" : "false",
             snapshot.output_enabled ? "true" : "false",
             snapshot.wake_word_running ? "true" : "false",
             snapshot.audio_processor_running ? "true" : "false",
             queue_depth,
             static_cast<unsigned>(ai_chat_drop_count_.load()),
             static_cast<unsigned>(snapshot.encode_queue_size),
             static_cast<unsigned>(snapshot.decode_queue_size),
             static_cast<unsigned>(snapshot.send_queue_size),
             static_cast<unsigned>(snapshot.playback_queue_size));
}

ListeningMode Application::GetDefaultListeningMode() const {
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [this, display](int progress, size_t speed) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
        Schedule([display, message = std::string(buffer)]() {
            display->SetChatMessage("system", message.c_str());
        });
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload = std::move(payload)]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    if (GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGD(TAG, "Skip local SFX outside idle state");
        return;
    }
    audio_service_.PlaySound(sound);
}

void Application::PlayOverlaySound(const std::string_view& sound) {
    // Idle only, same as PlaySound. The overlay lane used to play through the
    // AI session and duck the voice lane instead, but the two talking over
    // each other on one mono speaker never read well, and there is no working
    // AEC (device AEC has no reference signal wired, server AEC is off in this
    // build) so anything through the speaker while the mic is live bleeds into
    // what the AI hears. Reaction sounds are dropped, not deferred: a reaction
    // replayed after the conversation ends no longer matches what triggered
    // it. AudioService::SetSfxMuted is the backstop for a clip already in
    // flight when the session starts.
    //
    // CanPlayIdleOnlySfx() rather than a bare state check: the server's TTS
    // "stop" flips us to Idle while up to ~2.4s of decoded speech is still
    // sitting in the playback queue, so "Idle" alone does not mean the speaker
    // has gone quiet yet.
    if (!CanPlayIdleOnlySfx()) {
        ESP_LOGD(TAG, "Skip overlay SFX outside idle state");
        return;
    }
    audio_service_.PlayOverlaySound(sound);
}

void Application::InterruptAudioPlaybackForUserInput() {
    // Keep user touch responsive in local idle/config/testing flows by dropping
    // queued local playback immediately.
    auto state = GetDeviceState();
    if (state == kDeviceStateIdle ||
        state == kDeviceStateWifiConfiguring ||
        state == kDeviceStateAudioTesting) {
        audio_service_.ResetDecoder();
    }
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}
