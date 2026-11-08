#pragma once
#include <stddef.h>
#include <stdint.h>

// Procedurally synthesized sound effects, in two voices: a "robot pet" (chirps,
// boops, purrs) and a puppy (yips, barks, panting, whimpers, growls). Portable
// C++: the device renders them into RAM at boot, and the desktop tool writes
// them as WAV files.

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

constexpr uint32_t kSfxSampleRate = 16000;  // matches the full-duplex I2S rate

// Which voice the functions below use (default: Puppy). Set it before
// rendering; it applies to sfxSamples, sfxRender, sfxDurationMs and sfxLevelAt.
enum class SfxStyle : uint8_t { Robot, Puppy };
void sfxSetStyle(SfxStyle style);
SfxStyle sfxStyle();

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
