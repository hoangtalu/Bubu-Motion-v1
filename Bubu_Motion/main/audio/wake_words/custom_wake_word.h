#ifndef CUSTOM_WAKE_WORD_H
#define CUSTOM_WAKE_WORD_H

#include <esp_attr.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <model_path.h>

#include <deque>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "audio_codec.h"
#include "wake_word.h"

class CustomWakeWord : public WakeWord {
public:
    CustomWakeWord();
    ~CustomWakeWord();

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    void Start();
    void Stop();
    size_t GetFeedSize();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }

private:
    static constexpr const char* kLocalCommandPrefix = "__local_cmd__:";

    // Nothing in the firmware ever opens the local-command window
    // (command_window_active_ is only ever set to false), so a non-wake command
    // can never be emitted. Registering those commands anyway is not free:
    // MultiNet decodes one best path over the whole grammar, so every extra
    // phrase is a rival for the same utterance and a "hey bubu" that decodes as
    // "mad" is dropped on the floor. Keep the grammar wake-word-only until
    // Application actually drives the window.
    static constexpr bool kEnableLocalCommands = false;

    struct Command {
        std::string command;
        std::string text;
        std::string action;
        std::string phoneme;
    };

    // multinet 相关成员变量
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* multinet_model_data_ = nullptr;
    srmodel_list_t *models_ = nullptr;
    bool owns_models_ = false;
    char* mn_name_ = nullptr;
    std::string language_ = "cn";
    int duration_ = 3000;
    float threshold_ = 0.2;
    std::deque<Command> commands_;
    bool has_non_wake_actions_ = false;
    bool command_window_active_ = false;
    uint64_t command_window_deadline_us_ = 0;
    uint32_t command_window_timeout_ms_ = 10000;
 
    // MultiNet is free-running here (no WakeNet gate), so its detection window
    // is not aligned to the user's speech: it restarts whenever detect() times
    // out, which lands mid-utterance roughly duration/utterance of the time and
    // silently splits the phrase in half. These track a cheap energy gate so
    // the window can instead be refreshed during silence — see
    // MaybeRefreshDetectionWindow().
    struct ChunkStats {
        float rms = 0.0f;
        int peak = 0;
        int clipped = 0;
        bool is_speech = false;
    };

    float noise_floor_rms_ = 0.0f;
    int silent_chunk_count_ = 0;
    uint64_t window_started_us_ = 0;
    uint32_t window_refresh_count_ = 0;

    // Per-utterance mic telemetry. MultiNet gets raw codec audio here (no AFE,
    // so no AGC and no NS), and there is otherwise no way to tell a wake word
    // the model never scored from one it never properly heard. One INFO line
    // per spoken phrase is the right density for a hit-rate play test.
    bool utterance_active_ = false;
    uint64_t utterance_started_us_ = 0;
    int utterance_peak_ = 0;
    int utterance_chunks_ = 0;
    int utterance_clipped_ = 0;
    float utterance_rms_sum_ = 0.0f;
    int utterance_chunk_samples_ = 0;
    bool utterance_detected_ = false;

    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_;
    std::atomic<bool> running_ = false;
    std::vector<int16_t> input_buffer_;
    std::mutex input_buffer_mutex_;

    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::deque<std::vector<int16_t>> wake_word_pcm_;
    std::deque<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;

    void StoreWakeWordData(const std::vector<int16_t>& data);
    void ParseWakenetModelConfig();
    void AddWakeWordPronunciationVariants();
    bool EmitWakeEventLocked(const std::string& text);
    bool EmitLocalActionEventLocked(const std::string& action);
    void ResetDetectionWindowLocked();
    ChunkStats AnalyzeChunkLocked(const int16_t* samples, size_t count);
    void FlushUtteranceReportLocked();
    void MaybeRefreshDetectionWindowLocked(const ChunkStats& stats);
    static std::string TrimCopy(const std::string& text);
    static std::string NormalizeAction(const std::string& action);
};

#endif
