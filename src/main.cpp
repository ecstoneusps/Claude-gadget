// Desk companion for the M5Stack CoreS3.
//
// The face engine (lib/Face) handles drawing and animation. This file wires it
// to the hardware and decides how the face reacts:
//   tap screen      -> happy (or wakes it up if asleep)
//   hold / "pet"    -> love
//   shake           -> confused/dizzy
//   tilt            -> eyes look toward the low side
//   loud sudden sound (clap) -> surprised
//   sustained sound (talking) -> listening, then a happy "I heard you"
//   nobody around for a while -> sleepy, then the screen dims
//
// Voice recognition comes later; for now the mic only measures loudness.

#include <M5Unified.h>
#include <math.h>

#include "Face.h"

// ---------------------------------------------------------------------------
// Canvas adapter: lets the portable face engine draw into an M5Canvas sprite.

class M5CanvasAdapter : public Canvas {
public:
    explicit M5CanvasAdapter(M5Canvas& c) : c_(c) {}
    int width() const override { return c_.width(); }
    int height() const override { return c_.height(); }
    void fillScreen(uint16_t col) override { c_.fillScreen(col); }
    void fillRect(int x, int y, int w, int h, uint16_t col) override { c_.fillRect(x, y, w, h, col); }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t col) override {
        c_.fillRoundRect(x, y, w, h, r, col);
    }
    void fillCircle(int x, int y, int r, uint16_t col) override { c_.fillCircle(x, y, r, col); }
    void fillEllipse(int x, int y, int rx, int ry, uint16_t col) override { c_.fillEllipse(x, y, rx, ry, col); }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col) override {
        c_.fillTriangle(x0, y0, x1, y1, x2, y2, col);
    }

private:
    M5Canvas& c_;
};

// ---------------------------------------------------------------------------
// Tunables

constexpr uint32_t kFrameMs = 33;             // ~30 fps
constexpr uint32_t kSleepAfterMs = 60000;     // no interaction -> sleepy
constexpr uint32_t kDimAfterMs = 180000;      // ...then dim the screen
constexpr uint8_t kBrightness = 160;
constexpr uint8_t kDimBrightness = 20;

constexpr uint32_t kMicRate = 16000;
constexpr size_t kMicChunk = 256;             // 16 ms of audio per chunk
constexpr float kClapRatio = 6.0f;            // level vs. noise floor for "surprised"
constexpr float kVoiceRatio = 2.2f;           // level vs. noise floor for "someone is talking"
constexpr uint32_t kVoiceMinMs = 300;         // sustained sound before we "listen"
constexpr uint32_t kVoiceEndMs = 1200;        // quiet this long = they stopped talking

constexpr float kShakeG = 1.0f;               // deviation from 1 g that counts as a shake
constexpr float kTiltG = 0.25f;               // tilt before the eyes follow gravity

// ---------------------------------------------------------------------------

M5Canvas canvas(&M5.Display);
M5CanvasAdapter canvasAdapter(canvas);
Face face;

// An optional short-lived reaction overrides the base mood.
Expression reaction = Expression::Neutral;
uint32_t reactionUntil = 0;

uint32_t lastInteraction = 0;
bool dimmed = false;
bool lowBattery = false;

void react(Expression e, uint32_t durationMs) {
    reaction = e;
    reactionUntil = millis() + durationMs;
}

void noteInteraction() {
    const uint32_t now = millis();
    if (now - lastInteraction > kSleepAfterMs) {
        react(Expression::Surprised, 900);  // "huh? I'm awake!"
        face.blinkNow();
    }
    lastInteraction = now;
    if (dimmed) {
        M5.Display.setBrightness(kBrightness);
        dimmed = false;
    }
}

// --- Microphone ------------------------------------------------------------

int16_t micBuf[3][kMicChunk];
int micIdx = 0;
float noiseFloor = 200;          // adapts to the room
uint32_t voiceStart = 0, lastVoice = 0;
bool listening = false;

