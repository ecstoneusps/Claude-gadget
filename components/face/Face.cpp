#include "Face.h"
#include <math.h>
#include <string.h>

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kParamCount = sizeof(FaceParams) / sizeof(float);
static_assert(sizeof(FaceParams) % sizeof(float) == 0, "FaceParams must be all floats");

constexpr float kBlinkMs = 150.0f;   // full close+open time
constexpr float kBreathMs = 3800.0f; // breathing period

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Frame-rate independent exponential approach. tauMs ~ time to cover 63%.
float approach(float cur, float target, float dtMs, float tauMs) {
    float a = 1.0f - expf(-dtMs / tauMs);
    return cur + (target - cur) * a;
}

}  // namespace

const char* expressionName(Expression e) {
    switch (e) {
        case Expression::Neutral:   return "neutral";
        case Expression::Happy:     return "happy";
        case Expression::Sad:       return "sad";
        case Expression::Angry:     return "angry";
        case Expression::Surprised: return "surprised";
        case Expression::Sleepy:    return "sleepy";
        case Expression::Confused:  return "confused";
        case Expression::Listening: return "listening";
        case Expression::Love:      return "love";
        case Expression::Laughing:  return "laughing";
        case Expression::Crying:    return "crying";
        case Expression::Embarrassed: return "embarrassed";
        case Expression::Shocked:   return "shocked";
        case Expression::Thinking:  return "thinking";
        case Expression::Winking:   return "winking";
        case Expression::Cool:      return "cool";
        case Expression::Relaxed:   return "relaxed";
        case Expression::Silly:     return "silly";
        default:                    return "?";
    }
}

namespace {
struct EmotionMap {
    const char* name;
    Expression expr;
};
// xiaozhi-esp32's emotion vocabulary (the "emotion" field of its "llm"
// messages), mapped to our closest expression.
const EmotionMap kEmotionMap[] = {
    {"neutral", Expression::Neutral},       {"happy", Expression::Happy},
    {"laughing", Expression::Laughing},     {"funny", Expression::Laughing},
    {"sad", Expression::Sad},               {"angry", Expression::Angry},
    {"crying", Expression::Crying},         {"loving", Expression::Love},
    {"embarrassed", Expression::Embarrassed}, {"surprised", Expression::Surprised},
    {"shocked", Expression::Shocked},       {"thinking", Expression::Thinking},
    {"winking", Expression::Winking},       {"cool", Expression::Cool},
    {"relaxed", Expression::Relaxed},       {"delicious", Expression::Silly},
    {"kissy", Expression::Love},            {"confident", Expression::Cool},
    {"sleepy", Expression::Sleepy},         {"silly", Expression::Silly},
    {"confused", Expression::Confused},
};
}  // namespace

const char* const kEmotionNames[] = {
    "neutral", "happy", "laughing", "funny", "sad", "angry", "crying",
    "loving", "embarrassed", "surprised", "shocked", "thinking", "winking",
    "cool", "relaxed", "delicious", "kissy", "confident", "sleepy", "silly", "confused",
};
const int kEmotionNameCount = sizeof(kEmotionNames) / sizeof(kEmotionNames[0]);

bool expressionForEmotion(const char* emotion, Expression& out) {
    if (!emotion) return false;
    for (const auto& m : kEmotionMap) {
        if (strcmp(m.name, emotion) == 0) {
            out = m.expr;
            return true;
        }
    }
    // Our own expression names work too ("listening", "love").
    for (int i = 0; i < (int)Expression::Count; ++i) {
        if (strcmp(expressionName((Expression)i), emotion) == 0) {
            out = (Expression)i;
            return true;
        }
    }
    return false;
}

