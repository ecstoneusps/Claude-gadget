#pragma once
#include <stdint.h>
#include "Face.h"

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

    // Audio: called for every processed audio frame.
    void onAudio(float levelDb, bool speech, uint32_t now);

    // Voice
    void onWakeWord(uint32_t now);
    void onCommand(VoiceCommand cmd, uint32_t now);
    void onCommandTimeout(uint32_t now);

    void setLowBattery(bool low) { lowBattery_ = low; }

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

private:
    void react(Expression e, uint32_t ms, uint32_t now);
    void interact(uint32_t now, bool strong);
    void look(float x, float y, uint32_t ms, uint32_t now);

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
};
