#pragma once
#include <stddef.h>
#include <stdint.h>

// Minimal WAV (RIFF, 16-bit PCM) reader for the spoken replies. Portable,
// unit-tested on a PC.
struct WavInfo {
    int sampleRate = 0;
    int channels = 0;
    size_t dataOffset = 0;  // byte offset of the first sample
    size_t frames = 0;      // samples per channel
};

// Parses the header. Handles streamed files whose size fields are 0 or
// 0xFFFFFFFF (the samples then run to the end of the buffer).
bool parseWav(const uint8_t* data, size_t len, WavInfo& out);

// Number of mono samples `info` becomes at `rate`.
size_t wavResampledLength(const WavInfo& info, int rate);

// Converts to mono 16-bit at `rate` (averaging channels, linear
// interpolation). `out` needs wavResampledLength() samples. Returns the
// number written.
size_t wavToMono(const uint8_t* data, const WavInfo& info, int rate, int16_t* out, size_t outCap);

// Makes a spoken reply as loud and clear as the CoreS3's small speaker allows:
// cuts the bass it can't reproduce (below ~250 Hz, which only wastes
// loudness), raises the speech to a steady level (up to `maxGainDb`), and
// soft-limits peaks so nothing clips. In place. Returns the gain applied, in dB.
float prepareSpeech(int16_t* pcm, size_t n, int sampleRate, float maxGainDb = 18.0f);