FaceParams paramsFor(Expression e) {
    // Start from neutral and tweak per expression.
    FaceParams p{};
    p.eyeW = 64; p.eyeH = 84;
    p.eyeScaleL = 1; p.eyeScaleR = 1;
    p.openness = 1;
    p.lidAngry = 0; p.lidSad = 0; p.lidBottom = 0;
    p.mouthW = 52; p.mouthCurve = 0.3f; p.mouthOpen = 0;
    p.blush = 0;
    p.eyeR = 40; p.eyeG = 220; p.eyeB = 255;
    p.lookY = 0;
    p.lookX = 0;
    p.opennessL = 1; p.opennessR = 1;
    p.lidTop = 0; p.tears = 0; p.tongue = 0; p.mouthSkew = 0; p.bounce = 0;

    switch (e) {
        case Expression::Neutral:
            break;
        case Expression::Happy:
            p.eyeW = 70; p.eyeH = 80;
            p.lidBottom = 0.6f;
            p.mouthW = 66; p.mouthCurve = 1.0f; p.mouthOpen = 0.45f;
            p.blush = 0.7f;
            p.eyeR = 70; p.eyeG = 235; p.eyeB = 255;
            break;
        case Expression::Sad:
            p.eyeW = 60; p.eyeH = 72;
            p.lidSad = 0.55f;
            p.mouthW = 46; p.mouthCurve = -0.8f;
            p.eyeR = 80; p.eyeG = 140; p.eyeB = 255;
            p.lookY = 0.5f;
            break;
        case Expression::Angry:
            p.eyeW = 66; p.eyeH = 66;
            p.lidAngry = 0.75f;
            p.mouthW = 50; p.mouthCurve = -0.6f;
            p.eyeR = 255; p.eyeG = 70; p.eyeB = 40;
            break;
        case Expression::Surprised:
            p.eyeW = 72; p.eyeH = 98;
            p.mouthW = 30; p.mouthCurve = 0; p.mouthOpen = 1.0f;
            p.eyeR = 60; p.eyeG = 235; p.eyeB = 255;
            p.lookY = -0.2f;
            break;
        case Expression::Sleepy:
            p.eyeW = 70; p.eyeH = 84;
            p.openness = 0.16f;
            p.lidSad = 0.15f;
            p.mouthW = 34; p.mouthCurve = 0.1f; p.mouthOpen = 0.12f;
            p.eyeR = 20; p.eyeG = 120; p.eyeB = 170;
            p.lookY = 0.6f;
            break;
        case Expression::Confused:
            p.eyeScaleL = 1.12f; p.eyeScaleR = 0.72f;
            p.mouthW = 40; p.mouthCurve = -0.3f;
            p.eyeR = 120; p.eyeG = 210; p.eyeB = 255;
            p.lookY = -0.3f;
            break;
        case Expression::Listening:
            p.eyeW = 70; p.eyeH = 94;
            p.mouthW = 28; p.mouthCurve = 0.3f; p.mouthOpen = 0.2f;
            p.eyeR = 0; p.eyeG = 255; p.eyeB = 200;
            p.lookY = -0.15f;
            break;
        case Expression::Love:
            p.eyeW = 70; p.eyeH = 80;
            p.lidBottom = 0.5f;
            p.mouthW = 56; p.mouthCurve = 1.0f; p.mouthOpen = 0.25f;
            p.blush = 1.0f;
            p.eyeR = 255; p.eyeG = 90; p.eyeB = 170;
            break;
        case Expression::Laughing:   // squeezed-shut happy eyes, big open grin, bouncing
            p.eyeW = 72; p.eyeH = 76;
            p.lidBottom = 0.8f;
            p.mouthW = 76; p.mouthCurve = 1.0f; p.mouthOpen = 0.85f;
            p.blush = 0.8f;
            p.eyeR = 90; p.eyeG = 240; p.eyeB = 255;
            p.bounce = 1.0f;
            break;
        case Expression::Crying:     // sad eyes with tears running down
            p.eyeW = 60; p.eyeH = 70;
            p.lidSad = 0.65f;
            p.mouthW = 50; p.mouthCurve = -1.0f; p.mouthOpen = 0.3f;
            p.eyeR = 70; p.eyeG = 120; p.eyeB = 255;
            p.lookY = 0.4f;
            p.tears = 1.0f;
            break;
        case Expression::Embarrassed: // shy glance down and away, deep blush
            p.eyeW = 62; p.eyeH = 72;
            p.lidBottom = 0.3f;
            p.mouthW = 36; p.mouthCurve = 0.5f; p.mouthSkew = 0.4f;
            p.blush = 1.0f;
            p.eyeR = 255; p.eyeG = 150; p.eyeB = 190;
            p.lookY = 0.55f; p.lookX = -0.45f;
            break;
        case Expression::Shocked:    // even bigger than surprised, pale, tall "O"
            p.eyeW = 80; p.eyeH = 104;
            p.mouthW = 34; p.mouthCurve = 0; p.mouthOpen = 1.0f;
            p.eyeR = 200; p.eyeG = 245; p.eyeB = 255;
            p.lookY = -0.25f;
            break;
        case Expression::Thinking:   // looking up and to the side, mouth pushed over
            p.eyeW = 64; p.eyeH = 80;
            p.eyeScaleL = 0.92f;
            p.lidTop = 0.15f;
            p.mouthW = 30; p.mouthCurve = -0.1f; p.mouthSkew = 0.8f;
            p.eyeR = 120; p.eyeG = 200; p.eyeB = 255;
            p.lookY = -0.7f; p.lookX = 0.55f;
            break;
        case Expression::Winking:    // right eye shut, smirky smile
            p.eyeW = 68; p.eyeH = 84;
            p.opennessR = 0.0f;
            p.mouthW = 54; p.mouthCurve = 0.9f; p.mouthSkew = -0.5f;
            p.blush = 0.4f;
            p.eyeR = 60; p.eyeG = 235; p.eyeB = 255;
            break;
        case Expression::Cool:       // heavy flat lids, confident smirk
            p.eyeW = 74; p.eyeH = 70;
            p.lidTop = 0.55f;
            p.mouthW = 46; p.mouthCurve = 0.5f; p.mouthSkew = 0.7f;
            p.eyeR = 150; p.eyeG = 120; p.eyeB = 255;
            break;
        case Expression::Relaxed:    // content, half-closed eyes, soft smile
            p.eyeW = 70; p.eyeH = 78;
            p.lidTop = 0.4f; p.lidBottom = 0.25f;
            p.mouthW = 50; p.mouthCurve = 0.6f;
            p.blush = 0.3f;
            p.eyeR = 80; p.eyeG = 230; p.eyeB = 210;
            p.lookY = -0.1f;
            break;
        case Expression::Silly:      // lopsided eyes, tongue out
            p.eyeW = 66; p.eyeH = 84;
            p.eyeScaleL = 1.15f; p.eyeScaleR = 0.78f;
            p.mouthW = 60; p.mouthCurve = 0.9f; p.mouthSkew = -0.3f;
            p.tongue = 1.0f;
            p.eyeR = 120; p.eyeG = 255; p.eyeB = 160;
            p.lookX = -0.2f;
            break;
        default:
            break;
    }
    return p;
}

