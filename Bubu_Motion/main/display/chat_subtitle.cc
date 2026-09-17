#include "chat_subtitle.h"

#include <utility>

namespace {

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Only ASCII punctuation is checked, so a multi-byte UTF-8 tail can never be
// misread as one of these.
char LastChar(const std::string& word) {
    return word.empty() ? '\0' : word.back();
}

bool EndsSentence(const std::string& word) {
    char c = LastChar(word);
    return c == '.' || c == '!' || c == '?';
}

bool EndsClause(const std::string& word) {
    char c = LastChar(word);
    return c == ',' || c == ';' || c == ':';
}

}  // namespace

ChatSubtitle::ChatSubtitle(MeasureFn measure) : ChatSubtitle(std::move(measure), Config()) {}

ChatSubtitle::ChatSubtitle(MeasureFn measure, const Config& config)
    : measure_(std::move(measure)), config_(config) {}

void ChatSubtitle::StartTurn(uint64_t voice_played_ms, uint64_t now_ms) {
    turn_active_ = true;
    turn_origin_voice_ms_ = voice_played_ms;
    turn_started_now_ms_ = now_ms;
    turn_cursor_ms_ = 0;
    chunks_.clear();
    shown_chunk_ = -1;
    shown_chunk_now_ms_ = now_ms;
    last_catchup_now_ms_ = 0;
    partial_.clear();
}

void ChatSubtitle::EndTurn() {
    turn_active_ = false;
    chunks_.clear();
    shown_chunk_ = -1;
    partial_.clear();
    // Whatever the child said while Bubu was talking is stale by now.
    if (user_during_turn_) {
        user_text_.clear();
        user_dirty_ = true;
        user_during_turn_ = false;
    }
}

void ChatSubtitle::Clear() {
    EndTurn();
    user_text_.clear();
    user_dirty_ = true;
    output_.clear();
}

void ChatSubtitle::AppendAssistant(const std::string& fragment, uint64_t voice_played_ms,
                                   uint64_t now_ms) {
    if (!turn_active_) {
        // Bubu answering means the child's utterance is over; take its words
        // down now rather than leaving them up until the voice starts.
        user_text_.clear();
        user_dirty_ = true;
        output_.clear();
        StartTurn(voice_played_ms, now_ms);
    }
    partial_ += fragment;
    last_append_now_ms_ = now_ms;
    ConsumeCompleteWords();
}

void ChatSubtitle::AppendUser(const std::string& fragment, uint64_t now_ms) {
    // Gemini streams the input transcription independently of the reply, so
    // the tail of the child's sentence routinely arrives after Bubu has started
    // answering. Ending Bubu's turn here threw away the rest of the reply --
    // text that had already arrived, since it leads the voice. So only note it;
    // Tick() decides whether it was a real interruption (the voice stops).
    if (turn_active_) {
        user_during_turn_ = true;
    }
    user_text_ += fragment;
    // Only the tail is ever shown; keep the buffer from growing without bound.
    const size_t kMaxUserBytes = 512;
    if (user_text_.size() > kMaxUserBytes) {
        size_t cut = user_text_.find(' ', user_text_.size() - kMaxUserBytes);
        user_text_ = cut == std::string::npos ? std::string() : user_text_.substr(cut + 1);
    }
    user_updated_now_ms_ = now_ms;
    user_dirty_ = true;
}

void ChatSubtitle::ConsumeCompleteWords() {
    size_t pos = 0;
    while (true) {
        while (pos < partial_.size() && IsSpace(partial_[pos])) {
            pos++;
        }
        size_t end = pos;
        while (end < partial_.size() && !IsSpace(partial_[end])) {
            end++;
        }
        // A word is complete only once whitespace follows it; a fragment can
        // stop in the middle of a word and the next one carries the rest.
        if (end == partial_.size()) {
            break;
        }
        AddWord(partial_.substr(pos, end - pos));
        pos = end;
    }
    partial_.erase(0, pos);
}

void ChatSubtitle::AddWord(const std::string& word) {
    if (word.empty()) {
        return;
    }

    bool appended = false;
    if (!chunks_.empty() && !chunks_.back().closed) {
        std::string candidate = chunks_.back().text + " " + word;
        if (measure_(candidate) <= config_.max_width_px) {
            chunks_.back().text = std::move(candidate);
            appended = true;
        }
    }
    if (!appended) {
        // A single word wider than the budget still gets its own chunk; the
        // label wraps it rather than dropping it.
        chunks_.push_back({word, turn_cursor_ms_, false});
    }

    uint32_t duration = config_.ms_per_word;
    if (EndsSentence(word)) {
        duration += config_.sentence_pause_ms;
        // Never let a chunk straddle two sentences.
        chunks_.back().closed = true;
    } else if (EndsClause(word)) {
        duration += config_.comma_pause_ms;
    }
    turn_cursor_ms_ += static_cast<uint32_t>(static_cast<float>(duration) * pace_scale_);
}

