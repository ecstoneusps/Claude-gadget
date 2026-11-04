#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Offline speech pipeline (Espressif ESP-SR):
//   CoreS3 mic -> audio front-end (noise suppression, voice activity,
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
bool voiceStart(QueueHandle_t events);

// Latest audio level in dBFS and whether someone seems to be talking.
// Safe to call from any task.
float voiceLevelDb();
bool voiceSpeech();

// Used by the sound player: pause feeding the mic, wait for voicePaused(),
// then the mic may be switched off. voiceResume() after the mic is back on.
void voicePause();
void voiceResume();
bool voicePaused();
