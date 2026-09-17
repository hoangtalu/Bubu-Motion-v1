#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Subtitles for the eyes screen: what Bubu says, a few words at a time, paced to
// the speaker rather than to the network.
//
// Why pacing is needed at all: the gateway forwards Gemini's output
// transcription the moment it arrives, but releases the audio at playback speed
// (bubu-gateway device-session.ts, AUDIO_LEAD_MS). Text therefore lands seconds
// ahead of the voice on a long reply. Showing it on arrival would run ahead of
// Bubu's mouth.
//
// So the clock here is milliseconds of voice actually played by the speaker
// (AudioService::GetVoicePlayedMs), not wall time. It stops when playback
// stalls, so the subtitles stall with it. Each word is given an estimated
// duration; a chunk is shown once the played audio reaches its first word.
//
// Chunks are packed by rendered width, not by word count: Vietnamese syllables
// vary from "a" to "nghiêng", and the visible chord of the round panel is what
// limits a line.
//
// Pure logic -- no LVGL. The caller supplies a width measurement, calls Tick()
// periodically and draws the returned text (empty = hide).
class ChatSubtitle {
public:
    using MeasureFn = std::function<int(const std::string& text)>;

    struct Config {
        int max_width_px = 180;
        // Starting estimate of speech timing. Each reply that plays to the end
        // re-scales it (Calibrate), within [pace_scale_min, pace_scale_max].
        uint32_t ms_per_word = 260;
        uint32_t comma_pause_ms = 150;
        uint32_t sentence_pause_ms = 350;
        // A fragment that does not end in whitespace may be cut mid-word; hold
        // its tail this long for the rest before treating it as a whole word.
        uint32_t partial_word_hold_ms = 500;
        // Voice silent this long while chunks are still unshown: the estimate
        // was too slow for this reply, so drain the rest at a fixed pace.
        uint32_t catchup_after_idle_ms = 1200;
        uint32_t catchup_chunk_ms = 450;
        // Voice silent this long after the last chunk: the reply is over.
        uint32_t hide_after_idle_ms = 1500;
        // Text that never got any audio (e.g. an interrupted turn) is dropped.
        uint32_t no_audio_timeout_ms = 6000;
        // The child's own words disappear this long after the last update.
        uint32_t user_hide_after_ms = 2500;
        uint32_t calibrate_min_voice_ms = 2000;
        float pace_scale_min = 0.6f;
        float pace_scale_max = 1.6f;
    };

    explicit ChatSubtitle(MeasureFn measure);
    ChatSubtitle(MeasureFn measure, const Config& config);

    // A fragment of Bubu's speech transcript, as sent by the gateway.
    void AppendAssistant(const std::string& fragment, uint64_t voice_played_ms, uint64_t now_ms);
    // A fragment of the child's speech transcript. Shown immediately when Bubu
    // is not talking, tail-first so the newest words stay visible. While Bubu
    // is talking it is held back: it is usually just the late tail of the
    // question, and it only ends Bubu's turn if the voice then stops.
    void AppendUser(const std::string& fragment, uint64_t now_ms);
    void Clear();

    // Returns the text that should be on screen now; empty means hidden.
    const std::string& Tick(uint64_t voice_played_ms, uint64_t now_ms);

private:
    struct Chunk {
        std::string text;
        uint32_t start_ms;  // offset into the turn's played voice
        bool closed;        // sentence ended; never extend it
    };

    MeasureFn measure_;
    Config config_;

    // Assistant turn.
    bool turn_active_ = false;
    uint64_t turn_origin_voice_ms_ = 0;
    uint64_t turn_started_now_ms_ = 0;
    uint32_t turn_cursor_ms_ = 0;
    std::vector<Chunk> chunks_;
    int shown_chunk_ = -1;
    uint64_t shown_chunk_now_ms_ = 0;
    uint64_t last_catchup_now_ms_ = 0;
    std::string partial_;
    uint64_t last_append_now_ms_ = 0;

    // Voice activity, derived from the played-ms counter moving.
    uint64_t last_voice_ms_ = 0;
    uint64_t last_voice_advance_now_ms_ = 0;

    // Child's utterance.
    std::string user_text_;
    uint64_t user_updated_now_ms_ = 0;
    bool user_dirty_ = false;
    bool user_during_turn_ = false;  // child's words arrived while Bubu talked

    float pace_scale_ = 1.0f;  // learned from finished replies, kept across turns

    std::string output_;

    void StartTurn(uint64_t voice_played_ms, uint64_t now_ms);
    void EndTurn();
    void ConsumeCompleteWords();
    void AddWord(const std::string& word);
    void Calibrate(uint64_t turn_voice_ms);
    std::string UserTail() const;
};
