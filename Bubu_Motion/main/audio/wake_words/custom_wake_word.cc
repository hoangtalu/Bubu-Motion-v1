#include "custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"
#include "assets.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cmath>

#define TAG "CustomWakeWord"

namespace {
constexpr int kMaxMultinetDurationMs = 1500;

// Energy gate used only to decide when it is safe to restart MultiNet's
// detection window (see MaybeRefreshDetectionWindowLocked). Levels are post
// codec input gain, so they are absolute int16 RMS.
constexpr float kAbsoluteSilenceRms = 120.0f;   // below this it is silence no matter the floor
constexpr float kSpeechOverFloorRatio = 2.5f;   // ...or this much above the tracked noise floor
constexpr float kNoiseFloorRiseAlpha = 0.005f;  // floor climbs slowly
constexpr float kNoiseFloorFallAlpha = 0.25f;   // ...and drops fast
constexpr int kSilentChunksBeforeRefresh = 10;  // 10 x 30ms = 300ms of quiet
constexpr int kWindowRefreshAgePercent = 50;    // only bother once the window is half spent
constexpr int kClippedSampleLevel = 32000;      // the codec clamps at +/-INT16_MAX
constexpr int kMinUtteranceChunksToLog = 4;     // ignore sub-120ms clicks and taps

void LogHeapStats(const char* stage) {
    ESP_LOGI(TAG,
        "%s heap: internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
        stage,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}
}  // namespace

CustomWakeWord::CustomWakeWord()
    : wake_word_pcm_(), wake_word_opus_() {
}

CustomWakeWord::~CustomWakeWord() {
    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_model_data_);
        multinet_model_data_ = nullptr;
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }

    if (owns_models_ && models_ != nullptr) {
        esp_srmodel_deinit(models_);
    }
}

std::string CustomWakeWord::TrimCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

