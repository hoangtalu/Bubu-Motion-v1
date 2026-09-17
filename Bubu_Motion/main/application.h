#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <deque>
#include <memory>
#include <atomic>

#include "protocol.h"
#include "ota.h"
#include "audio_service.h"
#include "device_state.h"
#include "device_state_machine.h"

// Main event bits
#define MAIN_EVENT_SCHEDULE             (1 << 0)
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2)
#define MAIN_EVENT_VAD_CHANGE           (1 << 3)
#define MAIN_EVENT_ERROR                (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE      (1 << 5)
#define MAIN_EVENT_CLOCK_TICK           (1 << 6)
#define MAIN_EVENT_NETWORK_CONNECTED    (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT          (1 << 9)
#define MAIN_EVENT_START_LISTENING      (1 << 10)
#define MAIN_EVENT_STOP_LISTENING       (1 << 11)
#define MAIN_EVENT_STATE_CHANGED        (1 << 12)
#define MAIN_EVENT_NETWORK_STARTUP_IDLE (1 << 13)


enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

struct AiChatQueueItem {
    cJSON* root = nullptr;
    uint64_t enqueued_ms = 0;
};

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // Delete copy constructor and assignment operator
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /**
     * Initialize the application
     * This sets up display, audio, network callbacks, etc.
     * Network connection starts asynchronously.
     */
    void Initialize();

    /**
     * Run the main event loop
     * This function runs on the main task and never returns.
     * It handles all events including network, state changes, and user interactions.
     */
    void Run();

    DeviceState GetDeviceState() const { return state_machine_.GetState(); }
    bool IsProtocolReady() const { return protocol_ != nullptr; }
    int AddStateChangeListener(DeviceStateMachine::StateCallback cb) { return state_machine_.AddStateChangeListener(std::move(cb)); }
    void RemoveStateChangeListener(int id) { state_machine_.RemoveStateChangeListener(id); }
    bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }
    
    /**
     * Request state transition
     * Returns true if transition was successful
     */
    bool SetDeviceState(DeviceState state);

    /**
     * Schedule a callback to be executed in the main task
     */
    void Schedule(std::function<void()>&& callback);

    /**
     * Alert with status, message, emotion and optional sound
     */
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();

    void AbortSpeaking(AbortReason reason);

    /**
     * Toggle chat state (event-based, thread-safe)
     * Sends MAIN_EVENT_TOGGLE_CHAT to be handled in Run()
     */
    void ToggleChatState();

    /**
     * Start listening (event-based, thread-safe)
     * Sends MAIN_EVENT_START_LISTENING to be handled in Run()
     */
    void StartListening();

    /**
     * Stop listening (event-based, thread-safe)
     * Sends MAIN_EVENT_STOP_LISTENING to be handled in Run()
     */
    void StopListening();

    /**
     * End the active conversation session and return to idle.
     * Safe to call from board/input callbacks.
     */
    void EndConversation();

    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    bool UpgradeFirmware(const std::string& url, const std::string& version = "");
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    void PlaySound(const std::string_view& sound);
    void PlayOverlaySound(const std::string_view& sound);
    void PlayEmotionalVoice(const std::string& emotion);
    void InterruptAudioPlaybackForUserInput();
    AudioService& GetAudioService() { return audio_service_; }
    
    /**
     * Reset protocol resources (thread-safe)
     * Can be called from any task to release resources allocated after network connected
     * This includes closing audio channel, resetting protocol and ota objects
     */
    void ResetProtocol();

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    DeviceStateMachine state_machine_;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;
    std::string last_error_message_;
    AudioService audio_service_;
    std::unique_ptr<Ota> ota_;

    bool has_server_time_ = false;
    bool aborted_ = false;
    bool assets_version_checked_ = false;
    // Set once ActivationTask() has finished. MaybeRefreshAssetsBundle() must not
    // run Ota::CheckVersion() concurrently with that task, which owns ota_ during
    // boot; device state alone is not a safe proxy, because the activation retry
    // loop can legitimately sit in kDeviceStateIdle.
    std::atomic<bool> activation_done_{false};
    std::atomic<bool> assets_refresh_running_{false};
    bool play_popup_on_listening_ = false;  // Flag to play popup sound after state changes to listening
    int clock_ticks_ = 0;
    TaskHandle_t activation_task_handle_ = nullptr;
    TaskHandle_t ai_chat_task_handle_ = nullptr;
    QueueHandle_t ai_chat_queue_ = nullptr;
    std::mutex response_policy_mutex_;
    bool pending_silent_command_reply_ = false;
    bool suppress_current_tts_reply_ = false;
    std::atomic<bool> last_vad_speaking_{false};
    std::atomic<bool> listening_voice_detected_{false};
    std::atomic<uint64_t> listening_started_ms_{0};
    std::atomic<uint64_t> listening_server_loading_ms_{0};
    std::atomic<uint64_t> listening_diag_last_log_ms_{0};
    std::atomic<uint32_t> ai_chat_drop_count_{0};
    bool pending_ai_chat_mood_reward_ = false;
    std::string bind_required_message_;
    std::string bind_required_code_;
    bool has_bind_required_payload_ = false;


    // Event handlers
    void HandleStateChangedEvent();
    void HandleToggleChatEvent();
    void HandleStartListeningEvent();
    void HandleStopListeningEvent();
    void HandleNetworkConnectedEvent();
    void HandleNetworkDisconnectedEvent();
    void HandleNetworkStartupIdleEvent();
    void HandleActivationDoneEvent();
    void HandleWakeWordDetectedEvent();
    void ContinueOpenAudioChannel(ListeningMode mode);
    void ContinueWakeWordInvoke(const std::string& wake_word);
    void CheckListeningInactivityTimeout();
    void LogListeningDebugReport(uint64_t now_ms, const AudioDebugSnapshot& snapshot);

    // Activation task (runs in background)
    void ActivationTask();

    // Helper methods
    void StartAiChatRuntime();
    void EnqueueIncomingJson(const cJSON* root);
    void AiChatRuntimeTask();
    void ProcessIncomingJsonMessage(const cJSON* root, uint64_t queue_wait_ms);
    void CheckAssetsVersion();
    void CheckNewVersion();
    /* Periodic, idle-only poll for a new assets bundle. The bundle carries the
     * wake word model, so this is how a change made in the parent portal reaches
     * a device that is already running. */
    void MaybeRefreshAssetsBundle();
    void AssetsRefreshTask();
    void InitializeProtocol();
    void RewardMoodForAiChatUse();
    void ShowActivationCode(const std::string& code, const std::string& message);
    void UpdateBindRequiredState(const std::string& message, const std::string& code);
    void ClearBindRequiredState();
    bool CanPlayIdleOnlySfx();
    void SetListeningMode(ListeningMode mode);
    ListeningMode GetDefaultListeningMode() const;
    
    // State change handler called by state machine
    void OnStateChanged(DeviceState old_state, DeviceState new_state);
};


class TaskPriorityReset {
public:
    TaskPriorityReset(BaseType_t priority) {
        original_priority_ = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, priority);
    }
    ~TaskPriorityReset() {
        vTaskPrioritySet(NULL, original_priority_);
    }

private:
    BaseType_t original_priority_;
};

#endif // _APPLICATION_H_