Face::Face() {
    cur_ = target_ = paramsFor(Expression::Neutral);
}

float Face::rand01() {
    // xorshift32: cheap and deterministic (handy for the desktop preview).
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / float(0x1000000);
}

void Face::setExpression(Expression e) {
    expr_ = e;
    target_ = paramsFor(e);
}

void Face::setLookAt(float x, float y) {
    lookOverride_ = true;
    lookTX_ = clampf(x, -1, 1);
    lookTY_ = clampf(y, -1, 1);
}

void Face::clearLookAt() {
    lookOverride_ = false;
    gazeTimer_ = 300;
}

void Face::blinkNow() {
    if (blinkPhase_ < 0) blinkPhase_ = 0;
}

void Face::update(uint32_t dtMsInt) {
    const float dt = (float)dtMsInt;
    timeMs_ += dt;

    // Tween every parameter toward the target expression.
    float* c = reinterpret_cast<float*>(&cur_);
    const float* t = reinterpret_cast<const float*>(&target_);
    for (int i = 0; i < kParamCount; ++i) c[i] = approach(c[i], t[i], dt, 90.0f);

    talk_ = approach(talk_, talkTarget_, dt, 35.0f);

    // Blinking.
    if (blinkPhase_ >= 0) {
        blinkPhase_ += dt;
        if (blinkPhase_ >= kBlinkMs) {
            blinkPhase_ = -1;
            if (doubleBlink_) {
                doubleBlink_ = false;
                blinkTimer_ = 90;
            } else {
                bool sleepy = expr_ == Expression::Sleepy;
                blinkTimer_ = sleepy ? randRange(3000, 7000) : randRange(1800, 5500);
                doubleBlink_ = rand01() < 0.18f;
            }
        }
    } else if (autoBlink_) {
        blinkTimer_ -= dt;
        if (blinkTimer_ <= 0) blinkPhase_ = 0;
    }

    // Gaze: wander unless something told us where to look.
    if (!lookOverride_) {
        gazeTimer_ -= dt;
        if (gazeTimer_ <= 0) {
            float range = (expr_ == Expression::Sleepy) ? 0.3f
                        : (expr_ == Expression::Listening) ? 0.25f : 0.9f;
            if (rand01() < 0.45f) {  // drift back toward center
                lookTX_ = randRange(-0.15f, 0.15f);
                lookTY_ = randRange(-0.1f, 0.1f);
            } else {
                lookTX_ = randRange(-range, range);
                lookTY_ = randRange(-range * 0.6f, range * 0.6f);
            }
            gazeTimer_ = randRange(700, 3500);
            // Blinking during a big glance looks natural.
            if (autoBlink_ && rand01() < 0.25f) blinkNow();
        }
    }
    lookX_ = approach(lookX_, lookTX_, dt, 55.0f);
    lookY_ = approach(lookY_, lookTY_, dt, 55.0f);
}

