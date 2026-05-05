#include "protocol.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <memory>

#define TAG "Protocol"

namespace {

std::string BuildListenDetectMessage(const std::string& session_id, const std::string& text) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), cJSON_Delete);
    if (!root) {
        return {};
    }

    cJSON_AddStringToObject(root.get(), "session_id", session_id.c_str());
    cJSON_AddStringToObject(root.get(), "type", "listen");
    cJSON_AddStringToObject(root.get(), "state", "detect");
    cJSON_AddStringToObject(root.get(), "text", text.c_str());

    char* json = cJSON_PrintUnformatted(root.get());
    if (json == nullptr) {
        return {};
    }

    std::string message(json);
    cJSON_free(json);
    return message;
}

std::string BuildHiddenTextPromptMessage(const std::string& session_id, const std::string& text) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), cJSON_Delete);
    if (!root) {
        return {};
    }

    cJSON_AddStringToObject(root.get(), "session_id", session_id.c_str());
    cJSON_AddStringToObject(root.get(), "type", "listen");
    cJSON_AddStringToObject(root.get(), "state", "start");
    cJSON_AddStringToObject(root.get(), "mode", "auto");
    cJSON_AddStringToObject(root.get(), "source", "proactive_hidden_prompt");
    cJSON_AddBoolToObject(root.get(), "hidden", true);
    cJSON_AddStringToObject(root.get(), "text", text.c_str());

    char* json = cJSON_PrintUnformatted(root.get());
    if (json == nullptr) {
        return {};
    }

    std::string message(json);
    cJSON_free(json);
    return message;
}

}  // namespace

void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

void Protocol::OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback) {
    on_incoming_audio_ = callback;
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback) {
    on_audio_channel_opened_ = callback;
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback) {
    on_audio_channel_closed_ = callback;
}

void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback) {
    on_network_error_ = callback;
}

void Protocol::OnConnected(std::function<void()> callback) {
    on_connected_ = callback;
}

void Protocol::OnDisconnected(std::function<void()> callback) {
    on_disconnected_ = callback;
}

void Protocol::SetError(const std::string& message) {
    error_occurred_ = true;
    if (on_network_error_ != nullptr) {
        on_network_error_(message);
    }
}

void Protocol::SendAbortSpeaking(AbortReason reason) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string& wake_word) {
    auto message = BuildListenDetectMessage(session_id_, wake_word);
    if (!message.empty()) {
        SendText(message);
    }
}

void Protocol::SendHiddenTextPrompt(const std::string& text) {
    auto message = BuildHiddenTextPromptMessage(session_id_, text);
    if (!message.empty()) {
        const uint32_t now_ms = esp_log_timestamp();
        ESP_LOGI(TAG, "Hidden seed tx now_ms=%u json=%s",
                 static_cast<unsigned>(now_ms), message.c_str());
        SendText(message);
    }
}

void Protocol::SendStartListening(ListeningMode mode) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == kListeningModeRealtime) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == kListeningModeAutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendStopListening() {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    SendText(message);
}

void Protocol::SendMcpMessage(const std::string& payload) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    SendText(message);
}

bool Protocol::IsTimeout() const {
    const int kTimeoutSeconds = 120;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_incoming_time_);
    bool timeout = duration.count() > kTimeoutSeconds;
    if (timeout) {
        ESP_LOGE(TAG, "Channel timeout %ld seconds", (long)duration.count());
    }
    return timeout;
}