std::string CustomWakeWord::NormalizeAction(const std::string& action) {
    std::string normalized = TrimCopy(action);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

bool CustomWakeWord::EmitWakeEventLocked(const std::string& text) {
    std::string wake_text = TrimCopy(text);
    if (wake_text.empty()) {
        wake_text = "wake";
    }
    last_detected_wake_word_ = wake_text;
    if (wake_word_detected_callback_) {
        wake_word_detected_callback_(last_detected_wake_word_);
        return true;
    }
    return false;
}

bool CustomWakeWord::EmitLocalActionEventLocked(const std::string& action) {
    std::string normalized_action = TrimCopy(action);
    std::transform(normalized_action.begin(), normalized_action.end(), normalized_action.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalized_action.empty()) {
        return false;
    }
    last_detected_wake_word_ = std::string(kLocalCommandPrefix) + normalized_action;
    if (wake_word_detected_callback_) {
        wake_word_detected_callback_(last_detected_wake_word_);
        return true;
    }
    return false;
}

void CustomWakeWord::ParseWakenetModelConfig() {
    // Read index.json
    auto& assets = Assets::GetInstance();
    void* ptr = nullptr;
    size_t size = 0;
    if (!assets.GetAssetData("index.json", ptr, size)) {
        ESP_LOGE(TAG, "Failed to read index.json");
        return;
    }
    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse index.json");
        return;
    }
    cJSON* multinet_model = cJSON_GetObjectItem(root, "multinet_model");
    if (cJSON_IsObject(multinet_model)) {
        cJSON* language = cJSON_GetObjectItem(multinet_model, "language");
        cJSON* duration = cJSON_GetObjectItem(multinet_model, "duration");
        cJSON* threshold = cJSON_GetObjectItem(multinet_model, "threshold");
        cJSON* commands = cJSON_GetObjectItem(multinet_model, "commands");
        if (cJSON_IsString(language)) {
            language_ = language->valuestring;
        }
        if (cJSON_IsNumber(duration)) {
            duration_ = duration->valueint;
        }
        if (cJSON_IsNumber(threshold)) {
            threshold_ = threshold->valuedouble;
        }
        if (cJSON_IsArray(commands)) {
            for (int i = 0; i < cJSON_GetArraySize(commands); i++) {
                cJSON* command = cJSON_GetArrayItem(commands, i);
                if (cJSON_IsObject(command)) {
                    cJSON* command_name = cJSON_GetObjectItem(command, "command");
                    cJSON* text = cJSON_GetObjectItem(command, "text");
                    cJSON* action = cJSON_GetObjectItem(command, "action");
                    cJSON* phoneme = cJSON_GetObjectItem(command, "phoneme");
                    if (cJSON_IsString(command_name) && cJSON_IsString(text) && cJSON_IsString(action)) {
                        std::string phoneme_value;
                        if (cJSON_IsString(phoneme)) {
                            phoneme_value = phoneme->valuestring;
                        }
                        commands_.push_back({command_name->valuestring, text->valuestring, action->valuestring, phoneme_value});
                        ESP_LOGI(TAG, "Command: %s, Text: %s, Action: %s, Phoneme: %s",
                            command_name->valuestring,
                            text->valuestring,
                            action->valuestring,
                            phoneme_value.empty() ? "<none>" : phoneme_value.c_str());
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}


// MultiNet matches one phrase against one phoneme sequence, so a wake word
// spelled a single way only fires for speakers who say it that way — and a
// miss is total, not a low score, which is why cranking the threshold does not
// rescue it. Register the realistic pronunciations as extra phrases, all mapped
// to the wake action, so any of them wakes the device.
//
// These come from Kconfig rather than the assets because this board flashes a
// prebuilt assets.bin (CONFIG_FLASH_CUSTOM_ASSETS), so an assets-side variant
// list would need that blob regenerated; this one takes effect on an app flash.
void CustomWakeWord::AddWakeWordPronunciationVariants() {
#ifdef CONFIG_CUSTOM_WAKE_WORD_PHONEME_VARIANTS
    std::string variants = CONFIG_CUSTOM_WAKE_WORD_PHONEME_VARIANTS;
    if (TrimCopy(variants).empty()) {
        return;
    }

    const Command* wake_command = nullptr;
    for (const auto& cmd : commands_) {
        if (NormalizeAction(cmd.action) == "wake") {
            wake_command = &cmd;
            break;
        }
    }
    if (wake_command == nullptr) {
        ESP_LOGW(TAG, "Extra pronunciations configured but no wake command to attach them to");
        return;
    }

    std::string base_command = wake_command->command;
    std::string base_text = wake_command->text;
    std::deque<Command> variant_commands;

    size_t start = 0;
    int index = 0;
    while (start <= variants.size()) {
        size_t sep = variants.find(';', start);
        std::string phoneme = TrimCopy(variants.substr(start,
            sep == std::string::npos ? std::string::npos : sep - start));
        if (!phoneme.empty()) {
            bool duplicate = false;
            for (const auto& cmd : commands_) {
                if (cmd.phoneme == phoneme) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                // The phrase string is the key esp_mn_command_search() uses, so
                // it has to be distinct; the display text stays shared so every
                // variant reports the same wake word upstream.
                variant_commands.push_back({
                    base_command + " alt" + std::to_string(++index),
                    base_text,
                    "wake",
                    phoneme});
            }
        }
        if (sep == std::string::npos) {
            break;
        }
        start = sep + 1;
    }

    for (auto& cmd : variant_commands) {
        ESP_LOGI(TAG, "Extra wake pronunciation: '%s' phoneme=%s",
            cmd.command.c_str(), cmd.phoneme.c_str());
        commands_.push_back(std::move(cmd));
    }
#endif
}

bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    commands_.clear();
    has_non_wake_actions_ = false;
    command_window_active_ = false;
    command_window_deadline_us_ = 0;

    if (models_list == nullptr) {
        language_ = "cn";
        models_ = esp_srmodel_init("model");
        owns_models_ = true;
#ifdef CONFIG_CUSTOM_WAKE_WORD
        threshold_ = CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
        commands_.push_back({CONFIG_CUSTOM_WAKE_WORD, CONFIG_CUSTOM_WAKE_WORD_DISPLAY, "wake", ""});
#endif
    } else {
        models_ = models_list;
        owns_models_ = false;
        ParseWakenetModelConfig();
    }

    AddWakeWordPronunciationVariants();

    for (const auto& cmd : commands_) {
        if (NormalizeAction(cmd.action) != "wake") {
            has_non_wake_actions_ = true;
            break;
        }
    }

    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize wakenet model");
        return false;
    }

    // 初始化 multinet (命令词识别)
    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (mn_name_ == nullptr) {
        ESP_LOGW(TAG, "Language '%s' multinet not found, falling back to any multinet model", language_.c_str());
        mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, NULL);
    }
    if (mn_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize multinet, mn_name is nullptr");
        ESP_LOGI(TAG, "Please refer to https://pcn7cs20v8cr.feishu.cn/wiki/CpQjwQsCJiQSWSkYEvrcxcbVnwh to add custom wake word");
        return false;
    }

    if (duration_ > kMaxMultinetDurationMs) {
        ESP_LOGW(TAG, "Clamping multinet duration from %d ms to %d ms", duration_, kMaxMultinetDurationMs);
        duration_ = kMaxMultinetDurationMs;
    }

    multinet_ = esp_mn_handle_from_name(mn_name_);
    if (multinet_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get multinet handle for model: %s", mn_name_);
        return false;
    }

    LogHeapStats("Before multinet create");
    multinet_model_data_ = multinet_->create(mn_name_, duration_);
    if (multinet_model_data_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create multinet model data for model: %s", mn_name_);
        return false;
    }
    LogHeapStats("After multinet create");

    multinet_->set_det_threshold(multinet_model_data_, threshold_);
    esp_mn_commands_clear();

    std::deque<Command> accepted_commands;
    accepted_commands.clear();
    for (const auto& cmd : commands_) {
        std::string command_text = TrimCopy(cmd.command);
        std::string command_phoneme = TrimCopy(cmd.phoneme);
        if (command_text.empty()) {
            ESP_LOGW(TAG, "Skipping empty local command text");
            continue;
        }

        // Every phrase in the grammar competes with the wake word for the same
        // utterance, and a non-wake win is discarded (the command window is
        // never opened), so it costs wake-word hit rate and buys nothing.
        if (!kEnableLocalCommands && NormalizeAction(cmd.action) != "wake") {
            ESP_LOGI(TAG, "Not registering local command '%s' (action=%s): local commands are disabled, "
                "keeping the multinet grammar wake-word-only",
                command_text.c_str(), cmd.action.c_str());
            continue;
        }

        int command_id = static_cast<int>(accepted_commands.size()) + 1;
        esp_err_t add_ret = ESP_ERR_INVALID_STATE;
        if (!command_phoneme.empty()) {
            add_ret = esp_mn_commands_phoneme_add(command_id, command_text.c_str(), command_phoneme.c_str());
            if (add_ret != ESP_OK) {
                ESP_LOGW(TAG, "Phoneme registration failed for '%s' (%s), trying text registration fallback",
                    command_text.c_str(), command_phoneme.c_str());
            }
        }

        if (add_ret != ESP_OK) {
            add_ret = esp_mn_commands_add(command_id, command_text.c_str());
        }

        if (add_ret != ESP_OK) {
            ESP_LOGW(TAG, "Skipping unsupported local command: '%s' (action=%s)",
                command_text.c_str(), cmd.action.c_str());
            continue;
        }

        Command accepted = cmd;
        accepted.command = command_text;
        accepted.phoneme = command_phoneme;
        accepted_commands.push_back(std::move(accepted));
    }

    if (accepted_commands.empty()) {
        ESP_LOGE(TAG, "No valid local commands accepted by multinet model");
        return false;
    }

    bool has_wake_command = false;
    has_non_wake_actions_ = false;
    for (const auto& cmd : accepted_commands) {
        if (NormalizeAction(cmd.action) == "wake") {
            has_wake_command = true;
        } else {
            has_non_wake_actions_ = true;
        }
    }

    if (!has_wake_command) {
        ESP_LOGE(TAG, "No valid wake command accepted by multinet model");
        return false;
    }

    commands_ = std::move(accepted_commands);

    esp_mn_error_t* update_error = esp_mn_commands_update();
    if (update_error != nullptr && update_error->num > 0) {
        ESP_LOGE(TAG, "Multinet rejected %d command(s) during update", update_error->num);
        for (int i = 0; i < update_error->num; ++i) {
            if (update_error->phrases[i] != nullptr && update_error->phrases[i]->string != nullptr) {
                ESP_LOGE(TAG, "Rejected command: %s", update_error->phrases[i]->string);
            }
        }
        return false;
    }

    // The knob that actually decides sensitivity is the `threshold` in the
    // assets' index.json, NOT CONFIG_CUSTOM_WAKE_WORD_THRESHOLD — that Kconfig
    // value is only read on the models_list == nullptr path, which this board
    // never takes. Log the effective numbers so a field log says which is live.
    ESP_LOGI(TAG, "Wake word ready: model=%s lang=%s threshold=%.2f duration=%dms phrases=%d chunk=%d samples",
        mn_name_, language_.c_str(), threshold_, duration_,
        static_cast<int>(commands_.size()),
        multinet_->get_samp_chunksize(multinet_model_data_));
    multinet_->print_active_speech_commands(multinet_model_data_);
    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void CustomWakeWord::Start() {
    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    command_window_active_ = false;
    command_window_deadline_us_ = 0;

    // Detection is stopped for the whole AI session, during which the mic has
    // been carrying the user's speech and the speaker's TTS. MultiNet keeps its
    // decoder state across that gap, so without a clean the first utterance
    // after a session is decoded on top of stale context. Clear both buffers.
    input_buffer_.clear();
    if (multinet_model_data_ != nullptr) {
        multinet_->clean(multinet_model_data_);
    }
    ResetDetectionWindowLocked();
    noise_floor_rms_ = 0.0f;
    silent_chunk_count_ = 0;
    utterance_active_ = false;

    running_ = true;
}

void CustomWakeWord::Stop() {
    running_ = false;
    command_window_active_ = false;
    command_window_deadline_us_ = 0;

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    input_buffer_.clear();
}

void CustomWakeWord::ResetDetectionWindowLocked() {
    window_started_us_ = esp_timer_get_time();
}

// Level/energy analysis of one MultiNet chunk. Drives both the speech gate that
// aligns the detection window and the per-utterance mic report below, which is
// the only way to tell "the model scored the phrase and rejected it" apart from
// "the audio reaching the model was clipped or too quiet to score at all".
CustomWakeWord::ChunkStats CustomWakeWord::AnalyzeChunkLocked(const int16_t* samples, size_t count) {
    ChunkStats stats;
    if (count == 0) {
        return stats;
    }

    uint64_t sum_squares = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t value = samples[i];
        int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > stats.peak) {
            stats.peak = magnitude;
        }
        // The codec clamps to +/-INT16_MAX, so anything at the rail was cut off.
        if (magnitude >= kClippedSampleLevel) {
            ++stats.clipped;
        }
        sum_squares += static_cast<uint64_t>(value * value);
    }
    stats.rms = sqrtf(static_cast<float>(sum_squares / count));

    if (noise_floor_rms_ <= 0.0f) {
        noise_floor_rms_ = stats.rms;
    } else {
        float alpha = stats.rms < noise_floor_rms_ ? kNoiseFloorFallAlpha : kNoiseFloorRiseAlpha;
        noise_floor_rms_ += (stats.rms - noise_floor_rms_) * alpha;
    }
    stats.is_speech = stats.rms > kAbsoluteSilenceRms &&
        stats.rms > noise_floor_rms_ * kSpeechOverFloorRatio;

    if (stats.is_speech) {
        silent_chunk_count_ = 0;
        if (!utterance_active_) {
            utterance_active_ = true;
            utterance_started_us_ = esp_timer_get_time();
            utterance_peak_ = 0;
            utterance_chunks_ = 0;
            utterance_clipped_ = 0;
            utterance_rms_sum_ = 0.0f;
            utterance_detected_ = false;
        }
    } else if (silent_chunk_count_ < kSilentChunksBeforeRefresh) {
        ++silent_chunk_count_;
    }

    if (utterance_active_) {
        ++utterance_chunks_;
        utterance_clipped_ += stats.clipped;
        utterance_rms_sum_ += stats.rms;
        if (stats.peak > utterance_peak_) {
            utterance_peak_ = stats.peak;
        }
        utterance_chunk_samples_ = static_cast<int>(count);
        // Close the utterance once the gate has been quiet long enough that a
        // detection for it would already have been emitted.
        if (!stats.is_speech && silent_chunk_count_ >= kSilentChunksBeforeRefresh) {
            FlushUtteranceReportLocked();
        }
    }

    return stats;
}

// One INFO line per spoken phrase: enough to run a hit-rate play test and read
// off whether the misses were clipped, too quiet, or perfectly well-levelled
// audio the model simply did not match.
void CustomWakeWord::FlushUtteranceReportLocked() {
    if (!utterance_active_) {
        return;
    }
    utterance_active_ = false;
    if (utterance_chunks_ < kMinUtteranceChunksToLog) {
        return;
    }

    int samples_total = utterance_chunks_ * utterance_chunk_samples_;
    ESP_LOGI(TAG,
        "[MIC] utterance %ums peak=%d avg_rms=%.0f clipped=%.1f%% floor=%.0f gain=%.1f -> %s",
        static_cast<unsigned>((esp_timer_get_time() - utterance_started_us_) / 1000ULL),
        utterance_peak_,
        utterance_rms_sum_ / utterance_chunks_,
        samples_total > 0 ? (100.0f * utterance_clipped_ / samples_total) : 0.0f,
        noise_floor_rms_,
        codec_ != nullptr ? codec_->input_gain() : 0.0f,
        utterance_detected_ ? "WAKE" : "no match");
}

// MultiNet here runs free, with no WakeNet in front of it, so its detection
// window is restarted by whatever happens to trip detect()'s timeout rather
// than by the user starting to talk. A boundary landing between "hey" and
// "bubu" splits the phrase across two decodes and neither half matches.
//
// Fix the alignment rather than the odds: once the window is half spent,
// restart it as soon as the room has been quiet for 300ms, so the boundary
// falls between phrases instead of inside one.
void CustomWakeWord::MaybeRefreshDetectionWindowLocked(const ChunkStats& stats) {
    if (stats.is_speech || silent_chunk_count_ < kSilentChunksBeforeRefresh) {
        return;
    }

    uint64_t age_ms = (esp_timer_get_time() - window_started_us_) / 1000ULL;
    if (age_ms * 100 < static_cast<uint64_t>(duration_) * kWindowRefreshAgePercent) {
        return;
    }

    multinet_->clean(multinet_model_data_);
    ResetDetectionWindowLocked();
    ++window_refresh_count_;
    ESP_LOGD(TAG, "Refreshed detection window in silence (age=%ums floor=%.0f count=%u)",
        static_cast<unsigned>(age_ms), noise_floor_rms_,
        static_cast<unsigned>(window_refresh_count_));
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (multinet_model_data_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    // Check running state inside lock to avoid TOCTOU race with Stop()
    if (!running_) {
        return;
    }

    // If input channels is 2, we need to fetch the left channel data
    if (codec_->input_channels() == 2) {
        for (size_t i = 0; i < data.size(); i += 2) {
            input_buffer_.push_back(data[i]);
        }
    } else {
        input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    }
    
    int chunksize = multinet_->get_samp_chunksize(multinet_model_data_);
    while (input_buffer_.size() >= chunksize) {
        std::vector<int16_t> chunk(input_buffer_.begin(), input_buffer_.begin() + chunksize);
        StoreWakeWordData(chunk);
        ChunkStats stats = AnalyzeChunkLocked(chunk.data(), chunk.size());

        if (command_window_active_ && esp_timer_get_time() > command_window_deadline_us_) {
            command_window_active_ = false;
            command_window_deadline_us_ = 0;
            ESP_LOGI(TAG, "Local command window timed out");
        }
        
        esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, chunk.data());

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = multinet_->get_results(multinet_model_data_);
            if (mn_result == nullptr) {
                ESP_LOGW(TAG, "MultiNet reported detection but returned null results");
                multinet_->clean(multinet_model_data_);
                ResetDetectionWindowLocked();
                input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunksize);
                continue;
            }

            // MultiNet returns up to ESP_MN_RESULT_MAX_NUM candidates and the
            // wake word is not always the top one. Take it wherever it lands
            // rather than reading only rank 0 and dropping the rest — a wake
            // word that decoded second used to be a silent miss.
            int wake_index = -1;
            int action_index = -1;
            for (int i = 0; i < mn_result->num; i++) {
                int command_index = mn_result->command_id[i] - 1;
                if (command_index < 0 || command_index >= static_cast<int>(commands_.size())) {
                    ESP_LOGW(TAG, "Invalid command index: %d (size=%d)",
                        command_index, static_cast<int>(commands_.size()));
                    continue;
                }
                ESP_LOGI(TAG, "MultiNet candidate %d/%d: command_id=%d action=%s prob=%.3f string=%s",
                    i + 1, mn_result->num, mn_result->command_id[i],
                    commands_[command_index].action.c_str(), mn_result->prob[i],
                    (mn_result->string != nullptr) ? mn_result->string : "<null>");
                if (NormalizeAction(commands_[command_index].action) == "wake") {
                    if (wake_index < 0) {
                        wake_index = command_index;
                    }
                } else if (action_index < 0) {
                    action_index = command_index;
                }
            }

            if (wake_index >= 0) {
                utterance_detected_ = true;
                FlushUtteranceReportLocked();
                command_window_active_ = false;
                command_window_deadline_us_ = 0;
                running_ = false;
                input_buffer_.clear();
                EmitWakeEventLocked(commands_[wake_index].text);
            } else if (action_index >= 0 && command_window_active_) {
                command_window_active_ = false;
                command_window_deadline_us_ = 0;
                EmitLocalActionEventLocked(commands_[action_index].action);
            } else if (action_index >= 0) {
                // Only reachable with kEnableLocalCommands; kept so the
                // swallowed detection is visible instead of silent.
                ESP_LOGW(TAG, "Dropping local action '%s' outside the command window "
                    "(it may have outranked the wake word for this utterance)",
                    commands_[action_index].action.c_str());
            }
            multinet_->clean(multinet_model_data_);
            ResetDetectionWindowLocked();
        } else if (mn_state == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGD(TAG, "Command word detection timeout, cleaning state");
            multinet_->clean(multinet_model_data_);
            ResetDetectionWindowLocked();
        } else {
            MaybeRefreshDetectionWindowLocked(stats);
        }

        if (!running_) {
            break;
        }
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunksize);
    }
}

