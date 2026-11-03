#include "Companion.h"
#include <math.h>

const CommandPhrase kCommandPhrases[] = {
    {VoiceCommand::Hello, "hello"},
    {VoiceCommand::Hello, "good morning"},
    {VoiceCommand::Hello, "hi there"},
    {VoiceCommand::GoodNight, "good night"},
    {VoiceCommand::GoodNight, "go to sleep"},
    {VoiceCommand::WakeUp, "wake up"},
    {VoiceCommand::HowAreYou, "how are you"},
    {VoiceCommand::ILoveYou, "i love you"},
    {VoiceCommand::ILoveYou, "you are cute"},
    {VoiceCommand::GoodJob, "good job"},
    {VoiceCommand::GoodJob, "well done"},
    {VoiceCommand::GoodJob, "you are awesome"},
    {VoiceCommand::Scold, "bad robot"},
    {VoiceCommand::Scold, "stop it"},
    {VoiceCommand::ThankYou, "thank you"},
    {VoiceCommand::TellJoke, "tell me a joke"},
    {VoiceCommand::TellJoke, "make me laugh"},
    {VoiceCommand::LookLeft, "look left"},
    {VoiceCommand::LookRight, "look right"},
    {VoiceCommand::BeHappy, "be happy"},
    {VoiceCommand::BeSad, "be sad"},
    {VoiceCommand::BeAngry, "be angry"},
    {VoiceCommand::BeSurprised, "be surprised"},
};
const int kCommandPhraseCount = sizeof(kCommandPhrases) / sizeof(kCommandPhrases[0]);

const char* commandName(VoiceCommand c) {
    switch (c) {
        case VoiceCommand::Hello:       return "hello";
        case VoiceCommand::GoodNight:   return "good night";
        case VoiceCommand::WakeUp:      return "wake up";
        case VoiceCommand::HowAreYou:   return "how are you";
        case VoiceCommand::ILoveYou:    return "i love you";
        case VoiceCommand::GoodJob:     return "good job";
        case VoiceCommand::Scold:       return "scold";
        case VoiceCommand::ThankYou:    return "thank you";
        case VoiceCommand::TellJoke:    return "tell joke";
        case VoiceCommand::LookLeft:    return "look left";
        case VoiceCommand::LookRight:   return "look right";
        case VoiceCommand::BeHappy:     return "be happy";
        case VoiceCommand::BeSad:       return "be sad";
        case VoiceCommand::BeAngry:     return "be angry";
        case VoiceCommand::BeSurprised: return "be surprised";
        default:                        return "none";
    }
}

namespace {
// Wrap-safe "a is before b" for millisecond timestamps.
inline bool before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
}  // namespace

Companion::Companion(Face& face) : face_(face) {}

void Companion::react(Expression e, uint32_t ms, uint32_t now) {
    reaction_ = e;
    reactionUntil_ = now + ms;
}

void Companion::look(float x, float y, uint32_t ms, uint32_t now) {
    lookX_ = x;
    lookY_ = y;
    lookUntil_ = now + ms;
}

// strong = touch, motion, wake word: also wakes it from "good night" sleep.
void Companion::interact(uint32_t now, bool strong) {
    if (asleep_ && !strong) return;
    const bool wasDozing = asleep_ || now - lastInteraction_ > sleepAfterMs;
    asleep_ = false;
    lastInteraction_ = now;
    if (wasDozing) {
        react(Expression::Surprised, 900, now);  // "huh? I'm awake!"
        face_.blinkNow();
    }
}

void Companion::onTap(uint32_t now) {
    const bool wasDozing = asleep_ || now - lastInteraction_ > sleepAfterMs;
    interact(now, true);
    if (!wasDozing) react(Expression::Happy, 2500, now);
}

void Companion::onHold(uint32_t now) {
    interact(now, true);
    react(Expression::Love, 2000, now);  // refreshed while being petted
}

void Companion::onShake(uint32_t now) {
    interact(now, true);
    react(Expression::Confused, 2500, now);
}

void Companion::onTilt(float x, float y, uint32_t now) {
    if (!tilted_) interact(now, true);
    tilted_ = true;
    tiltX_ = x;
    tiltY_ = y;
}

void Companion::onTiltEnd() { tilted_ = false; }

