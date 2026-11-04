#pragma once
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "Companion.h"  // VoiceCommand

// Offline speech pipeline (Espressif ESP-SR):
//   CoreS3 mics (both, as stereo) -> audio front-end (source separation, voice activity,
//   "Hey Buddy" wake word) -> MultiNet7 command recognizer.
// Runs in two FreeRTOS tasks on core 0 and posts events to a queue.

struct VoiceEvent {
    enum Type : uint8_t { WakeWord, Command, Timeout } type;
    int commandId;  // VoiceCommand value, for Command events
    float prob;     // recognizer confidence, for Command events
};

// Sets up the microphone. Call once from the startup task after M5.begin(),
// before other tasks touch the I2C bus.
bool voiceMicBegin();

// Loads the models and starts the pipeline (takes a few seconds). Call after
// voiceMicBegin(). Returns false if the speech models are missing or memory ran out.
// `extraPhrases` (from settings.txt) are added to the built-in phrase list.
bool voiceStart(QueueHandle_t events,
                const std::vector<std::pair<VoiceCommand, std::string>>& extraPhrases = {});

// Latest audio level in dBFS and whether someone seems to be talking.
// Safe to call from any task.
float voiceLevelDb();
bool voiceSpeech();

// Used by the sound player: pause feeding the mic, wait for voicePaused(),
// then the mic may be switched off. voiceResume() after the mic is back on.
void voicePause();
void voiceResume();
bool voicePaused();
