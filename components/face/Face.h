#pragma once
#include <stdint.h>
#include "Canvas.h"

// Portable animated face: two eyes and a mouth that tween smoothly between
// expressions, blink on their own, glance around, and "breathe".
// No Arduino dependencies, so it also builds for the desktop preview.

enum class Expression : uint8_t {
    Neutral,
    Happy,
    Sad,
    Angry,
    Surprised,
    Sleepy,
    Confused,
    Listening,
    Love,
    Laughing,
    Crying,
    Embarrassed,
    Shocked,
    Thinking,
    Winking,
    Cool,
    Relaxed,
    Silly,
    Count
};

const char* expressionName(Expression e);

// The emotion names AI voice servers send (xiaozhi-esp32's set: "happy",
// "laughing", "crying", "thinking", "winking", ...). Each maps to the closest
// expression. Returns false for names it doesn't know.
bool expressionForEmotion(const char* emotion, Expression& out);
extern const char* const kEmotionNames[];
extern const int kEmotionNameCount;

// Every value here is tweened, so any two expressions blend smoothly.
struct FaceParams {
    float eyeW, eyeH;        // eye size, in units of a 320x240 screen
    float eyeScaleL, eyeScaleR; // per-eye size multiplier (confused = lopsided)
    float openness;          // 0 = closed, 1 = fully open
    float lidAngry;          // 0..1 top lid slants down toward the nose
    float lidSad;            // 0..1 top lid slants down toward the outside
    float lidBottom;         // 0..1 bottom lid pushes up (happy "^ ^" eyes)
    float mouthW;            // mouth width
    float mouthCurve;        // -1 frown .. +1 smile
    float mouthOpen;         // 0..1
    float blush;             // 0..1 cheek glow
    float eyeR, eyeG, eyeB;  // eye/mouth color, 0..255
    float lookY;             // vertical gaze bias (-1 up .. +1 down)
    float lookX;             // horizontal gaze bias (-1 left .. +1 right)
    float opennessL, opennessR; // per-eye openness multiplier (winking = one closed)
    float lidTop;            // 0..1 flat top lid (cool, relaxed: heavy-lidded)
    float tears;             // 0..1 falling tears
    float tongue;            // 0..1 tongue sticking out (silly)
    float mouthSkew;         // -1..1 crooked mouth / smirk
    float bounce;            // 0..1 bouncing with laughter
};

FaceParams paramsFor(Expression e);

class Face {
public:
    Face();

    // Advance animation by dtMs milliseconds.
    void update(uint32_t dtMs);
    void draw(Canvas& c) const;

    void setExpression(Expression e);
    Expression expression() const { return expr_; }

    // Override the wandering gaze. x,y in -1..1. Call clearLookAt() to resume wandering.
    void setLookAt(float x, float y);
    void clearLookAt();
    bool isLookOverridden() const { return lookOverride_; }
    float lookTargetX() const { return lookTX_; }
    float lookTargetY() const { return lookTY_; }

    // 0..1 mouth movement while the device is talking (from audio level).
    void setTalkLevel(float level) { talkTarget_ = level; }

    void blinkNow();
    void setAutoBlink(bool on) { autoBlink_ = on; }
    void setSeed(uint32_t seed) { rng_ = seed ? seed : 1; }

private:
    float rand01();
    float randRange(float a, float b) { return a + (b - a) * rand01(); }

    Expression expr_ = Expression::Neutral;
    FaceParams cur_, target_;

    // blink
    bool autoBlink_ = true;
    float blinkTimer_ = 2000;   // ms until next blink
    float blinkPhase_ = -1;     // <0 = not blinking, else ms into blink
    bool doubleBlink_ = false;

    // gaze
    bool lookOverride_ = false;
    float lookX_ = 0, lookY_ = 0, lookTX_ = 0, lookTY_ = 0;
    float gazeTimer_ = 1500;

    // misc
    float talk_ = 0, talkTarget_ = 0;
    float timeMs_ = 0;
    uint32_t rng_ = 0x12345678;
};
