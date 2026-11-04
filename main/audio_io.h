#pragma once
#include <stdint.h>

// Full-duplex audio on the CoreS3: both microphones (ES7210) and the speaker
// amplifier (AW88298) run at the same time on one I2S port, at 16 kHz, so the
// buddy keeps listening while it plays sounds. Based on how xiaozhi-esp32
// (MIT) drives the same chips with Espressif's esp_codec_dev.

constexpr uint32_t kAudioSampleRate = 16000;

bool audioBegin();                                   // call from app_main, after the I2C bus is up

// Blocking reads of interleaved stereo frames: left = mic 1, right = mic 2.
// `frames` stereo frames go into `buf` (2 * frames samples).
bool audioReadStereo(int16_t* buf, int frames);

// Blocking write of mono samples to the speaker.
bool audioWrite(const int16_t* samples, int count);

void audioSetVolume(int percent);                    // 0-100