size_t CustomWakeWord::GetFeedSize() {
    if (multinet_model_data_ == nullptr) {
        return 0;
    }
    return multinet_->get_samp_chunksize(multinet_model_data_);
}

void CustomWakeWord::StoreWakeWordData(const std::vector<int16_t>& data) {
    // store audio data to wake_word_pcm_
    wake_word_pcm_.push_back(data);
    // keep about 2 seconds of data, detect duration is 30ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void CustomWakeWord::EncodeWakeWordData() {
    const size_t stack_size = 4096 * 7;
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        assert(wake_word_encode_task_stack_ != nullptr);
    }
    if (wake_word_encode_task_buffer_ == nullptr) {
        wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        assert(wake_word_encode_task_buffer_ != nullptr);
    }

    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (CustomWakeWord*)arg;
        {
            auto start_time = esp_timer_get_time();
            // Create encoder
            esp_opus_enc_config_t opus_enc_cfg = AS_OPUS_ENC_CONFIG();
            void* encoder_handle = nullptr;
            auto ret = esp_opus_enc_open(&opus_enc_cfg, sizeof(esp_opus_enc_config_t), &encoder_handle);
            if (encoder_handle == nullptr) {
                ESP_LOGE(TAG, "Failed to create audio encoder, error code: %d", ret);
                std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                this_->wake_word_opus_.push_back(std::vector<uint8_t>());
                this_->wake_word_cv_.notify_all();
                return;
            }
            // Get frame size
            int frame_size = 0;
            int outbuf_size = 0;
            esp_opus_enc_get_frame_size(encoder_handle, &frame_size, &outbuf_size);
            frame_size = frame_size / sizeof(int16_t);
            // Encode all PCM data
            int packets = 0;
            std::vector<int16_t> in_buffer;
            esp_audio_enc_in_frame_t in = {};
            esp_audio_enc_out_frame_t out = {};
            for (auto& pcm: this_->wake_word_pcm_) {
                if (in_buffer.empty()) {
                    in_buffer = std::move(pcm);
                } else {
                    in_buffer.reserve(in_buffer.size() + pcm.size());
                    in_buffer.insert(in_buffer.end(), pcm.begin(), pcm.end());
                }
                while (in_buffer.size() >= frame_size) {
                    std::vector<uint8_t> opus_buf(outbuf_size);
                    in.buffer = (uint8_t *)(in_buffer.data());
                    in.len = (uint32_t)(frame_size * sizeof(int16_t));
                    out.buffer = opus_buf.data();
                    out.len = outbuf_size;
                    out.encoded_bytes = 0;
                    ret = esp_opus_enc_process(encoder_handle, &in, &out);
                    if (ret == ESP_AUDIO_ERR_OK) {
                        std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                        this_->wake_word_opus_.emplace_back(opus_buf.data(), opus_buf.data() + out.encoded_bytes);
                        this_->wake_word_cv_.notify_all();
                        packets++;
                    } else {
                        ESP_LOGE(TAG, "Failed to encode audio, error code: %d", ret);
                    }
                    in_buffer.erase(in_buffer.begin(), in_buffer.begin() + frame_size);
                }
            }
            this_->wake_word_pcm_.clear();
            // Close encoder
            esp_opus_enc_close(encoder_handle);
            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", packets, (long)((end_time - start_time) / 1000));

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 2, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool CustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
