#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Google Gemini Live API (WebSocket) — to be used by new Live API handler
static const char*    GEMINI_API_HOST = "generativelanguage.googleapis.com";
static const uint16_t GOOGLE_API_PORT = 443;
// Model for Live API (WebSocket). Use gemini-2.5-flash-native-audio for native audio dialog.
// Do NOT use REST generateContent with this model — it requires the Live API WebSocket endpoint.
static const char* GEMINI_LIVE_MODEL = "models/gemini-2.5-flash-native-audio-preview-12-2025";
static const uint16_t GEMINI_LIVE_MAX_OUTPUT_TOKENS = 64;

// Bubu personality system prompt
static const char* BUBU_SYSTEM_PROMPT =
    "You are Bubu, a friendly and cute virtual pet on a small screen. "
    "Respond as natural spoken dialogue, not written prose. "
    "Keep responses SHORT (1 sentence, at most 2 short sentences). "
    "Be warm, playful, and helpful. Respond in Vietnamese unless the owner asks otherwise. "
    "Do not narrate your reasoning, planning, or translation. "
    "Do not use markdown, headings, bullet points, or special characters the screen font may not support.";

struct ChatConfig {
    bool    enabled      = true;
    String  apiKey       = "";   // Google AI Studio key (for Live API)
    uint8_t volume       = 18;   // kept for potential future audio output

    void load() {
        Preferences prefs;
        prefs.begin("chat", true);
        enabled = prefs.getBool("enabled", true);
        apiKey  = prefs.getString("apikey", "");
        volume  = prefs.getUChar("volume", 18);
        prefs.end();
    }

    void save() {
        Preferences prefs;
        prefs.begin("chat", false);
        prefs.putBool("enabled", enabled);
        prefs.putString("apikey", apiKey);
        prefs.putUChar("volume", volume);
        prefs.end();
    }

    bool isConfigured() const {
        return !apiKey.isEmpty() && apiKey.length() > 10;
    }

    // Legacy stubs - kept so menu_system / config_fetcher compile
    String voiceName    = "Puck";
    bool   autoVAD      = true;
    bool   audioEnabled = true;
    String getWsUrl() const { return ""; }
};

extern ChatConfig chatConfig;
