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
