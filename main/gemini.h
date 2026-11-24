#pragma once
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "assistant.h"

// The brain when brain = gemini: Google's Gemini Live API, straight from the
// buddy over the internet (no Home Assistant or PC). After "Hey Buddy" what
// you say streams to Gemini as audio; Gemini answers in its own voice, which
// streams back and plays with the mouth moving, the face set from its words.
// "Remember that ..." works through a tool Gemini can call, and the notes go
// in its instructions. assistant.cpp hands its functions over to these when
// this brain is chosen, so the rest of the firmware doesn't change.

bool geminiStart(const AssistantConfig& cfg, QueueHandle_t events);
bool geminiAvailable();
const char* geminiStatus();
bool geminiBeginTurn(bool followUp);
void geminiAudio(const int16_t* pcm, int samples);
void geminiEndAudio(bool heardSpeech);
bool geminiSay(const std::string& text);
bool geminiCancel();
// The photo Gemini asked for with its look tool (nullptr: the camera failed). Takes the JPEG.
void geminiLook(uint8_t* jpeg, size_t len);
bool geminiLookPending();