void Face::draw(Canvas& cv) const {
    const int W = cv.width(), H = cv.height();
    const float s = fminf(W / 320.0f, H / 240.0f);
    const uint16_t bg = 0x0000;
    const FaceParams& p = cur_;

    const int er = (int)p.eyeR, eg = (int)p.eyeG, eb = (int)p.eyeB;
    const uint16_t eyeCol = rgb565(er, eg, eb);
    const uint16_t shine = rgb565(er + (255 - er) * 7 / 10, eg + (255 - eg) * 7 / 10, eb + (255 - eb) * 7 / 10);

    // Blink: openness dips to 0 and back over kBlinkMs.
    float blink = 1.0f;
    if (blinkPhase_ >= 0) blink = fabsf(blinkPhase_ - kBlinkMs / 2) / (kBlinkMs / 2);
    const float openBoth = clampf(p.openness * blink, 0, 1);

    const float breath = sinf(timeMs_ * 2 * kPi / kBreathMs) * 2.5f * s
                       + fabsf(sinf(timeMs_ * 2 * kPi / 360.0f)) * -5.0f * s * clampf(p.bounce, 0, 1);
    const float gx = clampf(lookX_ + p.lookX, -1, 1) * 24 * s;
    const float gy = clampf(lookY_ + p.lookY, -1, 1) * 16 * s;

    cv.fillScreen(bg);

    const float cx = W * 0.5f;
    const float eyeY = H * 0.42f + breath + gy;
    const float eyeDX = 74 * s;

    for (int side = -1; side <= 1; side += 2) {
        // Perspective: the eye on the side we're looking toward grows a bit.
        float persp = 1.0f + lookX_ * 0.08f * side;
        float sc = (side < 0 ? p.eyeScaleL : p.eyeScaleR) * persp;
        float open = clampf(openBoth * (side < 0 ? p.opennessL : p.opennessR), 0, 1);
        float w = p.eyeW * s * sc;
        float hFull = p.eyeH * s * sc;
        float h = fmaxf(hFull * open, 5 * s);
        float x = cx + side * eyeDX + gx;
        float top = eyeY - h / 2;
        float left = x - w / 2;

        int r = (int)(fminf(w, h) * 0.3f);
        cv.fillRoundRect((int)left, (int)top, (int)w, (int)h, r, eyeCol);

        // Sparkle highlight, only when the eye is open enough to hold it.
        if (open > 0.55f && p.lidBottom < 0.45f && p.lidTop < 0.2f) {
            float hr = w * 0.1f;
            cv.fillCircle((int)(x - w * 0.2f), (int)(top + h * 0.24f), (int)hr, shine);
            cv.fillCircle((int)(x - w * 0.02f), (int)(top + h * 0.38f), (int)(hr * 0.45f), shine);
        }

        // Left eye (side=-1) has its inner edge, toward the nose, on its right.
        float innerX = x - side * (w / 2 + 3);
        float outerX = x + side * (w / 2 + 3);

        if (p.lidAngry > 0.01f) {
            cv.fillTriangle((int)outerX, (int)top - 3, (int)innerX, (int)top - 3,
                            (int)innerX, (int)(top + p.lidAngry * h * 0.6f), bg);
        }
        if (p.lidSad > 0.01f) {
            cv.fillTriangle((int)innerX, (int)top - 3, (int)outerX, (int)top - 3,
                            (int)outerX, (int)(top + p.lidSad * h * 0.6f), bg);
        }
        if (p.lidTop > 0.01f) {
            // Flat, heavy upper lid.
            cv.fillRect((int)(left - 3), (int)(top - 3), (int)(w + 6), (int)(3 + p.lidTop * h * 0.55f), bg);
        }
        if (p.lidBottom > 0.01f) {
            // A background-colored ellipse rising from below carves the eye
            // into an arch; the corners survive as the arch's "legs".
            float ry = h * 0.6f;
            float bottom = top + h;
            float cyLid = bottom + ry - p.lidBottom * h * 1.05f;
            cv.fillEllipse((int)x, (int)cyLid, (int)(w * 0.6f), (int)ry, bg);
        }

        if (p.tears > 0.05f) {
            // Two drops per eye sliding down from the outer corner, looping.
            const float t = clampf(p.tears, 0, 1);
            const uint16_t tc = rgb565((int)(90 * t), (int)(170 * t), (int)(255 * t));
            for (int d = 0; d < 2; ++d) {
                float phase = fmodf(timeMs_ / 1100.0f + d * 0.5f + (side > 0 ? 0.25f : 0), 1.0f);
                float tx = x + side * w * 0.32f;
                float ty = top + h + 4 * s + phase * 46 * s;
                float r = 4.5f * s;
                cv.fillCircle((int)tx, (int)ty, (int)r, tc);
                cv.fillTriangle((int)(tx - r), (int)ty, (int)(tx + r), (int)ty, (int)tx, (int)(ty - 2.4f * r), tc);
            }
        }

        if (p.blush > 0.02f) {
            float b = clampf(p.blush, 0, 1) * 0.6f;
            uint16_t bc = rgb565((int)(255 * b), (int)(70 * b), (int)(120 * b));
            cv.fillEllipse((int)(x + side * w * 0.45f), (int)(eyeY + hFull * 0.58f),
                           (int)(w * 0.3f), (int)(9 * s), bc);
        }
    }

    // Mouth: a thick parabola, plus an open "hole" when talking/surprised.
    const float mx = cx + gx * 0.5f + p.mouthSkew * p.mouthW * 0.18f * s;
    const float my = H * 0.78f + breath * 0.6f + gy * 0.35f;
    const float mw = p.mouthW * s;
    const float k = p.mouthCurve * 13 * s;    // depth of the curve
    const float thick = 3.6f * s;
    const float mOpen = clampf(p.mouthOpen + talk_ * 0.8f, 0, 1);

    if (mOpen <= 0.04f) {
        // Closed: thick parabola from corner to corner.
        const int N = 28;
        for (int i = 0; i <= N; ++i) {
            float u = (float)i / N * 2 - 1;  // -1..1 across the mouth
            float px = mx + u * mw / 2;
            float py = my - k * 0.5f + k * (1 - u * u) - p.mouthSkew * u * 7 * s;  // skew lifts one corner
            cv.fillCircle((int)px, (int)py, (int)thick, eyeCol);
        }
    } else if (k > 3 * s) {
        // Open smile: a half-ellipse "D" lying on its flat side.
        float rx = mw * 0.5f;
        float ry = 4 * s + mOpen * 26 * s;
        float y0 = my - k * 0.4f;
        cv.fillEllipse((int)mx, (int)y0, (int)rx, (int)ry, eyeCol);
        cv.fillRect((int)(mx - rx - 2), (int)(y0 - ry - 2), (int)(rx * 2 + 4), (int)(ry + 2), bg);
    } else {
        // Open "O" (surprised, talking, frowning-while-talking).
        float rx = fmaxf(mw * 0.42f, 9 * s) * (0.75f + 0.25f * mOpen);
        float ry = 3 * s + mOpen * 18 * s;
        cv.fillEllipse((int)mx, (int)my, (int)rx, (int)ry, eyeCol);
    }

    if (p.tongue > 0.05f) {
        // Tongue poking out below the middle of the mouth.
        const float t = clampf(p.tongue, 0, 1);
        const float tx = mx + mw * 0.12f;
        const float ty = (mOpen > 0.04f && k > 3 * s) ? my - k * 0.4f + (4 + mOpen * 26) * s * 0.9f
                                                      : my + k * 0.5f + 6 * s;
        cv.fillEllipse((int)tx, (int)ty, (int)(mw * 0.17f * t), (int)(11 * s * t),
                       rgb565((int)(255 * t), (int)(95 * t), (int)(140 * t)));
    }
}
