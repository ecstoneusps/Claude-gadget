#pragma once
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "Companion.h"  // VoiceCommand

// Talking with an AI through Home Assistant's voice assistant ("Assist").
//
// After "Hey Buddy", voice.cpp streams what you say here. This sends it over
// Home Assistant's websocket API to its speech-to-text (Whisper). If the words
// are one of the buddy's own action commands ("look left", "wink at me") it
// does that itself; anything else goes to the conversation agent (the AI on
// Ollama). The reply's emotion tag sets the face, and the rest is turned into
// speech (Piper) and played with the mouth moving along.

struct AssistantConfig {
    std::string url;       // http://192.168.1.20:8123
    std::string token;     // long-lived access token (never logged)
    std::string pipeline;  // voice assistant name in Home Assistant ("" = its preferred one)
    bool followUp = true;  // listen again after each answer
    std::vector<std::pair<VoiceCommand, std::string>> extraPhrases;  // say.* from settings.txt
};

// What the UI should show, posted to the queue given to assistantStart().
struct AssistEvent {
    enum Type : uint8_t {
        Thinking,  // you stopped talking; it's working on it
        Command,   // you said a built-in action command: `command`
        Reply,     // the answer arrived: `emotion` ("" if none); speech starts playing
        Done,      // finished speaking
        NotHeard,  // no words recognized (`followUp`: it was a follow-up, so end quietly)
        Error,     // something failed (see the log); `text` says what
    } type;
    bool followUp;
    int command;
    char emotion[16];
    char text[48];
};

// Starts the client in the background: waits for Wi-Fi, connects, logs in
// and looks up the voice assistant. Returns false if the settings are unusable.
bool assistantStart(const AssistantConfig& cfg, QueueHandle_t events);

bool assistantAvailable();   // connected, logged in, voice assistant found, not mid-turn
const char* assistantStatus();

// For voice.cpp. beginTurn returns false if a turn can't start now (then the
// offline command recognizer handles it). Audio is 16 kHz mono.
bool assistantBeginTurn(bool followUp);
void assistantAudio(const int16_t* pcm, int samples);
void assistantEndAudio(bool heardSpeech);
bool assistantSttDone();  // Home Assistant decided you stopped talking
