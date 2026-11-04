#include "Sfx.h"
#include <math.h>

namespace {

enum Wave : uint8_t { Sine, Triangle, Buzz };

// One sweep from f0 to f1 Hz. Sounds are short lists of these.
struct Seg {
    float f0, f1;     // start/end frequency (exponential glide)
    uint16_t ms;      // duration
    uint16_t gapMs;   // silence after
    Wave wave;
    float vol;        // 0..1
    float vibHz;      // vibrato rate (0 = none)
    float vibDepth;   // vibrato depth as a fraction of frequency
};

struct Def {
    const Seg* segs;
    int n;
};

// --- Sound designs --------------------------------------------------------
const Seg kChirp[] = {
    {900, 1500, 70, 15, Sine, 0.7f, 0, 0},
    {1500, 2200, 90, 0, Sine, 0.7f, 0, 0},
};
const Seg kBoop[] = {
    {950, 520, 90, 0, Triangle, 0.7f, 0, 0},
};
const Seg kHappy[] = {  // C6 E6 G6 then a wiggly C7
    {1047, 1047, 70, 15, Triangle, 0.6f, 0, 0},
    {1319, 1319, 70, 15, Triangle, 0.6f, 0, 0},
    {1568, 1568, 70, 15, Triangle, 0.6f, 0, 0},
    {1568, 2093, 160, 0, Triangle, 0.6f, 9, 0.03f},
};
const Seg kGiggle[] = {
    {1350, 1550, 55, 40, Sine, 0.65f, 18, 0.04f},
    {1300, 1500, 55, 40, Sine, 0.65f, 18, 0.04f},
    {1250, 1450, 55, 40, Sine, 0.6f, 18, 0.04f},
    {1200, 1400, 55, 40, Sine, 0.55f, 18, 0.04f},
    {1150, 1350, 70, 0, Sine, 0.5f, 18, 0.04f},
};
const Seg kLove[] = {  // "awww"
    {700, 1100, 250, 0, Sine, 0.65f, 6, 0.02f},
    {1100, 880, 380, 0, Sine, 0.65f, 6, 0.03f},
};
const Seg kPurr[] = {
    {190, 175, 650, 0, Triangle, 0.55f, 26, 0.12f},
};
const Seg kSad[] = {
    {880, 520, 480, 0, Sine, 0.6f, 5, 0.025f},
    {520, 430, 320, 0, Sine, 0.5f, 5, 0.03f},
};
const Seg kAngry[] = {  // grumbly buzz
    {150, 120, 170, 40, Buzz, 0.45f, 30, 0.08f},
    {140, 100, 260, 0, Buzz, 0.45f, 30, 0.1f},
};
const Seg kSurprised[] = {  // "whoop!"
    {480, 1900, 170, 0, Sine, 0.7f, 0, 0},
    {1900, 1750, 110, 0, Sine, 0.6f, 0, 0},
};
const Seg kConfused[] = {  // "hmm?" with a questioning rise
    {700, 1000, 150, 0, Sine, 0.6f, 0, 0},
    {1000, 780, 130, 0, Sine, 0.6f, 0, 0},
    {780, 1250, 210, 0, Sine, 0.6f, 0, 0},
};
const Seg kYawn[] = {
    {650, 1000, 300, 0, Triangle, 0.55f, 0, 0},
    {1000, 420, 750, 0, Triangle, 0.55f, 4, 0.02f},
};
const Seg kWake[] = {  // "boing"
    {300, 1250, 120, 0, Sine, 0.7f, 0, 0},
    {1250, 900, 220, 0, Sine, 0.6f, 13, 0.06f},
};

#define DEF(a) {a, (int)(sizeof(a) / sizeof(a[0]))}
const Def kDefs[(int)Sfx::Count] = {
    {nullptr, 0}, DEF(kChirp), DEF(kBoop), DEF(kHappy), DEF(kGiggle), DEF(kLove), DEF(kPurr),
    DEF(kSad), DEF(kAngry), DEF(kSurprised), DEF(kConfused), DEF(kYawn), DEF(kWake),
};
#undef DEF

constexpr float kTwoPi = 6.28318531f;
constexpr float kAttackMs = 5.0f;

const Def& def(Sfx s) {
    int i = (int)s;
    return kDefs[(i > 0 && i < (int)Sfx::Count) ? i : 0];
}

size_t segSamples(uint32_t ms) { return (size_t)ms * kSfxSampleRate / 1000; }

// Click-free envelope. Segments that flow straight into the next one (no gap)
// don't fade, so multi-part sounds glide instead of stuttering.
bool fadesIn(const Def& d, int i) { return i == 0 || d.segs[i - 1].gapMs > 0; }
bool fadesOut(const Def& d, int i) { return i == d.n - 1 || d.segs[i].gapMs > 0; }

float envelope(const Def& d, int i, float tMs) {
    const float durMs = d.segs[i].ms;
    float a = fadesIn(d, i) ? fminf(1.0f, tMs / kAttackMs) : 1.0f;
    float r = 1.0f;
    if (fadesOut(d, i)) r = fminf(1.0f, (durMs - tMs) / fminf(30.0f, durMs * 0.35f));
    return fmaxf(0.0f, fminf(a, r));
}

float wave(Wave w, float phase) {  // phase in 0..1
    switch (w) {
        case Triangle: return 4.0f * fabsf(phase - 0.5f) - 1.0f;
        case Buzz: {
            // Softened square: rich and grumbly without harsh aliasing.
            float sq = phase < 0.5f ? 1.0f : -1.0f;
            return 0.55f * sq + 0.45f * sinf(phase * kTwoPi);
        }
        default: return sinf(phase * kTwoPi);
    }
}

}  // namespace

