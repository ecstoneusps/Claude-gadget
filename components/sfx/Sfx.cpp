#include "Sfx.h"
#include <math.h>

namespace {

// Sine/Triangle/Buzz: the robot's clean tones. For the puppy: Bark (a bright,
// raspy tone, softened by a low-pass filter), Breath (filtered noise: panting,
// sniffs) and Growl (a low buzz with noise in it).
enum Wave : uint8_t { Sine, Triangle, Buzz, Bark, Breath, Growl };

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

// --- Puppy -------------------------------------------------------------------
// Barks and yips: a quick downward pitch drop on a raspy tone. Whines and
// whimpers: high, wavering sine tones. Panting: short breathy noise bursts.
const Seg kPupChirp[] = {  // soft alert "wuf!"
    {620, 470, 95, 0, Bark, 0.75f, 0, 0},
};
const Seg kPupBoop[] = {  // tiny "yip"
    {980, 760, 60, 0, Bark, 0.7f, 0, 0},
};
const Seg kPupHappy[] = {  // "arf arf!"
    {700, 520, 85, 75, Bark, 0.8f, 0, 0},
    {760, 560, 100, 0, Bark, 0.8f, 0, 0},
};
const Seg kPupGiggle[] = {  // happy panting "hah-hah-hah-hah"
    {1, 1, 70, 50, Breath, 0.8f, 0, 0},
    {1, 1, 70, 50, Breath, 0.75f, 0, 0},
    {1, 1, 70, 50, Breath, 0.7f, 0, 0},
    {1, 1, 70, 50, Breath, 0.65f, 0, 0},
    {1, 1, 80, 0, Breath, 0.55f, 0, 0},
};
const Seg kPupLove[] = {  // a happy little "mmmm-whine", rising then settling
    {650, 1050, 260, 0, Sine, 0.6f, 7, 0.025f},
    {1050, 850, 360, 0, Sine, 0.55f, 7, 0.035f},
};
const Seg kPupPurr[] = {  // contented low "mmrrr"
    {210, 185, 600, 0, Growl, 0.42f, 9, 0.06f},
};
const Seg kPupSad[] = {  // whimper
    {1250, 980, 330, 60, Sine, 0.55f, 9, 0.045f},
    {1150, 820, 450, 0, Sine, 0.5f, 8, 0.05f},
};
const Seg kPupAngry[] = {  // little growl "grrrr"
    {130, 115, 260, 30, Growl, 0.6f, 23, 0.14f},
    {125, 100, 380, 0, Growl, 0.6f, 23, 0.16f},
};
const Seg kPupSurprised[] = {  // sharp "yip!"
    {1300, 900, 75, 0, Bark, 0.8f, 0, 0},
};
const Seg kPupConfused[] = {  // head-tilt "rrr?" rising like a question
    {330, 360, 140, 0, Growl, 0.35f, 15, 0.05f},
    {700, 1250, 260, 0, Sine, 0.55f, 6, 0.02f},
};
const Seg kPupYawn[] = {  // squeaky dog yawn
    {900, 1450, 220, 0, Sine, 0.55f, 0, 0},
    {1450, 520, 700, 0, Sine, 0.5f, 6, 0.03f},
};
const Seg kPupWake[] = {  // startled "wuf!"
    {580, 430, 110, 0, Bark, 0.8f, 0, 0},
};

#define DEF(a) {a, (int)(sizeof(a) / sizeof(a[0]))}
const Def kDefs[(int)Sfx::Count] = {
    {nullptr, 0}, DEF(kChirp), DEF(kBoop), DEF(kHappy), DEF(kGiggle), DEF(kLove), DEF(kPurr),
    DEF(kSad), DEF(kAngry), DEF(kSurprised), DEF(kConfused), DEF(kYawn), DEF(kWake),
};
const Def kPupDefs[(int)Sfx::Count] = {
    {nullptr, 0}, DEF(kPupChirp), DEF(kPupBoop), DEF(kPupHappy), DEF(kPupGiggle), DEF(kPupLove), DEF(kPupPurr),
    DEF(kPupSad), DEF(kPupAngry), DEF(kPupSurprised), DEF(kPupConfused), DEF(kPupYawn), DEF(kPupWake),
};
#undef DEF

SfxStyle g_style = SfxStyle::Puppy;

constexpr float kTwoPi = 6.28318531f;
constexpr float kAttackMs = 5.0f;

const Def& def(Sfx s) {
    int i = (int)s;
    const Def* defs = g_style == SfxStyle::Puppy ? kPupDefs : kDefs;
    return defs[(i > 0 && i < (int)Sfx::Count) ? i : 0];
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

// Repeatable white noise (-1..1): the same sound every time it's rendered.
struct Noise {
    uint32_t state = 0x9E3779B9u;
    float next() {
        state = state * 1664525u + 1013904223u;
        return (float)(int32_t)state / 2147483648.0f;
    }
};

float wave(Wave w, float phase, Noise& noise) {  // phase in 0..1
    switch (w) {
        case Triangle: return 4.0f * fabsf(phase - 0.5f) - 1.0f;
        case Buzz: {
            // Softened square: rich and grumbly without harsh aliasing.
            float sq = phase < 0.5f ? 1.0f : -1.0f;
            return 0.55f * sq + 0.45f * sinf(phase * kTwoPi);
        }
        case Bark: {
            // Sawtooth (lots of harmonics, like a voice) with a little breath in it.
            const float saw = 2.0f * phase - 1.0f;
            return 0.8f * saw + 0.2f * sinf(phase * kTwoPi) + 0.25f * noise.next();
        }
        case Breath: return noise.next();
        case Growl: {
            const float sq = phase < 0.5f ? 1.0f : -1.0f;
            return 0.6f * sq + 0.25f * sinf(phase * kTwoPi) + 0.35f * noise.next();
        }
        default: return sinf(phase * kTwoPi);
    }
}

// Low-pass amount per wave (one-pole filter coefficient; 1 = no filtering).
// Barks are softened toward a "woof"; breath becomes a "hah" rather than a hiss.
float filterCoeff(Wave w) {
    switch (w) {
        case Bark: return 0.30f;     // ~1.2 kHz
        case Breath: return 0.22f;   // ~0.8 kHz
        case Growl: return 0.18f;    // ~0.6 kHz
        default: return 1.0f;
    }
}

// Breath bursts swell and fade like a pant instead of switching on and off.
float breathShape(Wave w, float u) {
    if (w != Breath) return 1.0f;
    return sinf(u * 3.14159265f);
}

}  // namespace

void sfxSetStyle(SfxStyle style) { g_style = style; }
SfxStyle sfxStyle() { return g_style; }

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
    float phase = 0, vibPhase = 0, lp = 0;
    Noise noise;
    const float dt = 1.0f / kSfxSampleRate;
    for (int i = 0; i < d.n; ++i) {
        const Seg& g = d.segs[i];
        const size_t n = segSamples(g.ms);
        const float ratio = g.f1 / g.f0;
        const float a = filterCoeff(g.wave);
        // Filtering takes some loudness away; make it up so barks aren't quieter.
        const float makeup = a >= 1.0f ? 1.0f : g.wave == Breath ? 2.6f : 1.6f;
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
            float raw = wave(g.wave, phase, noise);
            lp += a * (raw - lp);
            float v = lp * makeup * breathShape(g.wave, u) * g.vol * envelope(d, i, tMs);
            v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
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
        if (ms < t + g.ms)
            return g.vol * envelope(d, i, (float)(ms - t)) * breathShape(g.wave, (float)(ms - t) / g.ms);
        t += g.ms;
        if (ms < t + g.gapMs) return 0;
        t += g.gapMs;
    }
    return 0;
}
