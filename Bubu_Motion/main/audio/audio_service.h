#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <memory>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/idf_additions.h>  // xTaskCreateWithCaps/vTaskDeleteWithCaps for the PSRAM-pinned sfx_codec task
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <model_path.h>
#include "esp_audio_enc.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include "esp_ae_rate_cvt.h"
#include "esp_audio_types.h"

#include "audio_codec.h"
#include "audio_processor.h"
#include "processors/audio_debugger.h"
#include "wake_word.h"
#include "protocol.h"
#include "ogg_demuxer.h"
#include "emotion_voice_map.h"

/*
 * There are two types of audio data flow:
 * 1. (MIC) -> [Processors] -> {Encode Queue} -> [Opus Encoder] -> {Send Queue} -> (Server)
 * 2. (Server) -> {Decode Queue} -> [Opus Decoder] -> {Playback Queue} -> (Speaker)
 *
 * We use one task for MIC / Speaker / Processors, and one task for Opus Encoder / Opus Decoder.
 * 
 * Decode Queue and Send Queue are the main queues, because Opus packets are quite smaller than PCM packets.
 * 
 */

#define OPUS_FRAME_DURATION_MS 60
#define MAX_ENCODE_TASKS_IN_QUEUE 2
#define MAX_PLAYBACK_TASKS_IN_QUEUE 2
#define MAX_DECODE_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)
#define MAX_SEND_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)
#define AUDIO_TESTING_MAX_DURATION_MS 10000
#define MAX_TIMESTAMPS_IN_QUEUE 3

// Overlay lane: local sounds triggered by touch/mischief reactions get their
// own decoder and queue so they never queue up behind the main voice lane
// (network TTS + Bubu's own emotional voice lines). The two lanes are never
// meant to be audible at once: SFX is hard-muted for the whole AI session
// (see SetSfxMuted) and only resumes once the device is back to Idle. The
// mix in AudioOutputTask exists only to cover the brief window where a
// local sound and residual voice playback overlap while not muted; there is
// deliberately no gain ducking anymore.
//
// PlayOverlaySound's OggDemuxer parses the whole embedded clip synchronously
// in one call, firing one decode-queue push per ~60ms Opus packet back to
// back — even for the longest clips in the mischief pool (vox_mumble_*/
// vox_hum_*, ~3s) that happens far faster than OpusCodecTask, a
// separate task, can drain them. The decode queue has to be sized to
// absorb a whole clip's worth of (small, compressed) packets up front, or
// everything past the first few packets gets silently dropped and the
// sound cuts off almost immediately. 20s of headroom comfortably covers
// today's longest asset with margin for future ones.
// Every overlay asset in main/assets/common is encoded as 20ms Opus packets
// (verified against their TOC bytes), against OPUS_FRAME_DURATION_MS's 60 for
// the network voice lane. Queue bounds counted in packets have to use this one
// or they mean a third of what they say.
#define OPUS_SFX_FRAME_DURATION_MS 20
// Compressed-packet backlog, deliberately left at the count it has always had
// rather than rescaled to the real packet size. At 20ms packets this is ~6.6s
// of audio, not the 20s the old expression implied. Today's longest overlay
// clip is ~3s so nothing is cut, but a clip past ~6.6s would lose its tail. Growing it
// is the wrong fix: payloads are ~79 bytes each, and anything under
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (2048) is allocated from INTERNAL RAM,
// so covering a 17s clip would park ~100KB in exactly the pool AFE needs to
// initialize. The real fix is to feed the demuxer incrementally instead of
// parsing a whole clip synchronously into this queue.
#define MAX_SFX_DECODE_PACKETS_IN_QUEUE 333

// Decoded-PCM depth for the overlay lane. This is what actually protects the
// speaker from scheduling jitter, and 2 frames was far too shallow: the assets
// are 20ms packets (not the 60ms this file used to assume), so "2" was 40ms of
// audio, produced by SfxCodecTask — the LOWEST-priority audio task, competing
// with LVGL and the AFE task at the same or higher priority. Any hiccup longer
// than 40ms underran the I2S stream, which is heard as a glitch/crackle partway
// through nearly every clip. 12 frames is ~240ms of slack and costs ~9KB while
// a clip is playing (nothing when idle).
#define MAX_SFX_PLAYBACK_TASKS_IN_QUEUE 12

#define AUDIO_POWER_TIMEOUT_MS 15000
#define AUDIO_POWER_CHECK_INTERVAL_MS 1000

