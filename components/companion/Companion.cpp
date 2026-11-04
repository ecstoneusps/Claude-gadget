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
    {VoiceCommand::BeQuiet, "be quiet"},
    {VoiceCommand::BeQuiet, "quiet please"},
    {VoiceCommand::SoundOn, "sound on"},
    {VoiceCommand::SoundOn, "you can talk"},
    {VoiceCommand::Wink, "wink at me"},
    {VoiceCommand::Wink, "give me a wink"},
    {VoiceCommand::BeCool, "be cool"},
    {VoiceCommand::BeSilly, "be silly"},
    {VoiceCommand::BeSilly, "make a silly face"},
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
        case VoiceCommand::BeQuiet:     return "be quiet";
        case VoiceCommand::SoundOn:     return "sound on";
        case VoiceCommand::Wink:        return "wink";
        case VoiceCommand::BeCool:      return "be cool";
        case VoiceCommand::BeSilly:     return "be silly";
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

void Companion::play(Sfx s, uint32_t now) {
    if (muted_ || s == Sfx::None) return;
    pending_ = s;
    playing_ = s;
    soundStart_ = now;
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
        play(Sfx::Wake, now);
    }
}

void Companion::onTap(uint32_t now) {
    const bool wasDozing = asleep_ || now - lastInteraction_ > sleepAfterMs;
    interact(now, true);
    if (!wasDozing) {
        react(Expression::Happy, 2500, now);
        play(Sfx::Boop, now);
    }
}

void Companion::onHold(uint32_t now) {
    interact(now, true);
    react(Expression::Love, 2000, now);  // refreshed while being petted
    // Purr on and off while the petting continues.
    if (!petting_ || now - lastPurr_ > 1500) {
        play(Sfx::Purr, now);
        lastPurr_ = now;
    }
    petting_ = true;
}

void Companion::onShake(uint32_t now) {
    interact(now, true);
    react(Expression::Confused, 2500, now);
    if (!shaking_ || now - lastShakeSound_ > 2000) {
        play(Sfx::Confused, now);
        lastShakeSound_ = now;
    }
    shaking_ = true;
}

void Companion::onTilt(float x, float y, uint32_t now) {
    if (!tilted_) interact(now, true);
    tilted_ = true;
    tiltX_ = x;
    tiltY_ = y;
}

void Companion::onTiltEnd() { tilted_ = false; }

void Companion::greet(uint32_t now) {
    // Not every time a hand waves past the sensor.
    if (greeted_ && now - lastGreeting_ <= 15000) return;
    reactionUntil_ = now;
    react(Expression::Happy, 2500, now);
    if (!seesFace(now)) look(0, -0.3f, 1500, now);  // look up toward where you probably are
    play(Sfx::Happy, now);
    lastGreeting_ = now;
    greeted_ = true;
}

void Companion::onArrived(uint32_t now) {
    if (awaiting_) return;  // mid-conversation; don't interrupt
    interact(now, true);    // wakes it, even from "good night"
    greet(now);
}

bool Companion::seesFace(uint32_t now) const {
    return faceSeen_ && now - lastFace_ < faceLostMs;
}

void Companion::onFace(float x, float y, uint32_t now) {
    const bool returning = !faceSeen_ || now - lastFace_ > faceAwayMs;
    // Smooth out detector jitter; jump straight there when a face first appears.
    if (!seesFace(now)) {
        faceX_ = x;
        faceY_ = y;
    } else {
        faceX_ += (x - faceX_) * 0.5f;
        faceY_ += (y - faceY_) * 0.5f;
    }
    faceSeen_ = true;
    lastFace_ = now;

    // You came back into view: wake up and say hi -- but if you said
    // "good night", it keeps sleeping even though it can see you.
    if (returning && !asleep_ && !awaiting_) {
        const bool dozing = now - lastInteraction_ > sleepAfterMs;
        if (dozing) {
            interact(now, true);
            greet(now);
        }
    }
}

void Companion::onLeft(uint32_t now) {
    if (asleep_) return;
    // Pretend the last interaction was long ago so it dozes off
    // sleepAfterLeftMs from now (unless it was going to sleep sooner anyway).
    const uint32_t idle = now - lastInteraction_;
    if (sleepAfterMs > sleepAfterLeftMs && idle < sleepAfterMs - sleepAfterLeftMs)
        lastInteraction_ = now - (sleepAfterMs - sleepAfterLeftMs);
}

