#pragma once
#include <stdint.h>
#include "Sfx.h"

// Plays the synthesized sound effects through the CoreS3 speaker, in the
// background while the microphones keep listening (see audio_io.h).

void soundInit();                    // pre-renders every effect and starts the player task
void soundSetVolume(int percent);    // 0-100
void soundPlay(Sfx s);               // start a sound, interrupting any playing one

// True while a sound plays and for `tailMs` after, so the mic-based reactions
// (clap, "someone is talking") can ignore the buddy's own noises.
bool soundHeardRecently(uint32_t tailMs = 300);

// Spoken replies (16 kHz mono), streamed: begin, write pieces as they
// arrive (playback starts right away), end. Speech isn't interrupted by sound
// effects; effects requested meanwhile are dropped. soundSpeechWrite blocks
// while the buffer (about 8 s) is full.
void soundSpeechBegin();
void soundSpeechWrite(const int16_t* pcm, size_t samples);
void soundSpeechEnd();
size_t soundSpeechQueued();  // samples written but not yet played
void soundStopSpeech();
bool soundSpeaking();        // from begin until everything written has played
float soundSpeechLevel();    // 0..1 loudness of what's playing now, for the mouth
float soundEffectLevel();    // 0..1 loudness of the sound effect playing now (0 if none), for the mouth