#define AS_EVENT_AUDIO_TESTING_RUNNING      (1 << 0)
#define AS_EVENT_WAKE_WORD_RUNNING          (1 << 1)
#define AS_EVENT_AUDIO_PROCESSOR_RUNNING    (1 << 2)
#define AS_EVENT_PLAYBACK_NOT_EMPTY         (1 << 3)

#define AS_OPUS_GET_FRAME_DRU_ENUM(duration_ms)                   \
    ((duration_ms) == 5 ? ESP_OPUS_ENC_FRAME_DURATION_5_MS :      \
     (duration_ms) == 10 ? ESP_OPUS_ENC_FRAME_DURATION_10_MS :    \
     (duration_ms) == 20 ? ESP_OPUS_ENC_FRAME_DURATION_20_MS :    \
     (duration_ms) == 40 ? ESP_OPUS_ENC_FRAME_DURATION_40_MS :    \
     (duration_ms) == 60 ? ESP_OPUS_ENC_FRAME_DURATION_60_MS :    \
     (duration_ms) == 80 ? ESP_OPUS_ENC_FRAME_DURATION_80_MS :    \
     (duration_ms) == 100 ? ESP_OPUS_ENC_FRAME_DURATION_100_MS :  \
     (duration_ms) == 120 ? ESP_OPUS_ENC_FRAME_DURATION_120_MS : -1)

#define AS_OPUS_ENC_CONFIG() {                                                                                    \
        .sample_rate        = ESP_AUDIO_SAMPLE_RATE_16K,                                                          \
        .channel            = ESP_AUDIO_MONO,                                                                     \
        .bits_per_sample    = ESP_AUDIO_BIT16,                                                                    \
        .bitrate            = ESP_OPUS_BITRATE_AUTO,                                                              \
        .frame_duration     = (esp_opus_enc_frame_duration_t)AS_OPUS_GET_FRAME_DRU_ENUM(OPUS_FRAME_DURATION_MS),  \
        .application_mode   = ESP_OPUS_ENC_APPLICATION_AUDIO,                                                     \
        .complexity         = 0,                                                                                  \
        .enable_fec         = false,                                                                              \
        .enable_dtx         = true,                                                                               \
        .enable_vbr         = true,                                                                               \
    }

struct AudioServiceCallbacks {
    std::function<void(void)> on_send_queue_available;
    std::function<void(const std::string&)> on_wake_word_detected;
    std::function<void(bool)> on_vad_change;
    std::function<void(void)> on_audio_testing_queue_full;
};


enum AudioTaskType {
    kAudioTaskTypeEncodeToSendQueue,
    kAudioTaskTypeEncodeToTestingQueue,
    kAudioTaskTypeDecodeToPlaybackQueue,
};

struct AudioTask {
    AudioTaskType type;
    std::vector<int16_t> pcm;
    uint32_t timestamp;
};

struct DebugStatistics {
    uint32_t input_count = 0;
    uint32_t decode_count = 0;
    uint32_t encode_count = 0;
    uint32_t playback_count = 0;
};

struct AudioDebugSnapshot {
    uint64_t last_input_ms = 0;
    int last_input_peak = 0;
    int last_input_avg_abs = 0;
    bool voice_detected = false;
    bool input_enabled = false;
    bool output_enabled = false;
    bool wake_word_running = false;
    bool audio_processor_running = false;
    size_t encode_queue_size = 0;
    size_t decode_queue_size = 0;
    size_t send_queue_size = 0;
    size_t playback_queue_size = 0;
    size_t testing_queue_size = 0;
    size_t sfx_decode_queue_size = 0;
    size_t sfx_playback_queue_size = 0;
};

class AudioService {
public:
    AudioService();
    ~AudioService();

    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();
    void EncodeWakeWord();
    std::unique_ptr<AudioStreamPacket> PopWakeWordPacket();
    const std::string& GetLastWakeWord() const;
    bool IsVoiceDetected() const { return voice_detected_; }
    bool IsIdle();
    void WaitForPlaybackQueueEmpty();
    bool IsWakeWordRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING; }
    bool IsAudioProcessorRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING; }
    bool IsAfeWakeWord();
    AudioDebugSnapshot GetDebugSnapshot();

    void EnableWakeWordDetection(bool enable);
    void EnableVoiceProcessing(bool enable);
    // True once the audio processor is actually usable. Retries Initialize()
    // on every call until it succeeds — a failed init must not latch.
    bool EnsureAudioProcessorInitialized();
    void EnableAudioTesting(bool enable);
    void EnableDeviceAec(bool enable);

    void SetCallbacks(AudioServiceCallbacks& callbacks);

    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait = false);
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();
    void PlaySound(const std::string_view& sound);
    void PlayOverlaySound(const std::string_view& sound);
    void PlayEmotionalVoice(const std::string& emotion);
    bool ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples);
    void ResetDecoder();
    void SetModelsList(srmodel_list_t* models_list);

    // Mute/unmute the SFX overlay lane. Muting also flushes whatever is
    // already queued on it, so the lane goes quiet immediately rather than
    // draining the rest of the current clip under the AI's voice.
    void SetSfxMuted(bool muted);
    bool IsSfxMuted() const { return sfx_muted_.load(); }
    // Milliseconds of voice-lane audio handed to the codec since boot. Only
    // advances while the speaker is actually playing voice, so it is a playback
    // clock (the chat subtitles pace against it), not wall time.
    uint64_t GetVoicePlayedMs() const { return voice_played_ms_.load(); }

