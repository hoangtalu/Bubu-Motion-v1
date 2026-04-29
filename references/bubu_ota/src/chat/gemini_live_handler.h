#pragma once

#include <Arduino.h>

namespace GeminiLiveHandler {
    struct TurnResult {
        String responseText;
        String functionName;
        String functionParam;
        bool   receivedAudio = false;
        bool   receivedTranscript = false;
    };

    // Start SSL WebSocket connection (non-blocking — monitor with isConnected())
    void beginConnect();

    // True once setupComplete received from Gemini
    bool isConnected();

    // True once the server has finished generating this turn.
    bool isGenerationComplete();

    // True if the server or socket reported an error for the current session.
    bool hasError();

    // Human-readable error for the current session, if any.
    String getLastError();

    // Close the WebSocket (next beginConnect() = fresh session with no history)
    void disconnect();

    // Drive WebSocket I/O — call from ChatSystem::update() every frame
    void loop();

    // Begin a new audio input turn on an already-connected session.
    void beginAudioTurn();

    // Send one chunk of 16 kHz PCM audio for the current turn.
    bool sendAudioChunk(const uint8_t* pcm, size_t bytes);

    // Mark the current audio turn as complete.
    bool endAudioTurn();

    // True once the current turn has fully settled.
    bool isTurnSettled();

    // Final success/failure for the current turn after it has settled.
    bool didTurnSucceed();

    // Send one audio turn: activityStart → PCM chunks → activityEnd, then block
    // until turnComplete (or 30s timeout). Audio response arrives async via
    // the event handler → ChatAudio::sendToPlayback().
    // Returns true on success.
    bool sendAudioTurn(const uint8_t* pcm, size_t bytes);

    // Result accumulated for the most recent turn.
    TurnResult getTurnResult();
}