void handleMic() {
    // M5Unified queues up to two recordings. Only queue a new one when a slot
    // is free so the frame loop never blocks; the buffer queued two calls ago
    // is then complete and safe to read.
    if (M5.Mic.isRecording() >= 2) return;
    if (!M5.Mic.record(micBuf[micIdx], kMicChunk, kMicRate)) return;
    const int16_t* done = micBuf[(micIdx + 1) % 3];
    micIdx = (micIdx + 1) % 3;

    double sum = 0;
    for (size_t i = 0; i < kMicChunk; ++i) sum += (double)done[i] * done[i];
    float level = sqrtf(sum / kMicChunk);

    const uint32_t now = millis();
    const bool loud = level > noiseFloor * kVoiceRatio;

    // Track background noise only while it's quiet, so speech doesn't raise it.
    if (!loud) noiseFloor += (level - noiseFloor) * 0.02f;
    if (noiseFloor < 30) noiseFloor = 30;

    if (level > noiseFloor * kClapRatio && !listening) {
        noteInteraction();
        react(Expression::Surprised, 1200);
        voiceStart = 0;  // a clap shouldn't start "listening"
        return;
    }

    if (loud) {
        if (voiceStart == 0) voiceStart = now;
        lastVoice = now;
        if (!listening && now - voiceStart > kVoiceMinMs) {
            listening = true;
            noteInteraction();
        }
    } else if (now - lastVoice > kVoiceEndMs) {
        voiceStart = 0;
        if (listening) {
            listening = false;
            react(Expression::Happy, 1500);  // "I heard you!"
        }
    }
}

// --- Touch -----------------------------------------------------------------

void handleTouch() {
    if (M5.Touch.getCount() == 0) return;
    const auto t = M5.Touch.getDetail();
    if (t.isHolding()) {
        noteInteraction();
        react(Expression::Love, 2000);  // keep refreshing while being petted
    } else if (t.wasClicked()) {
        noteInteraction();
        if (reaction != Expression::Surprised) react(Expression::Happy, 2500);
    }
}

// --- Motion ----------------------------------------------------------------

bool tilted = false;
bool haveRest = false;
float restX = 0, restY = 0;  // gravity in the screen plane when sitting normally

void handleImu() {
    static uint32_t lastRead = 0;
    if (millis() - lastRead < 20) return;  // 50 Hz is plenty
    lastRead = millis();

    float ax, ay, az;
    M5.Imu.update();
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    // Learn the resting pose (standing up, lying flat, ...) and slowly re-learn
    // it so the eyes don't stay stuck to one side after it's moved.
    if (!haveRest) {
        restX = ax; restY = ay; haveRest = true;
    } else {
        restX += (ax - restX) * 0.002f;
        restY += (ay - restY) * 0.002f;
    }

    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (fabsf(mag - 1.0f) > kShakeG) {
        noteInteraction();
        react(Expression::Confused, 2500);
    }

    // Change in gravity relative to rest tells us which way it's tilted.
    // If the eyes look the wrong way on your unit, flip these signs.
    const float tx = -(ax - restX), ty = (ay - restY);
    if (fabsf(tx) > kTiltG || fabsf(ty) > kTiltG) {
        face.setLookAt(tx * 1.5f, ty * 1.5f);
        if (!tilted) noteInteraction();
        tilted = true;
    } else if (tilted) {
        face.clearLookAt();
        tilted = false;
    }
}

// ---------------------------------------------------------------------------

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setRotation(1);  // landscape, 320x240
    M5.Display.setBrightness(kBrightness);

    canvas.setColorDepth(16);
    canvas.setPsram(true);
    if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
        M5.Display.println("Sprite alloc failed");
        while (true) delay(1000);
    }

    // On the CoreS3 the speaker and mic can't run at the same time in
    // M5Unified; we only listen for now.
    M5.Speaker.end();
    M5.Mic.begin();

    face.setSeed(esp_random());
    lastInteraction = millis();
    Serial.println("Desk companion ready");
}

void loop() {
    static uint32_t lastFrame = millis();
    const uint32_t now = millis();

    M5.update();
    handleTouch();
    handleImu();
    handleMic();

    // Low battery makes it drowsy unless it's charging. Checked every 10 s.
    static uint32_t lastBattery = 0;
    if (now - lastBattery > 10000 || lastBattery == 0) {
        lastBattery = now;
        lowBattery = !M5.Power.isCharging() && M5.Power.getBatteryLevel() < 10;
    }

    // Idle -> sleepy -> dim.
    const uint32_t idle = now - lastInteraction;
    if (idle > kDimAfterMs && !dimmed) {
        M5.Display.setBrightness(kDimBrightness);
        dimmed = true;
    }
    const Expression baseMood = (idle > kSleepAfterMs || lowBattery) ? Expression::Sleepy : Expression::Neutral;

    // Pick what to show: listening > active reaction > base mood.
    Expression want = baseMood;
    if ((int32_t)(reactionUntil - now) > 0) want = reaction;
    if (listening) want = Expression::Listening;
    if (want != face.expression()) face.setExpression(want);

    if (now - lastFrame >= kFrameMs) {
        face.update(now - lastFrame);
        lastFrame = now;
        face.draw(canvasAdapter);
        canvas.pushSprite(0, 0);
    }
    delay(1);
}