const char* sfxName(Sfx s) {
    switch (s) {
        case Sfx::Chirp:     return "chirp";
        case Sfx::Boop:      return "boop";
        case Sfx::Happy:     return "happy";
        case Sfx::Giggle:    return "giggle";
        case Sfx::Love:      return "love";
        case Sfx::Purr:      return "purr";
        case Sfx::Sad:       return "sad";
        case Sfx::Angry:     return "angry";
        case Sfx::Surprised: return "surprised";
        case Sfx::Confused:  return "confused";
        case Sfx::Yawn:      return "yawn";
        case Sfx::Wake:      return "wake";
        default:             return "none";
    }
}

uint32_t sfxDurationMs(Sfx s) {
    const Def& d = def(s);
    uint32_t ms = 0;
    for (int i = 0; i < d.n; ++i) ms += d.segs[i].ms + d.segs[i].gapMs;
    return ms;
}

size_t sfxSamples(Sfx s) {
    const Def& d = def(s);
    size_t n = 0;
    for (int i = 0; i < d.n; ++i) n += segSamples(d.segs[i].ms) + segSamples(d.segs[i].gapMs);
    return n;
}

size_t sfxRender(Sfx s, int16_t* out) {
    const Def& d = def(s);
    size_t pos = 0;
    float phase = 0, vibPhase = 0;
    const float dt = 1.0f / kSfxSampleRate;
    for (int i = 0; i < d.n; ++i) {
        const Seg& g = d.segs[i];
        const size_t n = segSamples(g.ms);
        const float ratio = g.f1 / g.f0;
        for (size_t k = 0; k < n; ++k) {
            float u = (float)k / (float)n;
            float f = g.f0 * powf(ratio, u);
            if (g.vibHz > 0) {
                vibPhase += g.vibHz * dt;
                f *= 1.0f + g.vibDepth * sinf(vibPhase * kTwoPi);
            }
            phase += f * dt;
            phase -= floorf(phase);
            float tMs = k * 1000.0f / kSfxSampleRate;
            float v = wave(g.wave, phase) * g.vol * envelope(d, i, tMs);
            out[pos++] = (int16_t)(v * 0.85f * 32767.0f);
        }
        const size_t gap = segSamples(g.gapMs);
        for (size_t k = 0; k < gap; ++k) out[pos++] = 0;
    }
    return pos;
}

float sfxLevelAt(Sfx s, uint32_t ms) {
    const Def& d = def(s);
    uint32_t t = 0;
    for (int i = 0; i < d.n; ++i) {
        const Seg& g = d.segs[i];
        if (ms < t + g.ms) return g.vol * envelope(d, i, (float)(ms - t));
        t += g.ms;
        if (ms < t + g.gapMs) return 0;
        t += g.gapMs;
    }
    return 0;
}