void Companion::onAudio(float db, bool speech, uint32_t now) {
    const float jump = db - prevDb_;
    prevDb_ = db;
    if (asleep_) return;  // sleeping through the noise

    if (!awaiting_ && jump > clapJumpDb && db > clapMinDb) {
        interact(now, false);
        react(Expression::Surprised, 1200, now);
        play(Sfx::Surprised, now);
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
    play(Sfx::Chirp, now);  // "yes?"
}

void Companion::onCommand(VoiceCommand cmd, uint32_t now) {
    awaiting_ = false;
    lastVoiceEvent_ = now;
    voiceEventSeen_ = true;
    interact(now, true);
    reactionUntil_ = now;  // the command decides, not the wake-up surprise

    switch (cmd) {
        case VoiceCommand::Hello:
            react(Expression::Happy, 3000, now); play(Sfx::Happy, now); break;
        case VoiceCommand::GoodNight:
            asleep_ = true; play(Sfx::Yawn, now); break;
        case VoiceCommand::WakeUp:
            react(Expression::Surprised, 1000, now); play(Sfx::Wake, now); break;
        case VoiceCommand::HowAreYou:
            react(lowBattery_ ? Expression::Sleepy : Expression::Happy, 2500, now);
            play(lowBattery_ ? Sfx::Yawn : Sfx::Happy, now);
            break;
        case VoiceCommand::ILoveYou:
            react(Expression::Love, 4000, now); play(Sfx::Love, now); break;
        case VoiceCommand::GoodJob:
            react(Expression::Happy, 3000, now); play(Sfx::Happy, now); break;
        case VoiceCommand::Scold:
            react(Expression::Sad, 4000, now); play(Sfx::Sad, now); break;
        case VoiceCommand::ThankYou:
            react(Expression::Love, 2500, now); play(Sfx::Love, now); break;
        case VoiceCommand::TellJoke:
            react(Expression::Happy, 2500, now);
            laughUntil_ = now + 2500;
            play(Sfx::Giggle, now);
            break;
        case VoiceCommand::LookLeft:
            look(-1, 0, 2500, now); play(Sfx::Boop, now); break;
        case VoiceCommand::LookRight:
            look(1, 0, 2500, now); play(Sfx::Boop, now); break;
        case VoiceCommand::BeHappy:
            react(Expression::Happy, 4000, now); play(Sfx::Happy, now); break;
        case VoiceCommand::BeSad:
            react(Expression::Sad, 4000, now); play(Sfx::Sad, now); break;
        case VoiceCommand::BeAngry:
            react(Expression::Angry, 4000, now); play(Sfx::Angry, now); break;
        case VoiceCommand::BeSurprised:
            react(Expression::Surprised, 4000, now); play(Sfx::Surprised, now); break;
        case VoiceCommand::BeQuiet:
            muted_ = true; react(Expression::Sad, 1500, now); break;
        case VoiceCommand::SoundOn:
            muted_ = false; react(Expression::Happy, 2000, now); play(Sfx::Chirp, now); break;
        case VoiceCommand::Wink:
            react(Expression::Winking, 1500, now); play(Sfx::Chirp, now); break;
        case VoiceCommand::BeCool:
            react(Expression::Cool, 4000, now); play(Sfx::Boop, now); break;
        case VoiceCommand::BeSilly:
            react(Expression::Silly, 4000, now); play(Sfx::Giggle, now); break;
        default: break;
    }
}

namespace {
// The sound that goes with each expression when shown on request.
Sfx soundFor(Expression e) {
    switch (e) {
        case Expression::Happy:       return Sfx::Happy;
        case Expression::Laughing:    return Sfx::Giggle;
        case Expression::Silly:       return Sfx::Giggle;
        case Expression::Love:        return Sfx::Love;
        case Expression::Embarrassed: return Sfx::Love;
        case Expression::Sad:         return Sfx::Sad;
        case Expression::Crying:      return Sfx::Sad;
        case Expression::Angry:       return Sfx::Angry;
        case Expression::Surprised:   return Sfx::Surprised;
        case Expression::Shocked:     return Sfx::Surprised;
        case Expression::Confused:    return Sfx::Confused;
        case Expression::Thinking:    return Sfx::Confused;
        case Expression::Winking:     return Sfx::Chirp;
        case Expression::Cool:        return Sfx::Boop;
        case Expression::Sleepy:      return Sfx::Yawn;
        default:                      return Sfx::None;  // neutral, relaxed, listening
    }
}
}  // namespace

bool Companion::onEmotion(const char* emotion, uint32_t now, uint32_t ms) {
    Expression e;
    if (!expressionForEmotion(emotion, e)) return false;
    interact(now, true);
    reactionUntil_ = now;
    react(e, ms, now);
    play(soundFor(e), now);
    if (e == Expression::Laughing) laughUntil_ = now + (ms < 2500 ? ms : 2500);
    return true;
}

void Companion::onCommandTimeout(uint32_t now) {
    awaiting_ = false;
    lastVoiceEvent_ = now;
    voiceEventSeen_ = true;
    react(Expression::Confused, 1500, now);  // "hmm? never mind"
    play(Sfx::Confused, now);
}

void Companion::update(uint32_t now) {
    const uint32_t idle = now - lastInteraction_;
    dim_ = asleep_ || idle > dimAfterMs;

    // Yawn once when dozing off from boredom.
    if (idle > sleepAfterMs && !asleep_) {
        if (!idleSleepy_) play(Sfx::Yawn, now);
        idleSleepy_ = true;
    } else {
        idleSleepy_ = false;
    }
    // Petting/shaking streaks end when the events stop.
    if (now - lastPurr_ > 2500) petting_ = false;
    if (now - lastShakeSound_ > 3000) shaking_ = false;

    // Expression priority: waiting for a command > reaction > hearing speech > mood.
    Expression want = (asleep_ || lowBattery_ || idle > sleepAfterMs) ? Expression::Sleepy
                                                                       : Expression::Neutral;
    if (speech_ && !asleep_) want = Expression::Listening;
    if (before(now, reactionUntil_)) want = reaction_;
    if (awaiting_) want = Expression::Listening;
    if (want != face_.expression()) face_.setExpression(want);

    // Gaze priority: commanded look > your face > attentive (awaiting) > tilt > wander.
    bool override = true;
    float gx = 0, gy = 0;
    if (before(now, lookUntil_)) {
        gx = lookX_; gy = lookY_;
    } else if (seesFace(now) && !asleep_) {
        gx = faceX_; gy = faceY_;
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

    // Mouth: bounce while laughing, and "sing" along with sound effects.
    float talk = 0;
    if (before(now, laughUntil_)) talk = 0.45f + 0.45f * sinf(now * 0.03f);
    if (playing_ != Sfx::None) {
        uint32_t t = now - soundStart_;
        if (t < sfxDurationMs(playing_)) talk = fmaxf(talk, sfxLevelAt(playing_, t));
        else playing_ = Sfx::None;
    }
    face_.setTalkLevel(talk);
}