void ChatSubtitle::Calibrate(uint64_t turn_voice_ms) {
    // A reply that ran its course tells us how fast this voice really speaks:
    // compare the audio it took with what the estimate predicted, and move the
    // pace halfway there for the next reply. Short replies are too noisy.
    if (turn_voice_ms < config_.calibrate_min_voice_ms || turn_cursor_ms_ == 0) {
        return;
    }
    float ratio = static_cast<float>(turn_voice_ms) / static_cast<float>(turn_cursor_ms_);
    float scale = pace_scale_ * (0.5f + 0.5f * ratio);
    if (scale < config_.pace_scale_min) {
        scale = config_.pace_scale_min;
    } else if (scale > config_.pace_scale_max) {
        scale = config_.pace_scale_max;
    }
    pace_scale_ = scale;
}

std::string ChatSubtitle::UserTail() const {
    // Newest words that fit, walking back from the end.
    std::string tail;
    size_t end = user_text_.size();
    while (end > 0) {
        while (end > 0 && IsSpace(user_text_[end - 1])) {
            end--;
        }
        size_t start = end;
        while (start > 0 && !IsSpace(user_text_[start - 1])) {
            start--;
        }
        if (start == end) {
            break;
        }
        std::string word = user_text_.substr(start, end - start);
        std::string candidate = tail.empty() ? word : word + " " + tail;
        if (!tail.empty() && measure_(candidate) > config_.max_width_px) {
            break;
        }
        tail = std::move(candidate);
        end = start;
    }
    return tail;
}

const std::string& ChatSubtitle::Tick(uint64_t voice_played_ms, uint64_t now_ms) {
    if (voice_played_ms != last_voice_ms_) {
        last_voice_ms_ = voice_played_ms;
        last_voice_advance_now_ms_ = now_ms;
    }
    const uint64_t voice_idle_ms = now_ms - last_voice_advance_now_ms_;

    if (!turn_active_) {
        if (!user_text_.empty() && now_ms - user_updated_now_ms_ >= config_.user_hide_after_ms) {
            user_text_.clear();
            user_dirty_ = true;
        }
        // Measuring is the expensive part; only redo it when the text changed.
        if (user_dirty_) {
            output_ = UserTail();
            user_dirty_ = false;
        }
        return output_;
    }

    if (!partial_.empty() && now_ms - last_append_now_ms_ >= config_.partial_word_hold_ms) {
        std::string word;
        word.swap(partial_);
        size_t start = 0;
        while (start < word.size() && IsSpace(word[start])) {
            start++;
        }
        AddWord(word.substr(start));
    }

    const uint64_t played_ms = voice_played_ms - turn_origin_voice_ms_;
    const int last_chunk = static_cast<int>(chunks_.size()) - 1;

    if (played_ms == 0) {
        // No audio yet for this turn. Text normally arrives first, so wait --
        // but not forever: an interrupted turn may never get its audio.
        if (now_ms - turn_started_now_ms_ >= config_.no_audio_timeout_ms) {
            EndTurn();
            output_.clear();
        }
        return output_;
    }

    // Paced by the speaker: the last chunk whose first word has been reached.
    int target = shown_chunk_;
    while (target < last_chunk && chunks_[target + 1].start_ms <= played_ms) {
        target++;
    }

    // The child spoke and then the voice went quiet with chunks unshown: that
    // was an interruption, and the rest is text Bubu never said. Drop it.
    if (user_during_turn_ && target < last_chunk &&
        voice_idle_ms >= config_.catchup_after_idle_ms) {
        EndTurn();
        output_.clear();
        return output_;
    }

    // The voice has gone quiet with chunks still unshown: the per-word estimate
    // was too slow for this reply. Drain what is left rather than freezing.
    if (target < last_chunk && voice_idle_ms >= config_.catchup_after_idle_ms &&
        now_ms - last_catchup_now_ms_ >= config_.catchup_chunk_ms) {
        target = (target < shown_chunk_ + 1) ? shown_chunk_ + 1 : target;
        last_catchup_now_ms_ = now_ms;
    }

    if (target >= 0) {
        if (target != shown_chunk_) {
            shown_chunk_ = target;
            shown_chunk_now_ms_ = now_ms;
        }
        output_ = chunks_[shown_chunk_].text;
    }

    // Counted from whichever is later, the voice stopping or the last chunk
    // appearing -- a chunk drained by catch-up must still stay up long enough
    // to be read.
    const uint64_t quiet_since_ms = last_voice_advance_now_ms_ > shown_chunk_now_ms_
                                        ? last_voice_advance_now_ms_
                                        : shown_chunk_now_ms_;
    if (shown_chunk_ == last_chunk && partial_.empty() &&
        now_ms - quiet_since_ms >= config_.hide_after_idle_ms) {
        Calibrate(last_voice_ms_ - turn_origin_voice_ms_);
        EndTurn();
        output_.clear();
    }
    return output_;
}
