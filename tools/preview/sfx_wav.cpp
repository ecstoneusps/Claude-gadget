// Writes every sound effect as a WAV file so you can listen on a PC.
// Usage: sfx_wav <output-dir>
#include <stdio.h>
#include <string>
#include <vector>

#include "Sfx.h"

static void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 1;
    }
    for (int i = 1; i < (int)Sfx::Count; ++i) {
        Sfx s = (Sfx)i;
        std::vector<int16_t> pcm(sfxSamples(s));
        size_t n = sfxRender(s, pcm.data());
        std::string path = std::string(argv[1]) + "/" + sfxName(s) + ".wav";
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return 1;
        uint32_t bytes = (uint32_t)(n * 2);
        fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16); put16(f, 1); put16(f, 1); put32(f, kSfxSampleRate); put32(f, kSfxSampleRate * 2);
        put16(f, 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
        fwrite(pcm.data(), 2, n, f);
        fclose(f);
        printf("%-10s %4u ms\n", sfxName(s), sfxDurationMs(s));
    }
    return 0;
}
