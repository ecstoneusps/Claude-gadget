#include "Wav.h"

#include <string.h>

namespace {
uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
}  // namespace

bool parseWav(const uint8_t* d, size_t len, WavInfo& out) {
    if (len < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return false;
    size_t pos = 12;
    bool haveFmt = false;
    int bits = 0;
    while (pos + 8 <= len) {
        const uint8_t* id = d + pos;
        const uint32_t size = u32(d + pos + 4);
        pos += 8;
        if (memcmp(id, "fmt ", 4) == 0) {
            if (size < 16 || pos + 16 > len) return false;
            const uint16_t format = u16(d + pos);
            out.channels = u16(d + pos + 2);
            out.sampleRate = (int)u32(d + pos + 4);
            bits = u16(d + pos + 14);
            // 1 = PCM; 0xFFFE = WAVE_FORMAT_EXTENSIBLE (still PCM for 16-bit)
            if ((format != 1 && format != 0xFFFE) || bits != 16 || out.channels < 1 || out.channels > 8 ||
                out.sampleRate < 4000 || out.sampleRate > 96000)
                return false;
            haveFmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            if (!haveFmt) return false;
            out.dataOffset = pos;
            size_t bytes = len - pos;
            if (size != 0 && size != 0xFFFFFFFFu && size < bytes) bytes = size;
            out.frames = bytes / (2 * out.channels);
            return true;
        }
        if (size > len - pos) return false;
        pos += size + (size & 1);  // chunks are word-aligned
    }
    return false;
}

size_t wavResampledLength(const WavInfo& w, int rate) {
    if (w.sampleRate <= 0) return 0;
    return (size_t)((uint64_t)w.frames * rate / w.sampleRate);
}

size_t wavToMono(const uint8_t* d, const WavInfo& w, int rate, int16_t* out, size_t outCap) {
    const uint8_t* base = d + w.dataOffset;
    auto mono = [&](size_t frame) -> int32_t {
        const uint8_t* p = base + frame * 2 * w.channels;
        int32_t sum = 0;
        for (int c = 0; c < w.channels; ++c) sum += (int16_t)u16(p + 2 * c);
        return sum / w.channels;
    };
    size_t n = wavResampledLength(w, rate);
    if (n > outCap) n = outCap;
    if (w.frames == 0) return 0;
    for (size_t i = 0; i < n; ++i) {
        // Position in the source, in 1/65536 frames.
        const uint64_t pos = (uint64_t)i * w.sampleRate * 65536 / rate;
        const size_t f = (size_t)(pos >> 16);
        const int32_t frac = (int32_t)(pos & 0xFFFF);
        const int32_t a = mono(f < w.frames ? f : w.frames - 1);
        const int32_t b = mono(f + 1 < w.frames ? f + 1 : w.frames - 1);
        out[i] = (int16_t)(a + (((b - a) * frac) >> 16));
    }
    return n;
}
