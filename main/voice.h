#pragma once
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "Companion.h"  // VoiceCommand

// Offline speech pipeline (Espressif ESP-SR):
//   CoreS3 mics (both, as stereo, via audio_io) -> audio front-end (source separation, voice activity,
//   "Hey Buddy" wake word) -> MultiNet7 command recognizer.
// Runs in two FreeRTOS tasks on core 0 and posts events to a queue.

struct VoiceEvent {
    // FollowUp: listening again after an AI answer (no wake word needed).
    // FollowUpEnd: nobody said anything, so it stopped listening.
    enum Type : uint8_t { WakeWord, Command, Timeout, FollowUp, FollowUpEnd } type;
    int commandId;  // VoiceCommand value, for Command events
    float prob;     // recognizer confidence, for Command events
};

// Loads the models and starts the pipeline (takes a few seconds). Call after
// audioBegin(). Returns false if the speech models are missing or memory ran out.
// `extraPhrases` (from settings.txt) are added to the built-in phrase list.
// `twoMics` uses both microphones (heavier; see voice.cpp). `detectCore`:
// where the recognizer runs in two-mic mode (the less busy core).
bool voiceStart(QueueHandle_t events,
                const std::vector<std::pair<VoiceCommand, std::string>>& extraPhrases = {},
                bool twoMics = false, int detectCore = 1);

// Latest audio level in dBFS and whether someone seems to be talking.
// Safe to call from any task.
float voiceLevelDb();
const char* voiceFrontEnd();  // "MM" (both mics), "M" (one) or "off"
bool voiceSpeech();

// Conversation mode (see assistant.h): after the wake word, if the assistant is
// available, what you say streams to it instead of the command recognizer.
// When the turn is over the assistant calls voiceResume() (back to waiting for
// "Hey Buddy") or voiceFollowUp() (listen again straight away).
void voiceResume();
void voiceFollowUp();
