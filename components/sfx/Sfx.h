#pragma once
#include <stddef.h>
#include <stdint.h>

// Procedurally synthesized "robot pet" sound effects. Portable C++: the device
// renders them into RAM at boot, and the desktop tool writes them as WAV files.

enum class Sfx : uint8_t {
    None,
    Chirp,      // "bweep!" -- heard the wake word
    Boop,       // tap
    Happy,
    Giggle,
    Love,
    Purr,       // being petted
    Sad,
    Angry,
    Surprised,
    Confused,
    Yawn,       // falling asleep
    Wake,       // startled awake
    Count
};

constexpr uint32_t kSfxSampleRate = 24000;

const char* sfxName(Sfx s);

// Length of a sound in samples / milliseconds.
size_t sfxSamples(Sfx s);
uint32_t sfxDurationMs(Sfx s);

// Renders the sound as 16-bit mono PCM at kSfxSampleRate. `out` must hold
// sfxSamples(s) samples. Returns the number of samples written.
size_t sfxRender(Sfx s, int16_t* out);

// Loudness envelope (0..1) at `ms` into the sound; drives the mouth so the
// face "sings" along. Returns 0 past the end.
float sfxLevelAt(Sfx s, uint32_t ms);
