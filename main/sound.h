#pragma once
#include <stdint.h>
#include "Sfx.h"

// Plays the synthesized sound effects through the CoreS3 speaker. Call from
// the UI task only (it owns the I2C bus used to switch mic <-> speaker).

void soundInit();                 // pre-renders every effect into PSRAM
void soundPlay(Sfx s);            // queue a sound (the newest request wins)
void soundUpdate(uint32_t nowMs); // drive playback; call every loop