private:
    AudioCodec* codec_ = nullptr;
    AudioServiceCallbacks callbacks_;
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<WakeWord> wake_word_;
    std::unique_ptr<AudioDebugger> audio_debugger_;
    std::unique_ptr<EmotionVoiceMap> emotion_voice_map_;
    void* opus_encoder_ = nullptr;
    void* opus_decoder_ = nullptr;
    std::mutex decoder_mutex_;
    std::mutex input_resampler_mutex_;
    esp_ae_rate_cvt_handle_t input_resampler_ = nullptr;
    esp_ae_rate_cvt_handle_t output_resampler_ = nullptr;

    // Overlay (SFX) lane: independent decoder/resampler so it never blocks on
    // or gets blocked by the main voice lane's decoder state.
    void* sfx_opus_decoder_ = nullptr;
    std::mutex sfx_decoder_mutex_;
    esp_ae_rate_cvt_handle_t sfx_output_resampler_ = nullptr;
    int sfx_decoder_sample_rate_ = 0;
    int sfx_decoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
    int sfx_decoder_frame_size_ = 0;

    // Encoder/Decoder state
    int encoder_sample_rate_ = 16000;
    int encoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
    int encoder_frame_size_ = 0;
    int encoder_outbuf_size_ = 0;
    int decoder_sample_rate_ = 0;
    int decoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
    int decoder_frame_size_ = 0;
    DebugStatistics debug_statistics_;
    srmodel_list_t* models_list_ = nullptr;

    EventGroupHandle_t event_group_;

    // Audio encode / decode
    TaskHandle_t audio_input_task_handle_ = nullptr;
    TaskHandle_t audio_output_task_handle_ = nullptr;
    TaskHandle_t opus_codec_task_handle_ = nullptr;
    TaskHandle_t sfx_codec_task_handle_ = nullptr;
    std::mutex audio_queue_mutex_;
    std::condition_variable audio_queue_cv_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_decode_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_send_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_testing_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_encode_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_playback_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_sfx_decode_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_sfx_playback_queue_;
    // When set, the overlay lane is silenced end to end: new clips are
    // refused, in-flight packets are dropped and anything already decoded is
    // discarded instead of being played. Set for the whole AI session by
    // Application's state-change listener. Atomic because it is read on the
    // audio tasks and written from whichever task drives the transition.
    std::atomic<bool> sfx_muted_{false};
    std::atomic<uint64_t> voice_played_ms_{0};
    // For server AEC
    std::deque<uint32_t> timestamp_queue_;

    bool wake_word_initialized_ = false;
    // Only ever true when Initialize() actually produced a working processor
    // (feed size > 0); see EnsureAudioProcessorInitialized.
    bool audio_processor_initialized_ = false;
    bool voice_detected_ = false;
    bool service_stopped_ = true;
    bool audio_input_need_warmup_ = false;
    std::atomic<uint64_t> last_input_ms_{0};
    std::atomic<int> last_input_peak_{0};
    std::atomic<int> last_input_avg_abs_{0};

    esp_timer_handle_t audio_power_timer_ = nullptr;
    std::chrono::steady_clock::time_point last_input_time_;
    std::chrono::steady_clock::time_point last_output_time_;

    void AudioInputTask();
    void AudioOutputTask();
    void OpusCodecTask();
    void SfxCodecTask();
    void PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm);
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    void SetSfxDecodeSampleRate(int sample_rate, int frame_duration);
    bool PushPacketToSfxDecodeQueue(std::unique_ptr<AudioStreamPacket> packet);
    void CheckAndUpdateAudioPowerState();
};

#endif
