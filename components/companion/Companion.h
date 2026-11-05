#pragma once
#include <stdint.h>
#include "Face.h"
#include "Sfx.h"

// Things you can say after "Hey Buddy". The numeric value is the command ID
// given to the speech recognizer (it must start at 1).
enum class VoiceCommand : int {
    None = 0,
    Hello = 1,
    GoodNight,
    WakeUp,
    HowAreYou,
    ILoveYou,
    GoodJob,
    Scold,
    ThankYou,
    TellJoke,
    LookLeft,
    LookRight,
    BeHappy,
    BeSad,
    BeAngry,
    BeSurprised,
    BeQuiet,
    SoundOn,
    Wink,
    BeCool,
    BeSilly,
    Count
};

struct CommandPhrase {
    VoiceCommand command;
    const char* text;  // lowercase English, letters and spaces only
};

// Phrases the recognizer listens for. Several phrases can map to one command.
extern const CommandPhrase kCommandPhrases[];
extern const int kCommandPhraseCount;

const char* commandName(VoiceCommand c);

// The companion's "brain": turns events (touch, motion, sound, voice) into
// what the face shows. Hardware-free so it can be unit-tested on a PC.
// All times are milliseconds from any monotonic clock.
class Companion {
public:
    explicit Companion(Face& face);

    // Touch / motion
    void onTap(uint32_t now);
    void onHold(uint32_t now);
    void onShake(uint32_t now);
    void onTilt(float x, float y, uint32_t now);  // -1..1 each
    void onTiltEnd();

    // Proximity sensor: someone sat down in front of it / went away.
    void onArrived(uint32_t now);
    void onLeft(uint32_t now);

    // Camera: a face is visible at (x, y), each -1..1 in the buddy's own gaze
    // directions (x: -1 = look left, +1 = look right; y: -1 = up, +1 = down).
    // Call for every camera frame that contains a face.
    void onFace(float x, float y, uint32_t now);
    bool seesFace(uint32_t now) const;

    // Audio: called for every processed audio frame.
    void onAudio(float levelDb, bool speech, uint32_t now);

    // Voice
    void onWakeWord(uint32_t now);
    void onCommand(VoiceCommand cmd, uint32_t now);
    void onCommandTimeout(uint32_t now);

    // Show a named emotion ("laughing", "thinking", ... see kEmotionNames in
    // Face.h) for `ms`, with a matching sound. For an AI server that tags its
    // replies with emotions. Returns false for unknown names.
    bool onEmotion(const char* emotion, uint32_t now, uint32_t ms = 4000);

    void setLowBattery(bool low) { lowBattery_ = low; }

    // Sound effects: the main loop plays whatever this returns (once).
    Sfx takeSound() {
        Sfx s = pending_;
        pending_ = Sfx::None;
        return s;
    }
    void setMuted(bool m) { muted_ = m; }
    bool isMuted() const { return muted_; }

    // Call every frame; updates the face's expression, gaze and mouth.
    void update(uint32_t now);

    bool wantsDim() const { return dim_; }
    bool isAsleep() const { return asleep_; }
    bool isAwaitingCommand() const { return awaiting_; }

    // Tunables
    uint32_t sleepAfterMs = 60000;
    uint32_t dimAfterMs = 180000;
    float clapJumpDb = 22.0f;     // sudden rise between frames that counts as a clap
    float clapMinDb = -30.0f;     // ...and it must be at least this loud
    uint32_t speechEndMs = 1200;  // quiet this long = they stopped talking
    uint32_t sleepAfterLeftMs = 20000;  // doze off sooner once you walk away
    uint32_t faceLostMs = 1500;         // keep looking where the face was this long
    uint32_t faceAwayMs = 10000;        // face gone this long, then seeing it again greets you

private:
    void react(Expression e, uint32_t ms, uint32_t now);
    void interact(uint32_t now, bool strong);
    void look(float x, float y, uint32_t ms, uint32_t now);
    void play(Sfx s, uint32_t now);

    Face& face_;

    uint32_t lastInteraction_ = 0;
    bool asleep_ = false;      // told "good night"; only strong events wake it
    bool lowBattery_ = false;
    bool dim_ = false;

    Expression reaction_ = Expression::Neutral;
    uint32_t reactionUntil_ = 0;

    bool awaiting_ = false;    // heard the wake word, waiting for a command
    uint32_t lastVoiceEvent_ = 0;
    bool voiceEventSeen_ = false;

    bool speech_ = false;
    uint32_t lastSpeech_ = 0;
    float prevDb_ = -90.0f;

    bool tilted_ = false;
    float tiltX_ = 0, tiltY_ = 0;
    float lookX_ = 0, lookY_ = 0;
    uint32_t lookUntil_ = 0;
    bool lookActive_ = false;  // whether we currently override the face's gaze

    uint32_t laughUntil_ = 0;

    bool muted_ = false;
    Sfx pending_ = Sfx::None;
    Sfx playing_ = Sfx::None;   // for lip-sync
    uint32_t soundStart_ = 0;
    uint32_t lastPurr_ = 0, lastShakeSound_ = 0;
    bool petting_ = false, shaking_ = false;
    bool idleSleepy_ = false;   // already yawned for this idle stretch
    uint32_t lastGreeting_ = 0;
    void greet(uint32_t now);

    bool present_ = false;      // proximity sensor says someone is at the desk
    bool faceSeen_ = false;
    uint32_t lastFace_ = 0;
    float faceX_ = 0, faceY_ = 0;
    bool greeted_ = false;
};