void Companion::onAudio(float db, bool speech, uint32_t now) {
    const float jump = db - prevDb_;
    prevDb_ = db;
    if (asleep_) return;  // sleeping through the noise

    if (!awaiting_ && jump > clapJumpDb && db > clapMinDb) {
        interact(now, false);
        react(Expression::Surprised, 1200, now);
        return;
    }

    if (speech) {
        if (!speech_) interact(now, false);
        speech_ = true;
        lastSpeech_ = now;
    } else if (speech_ && now - lastSpeech_ > speechEndMs) {
        speech_ = false;
        // "I heard you!" -- unless that speech was a wake word or command,
        // in which case the command's own reaction is already showing.
        if (!voiceEventSeen_ || now - lastVoiceEvent_ > 3000) react(Expression::Happy, 1500, now);
    }
}

void Companion::onWakeWord(uint32_t now) {
    interact(now, true);
    awaiting_ = true;
    lastVoiceEvent_ = now;
    voiceEventSeen_ = true;
    reactionUntil_ = now;  // clear any reaction; listening takes over
    face_.blinkNow();
}

void Companion::onCommand(VoiceCommand cmd, uint32_t now) {
    awaiting_ = false;
    lastVoiceEvent_ = now;
    voiceEventSeen_ = true;
    interact(now, true);
    reactionUntil_ = now;  // the command decides, not the wake-up surprise

    switch (cmd) {
        case VoiceCommand::Hello:       react(Expression::Happy, 3000, now); break;
        case VoiceCommand::GoodNight:   asleep_ = true; break;
        case VoiceCommand::WakeUp:      react(Expression::Surprised, 1000, now); break;
        case VoiceCommand::HowAreYou:
            react(lowBattery_ ? Expression::Sleepy : Expression::Happy, 2500, now);
            break;
        case VoiceCommand::ILoveYou:    react(Expression::Love, 4000, now); break;
        case VoiceCommand::GoodJob:     react(Expression::Happy, 3000, now); break;
        case VoiceCommand::Scold:       react(Expression::Sad, 4000, now); break;
        case VoiceCommand::ThankYou:    react(Expression::Love, 2500, now); break;
        case VoiceCommand::TellJoke:
            react(Expression::Happy, 2500, now);
            laughUntil_ = now + 2500;
            break;
        case VoiceCommand::LookLeft:    look(-1, 0, 2500, now); break;
        case VoiceCommand::LookRight:   look(1, 0, 2500, now); break;
        case VoiceCommand::BeHappy:     react(Expression::Happy, 4000, now); break;
        case VoiceCommand::BeSad:       react(Expression::Sad, 4000, now); break;
        case VoiceCommand::BeAngry:     react(Expression::Angry, 4000, now); break;
        case VoiceCommand::BeSurprised: react(Expression::Surprised, 4000, now); break;
        default: break;
    }
}

void Companion::onCommandTimeout(uint32_t now) {
    awaiting_ = false;
    lastVoiceEvent_ = now;
    voiceEventSeen_ = true;
    react(Expression::Confused, 1500, now);  // "hmm? never mind"
}

void Companion::update(uint32_t now) {
    const uint32_t idle = now - lastInteraction_;
    dim_ = asleep_ || idle > dimAfterMs;

    // Expression priority: waiting for a command > reaction > hearing speech > mood.
    Expression want = (asleep_ || lowBattery_ || idle > sleepAfterMs) ? Expression::Sleepy
                                                                       : Expression::Neutral;
    if (speech_ && !asleep_) want = Expression::Listening;
    if (before(now, reactionUntil_)) want = reaction_;
    if (awaiting_) want = Expression::Listening;
    if (want != face_.expression()) face_.setExpression(want);

    // Gaze priority: commanded look > attentive (awaiting) > tilt > wander.
    bool override = true;
    float gx = 0, gy = 0;
    if (before(now, lookUntil_)) {
        gx = lookX_; gy = lookY_;
    } else if (awaiting_) {
        gx = 0; gy = 0;
    } else if (tilted_) {
        gx = tiltX_; gy = tiltY_;
    } else {
        override = false;
    }
    if (override) {
        face_.setLookAt(gx, gy);
    } else if (lookActive_) {
        face_.clearLookAt();
    }
    lookActive_ = override;

    // Laughing: bounce the mouth.
    if (before(now, laughUntil_)) {
        face_.setTalkLevel(0.45f + 0.45f * sinf(now * 0.03f));
    } else {
        face_.setTalkLevel(0);
    }
}
