// Native unit tests for the companion "brain". Run: tests/run_tests.sh
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "Companion.h"
#include "Face.h"
#include "Sfx.h"
#include "Presence.h"

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                 \
        }                                                               \
    } while (0)

struct Rig {
    Face face;
    Companion c{face};
    uint32_t now = 1000;
    // Advance time in frame-sized steps, like the real loop.
    void run(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 33) {
            now += 33;
            c.update(now);
            face.update(33);
        }
    }
    Expression expr() const { return face.expression(); }
};

static void test_tap_makes_happy_then_neutral() {
    Rig r;
    r.run(100);
    CHECK(r.expr() == Expression::Neutral);
    r.c.onTap(r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Happy);
    r.run(3000);
    CHECK(r.expr() == Expression::Neutral);
}

static void test_idle_sleep_and_dim_then_wake() {
    Rig r;
    r.run(61000);
    CHECK(r.expr() == Expression::Sleepy);
    CHECK(!r.c.wantsDim());
    r.run(120000);
    CHECK(r.c.wantsDim());
    r.c.onTap(r.now);  // tapping a dozing buddy startles it awake
    r.run(100);
    CHECK(r.expr() == Expression::Surprised);
    CHECK(!r.c.wantsDim());
    r.run(1500);
    CHECK(r.expr() == Expression::Neutral);
}

static void test_wake_word_listens_then_command_reacts() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.run(100);
    CHECK(r.c.isAwaitingCommand());
    CHECK(r.expr() == Expression::Listening);
    r.c.onCommand(VoiceCommand::ILoveYou, r.now);
    r.run(100);
    CHECK(!r.c.isAwaitingCommand());
    CHECK(r.expr() == Expression::Love);
    r.run(4500);
    CHECK(r.expr() == Expression::Neutral);
}

static void test_command_timeout_is_confused() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.run(500);
    r.c.onCommandTimeout(r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Confused);
    r.run(2000);
    CHECK(r.expr() == Expression::Neutral);
}

static void test_good_night_sleeps_through_noise_until_wake_word() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.c.onCommand(VoiceCommand::GoodNight, r.now);
    r.run(100);
    CHECK(r.c.isAsleep());
    CHECK(r.c.wantsDim());
    CHECK(r.expr() == Expression::Sleepy);
    // Loud noise and talking don't wake it.
    r.c.onAudio(-70, false, r.now);
    r.c.onAudio(-10, false, r.now);
    r.c.onAudio(-20, true, r.now);
    r.run(100);
    CHECK(r.c.isAsleep());
    CHECK(r.expr() == Expression::Sleepy);
    // The wake word does.
    r.c.onWakeWord(r.now);
    r.run(100);
    CHECK(!r.c.isAsleep());
    CHECK(r.expr() == Expression::Listening);
}

static void test_clap_surprises() {
    Rig r;
    r.c.onAudio(-65, false, r.now);
    r.c.onAudio(-15, false, r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Surprised);
}

static void test_quiet_noise_rise_is_not_a_clap() {
    Rig r;
    r.c.onAudio(-80, false, r.now);
    r.c.onAudio(-50, false, r.now);  // big jump but still quiet
    r.run(100);
    CHECK(r.expr() == Expression::Neutral);
}

static void test_speech_listens_then_acknowledges() {
    Rig r;
    for (int i = 0; i < 20; ++i) {
        r.c.onAudio(-35, true, r.now);
        r.run(33);
    }
    CHECK(r.expr() == Expression::Listening);
    for (int i = 0; i < 50; ++i) {
        r.c.onAudio(-60, false, r.now);
        r.run(33);
    }
    CHECK(r.expr() == Expression::Happy);  // "I heard you!"
}

static void test_command_reaction_not_overridden_by_speech_end() {
    Rig r;
    r.c.onWakeWord(r.now);
    for (int i = 0; i < 20; ++i) {
        r.c.onAudio(-35, true, r.now);
        r.run(33);
    }
    r.c.onCommand(VoiceCommand::Scold, r.now);
    for (int i = 0; i < 60; ++i) {  // speech ends ~1.2 s later
        r.c.onAudio(-60, false, r.now);
        r.run(33);
    }
    CHECK(r.expr() == Expression::Sad);
}

static void test_low_battery_is_sleepy() {
    Rig r;
    r.c.setLowBattery(true);
    r.run(100);
    CHECK(r.expr() == Expression::Sleepy);
    r.c.onWakeWord(r.now);
    r.c.onCommand(VoiceCommand::HowAreYou, r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Sleepy);
}

static void test_phrases_are_valid_for_recognizer() {
    // MultiNet needs lowercase letters and single spaces, no digits/punctuation,
    // at most 63 characters; every command except None must have a phrase.
    bool covered[(int)VoiceCommand::Count] = {};
    for (int i = 0; i < kCommandPhraseCount; ++i) {
        const char* t = kCommandPhrases[i].text;
        size_t n = strlen(t);
        CHECK(n >= 2 && n <= 63);
        for (size_t j = 0; j < n; ++j) CHECK(islower((unsigned char)t[j]) || t[j] == ' ');
        CHECK(t[0] != ' ' && t[n - 1] != ' ' && !strstr(t, "  "));
        int id = (int)kCommandPhrases[i].command;
        CHECK(id >= 1 && id < (int)VoiceCommand::Count);
        if (id >= 1 && id < (int)VoiceCommand::Count) covered[id] = true;
        for (int k = 0; k < i; ++k) CHECK(strcmp(t, kCommandPhrases[k].text) != 0);
    }
    for (int id = 1; id < (int)VoiceCommand::Count; ++id) {
        if (!covered[id]) printf("  command %s has no phrase\n", commandName((VoiceCommand)id));
        CHECK(covered[id]);
    }
}

static void test_sounds_follow_events() {
    Rig r;
    r.c.onTap(r.now);
    CHECK(r.c.takeSound() == Sfx::Boop);
    CHECK(r.c.takeSound() == Sfx::None);  // played once
    r.c.onWakeWord(r.now);
    CHECK(r.c.takeSound() == Sfx::Chirp);
    r.c.onCommand(VoiceCommand::TellJoke, r.now);
    CHECK(r.c.takeSound() == Sfx::Giggle);
    r.c.onWakeWord(r.now);
    r.c.onCommandTimeout(r.now);
    CHECK(r.c.takeSound() == Sfx::Confused);
    r.c.onWakeWord(r.now);
    r.c.onCommand(VoiceCommand::GoodNight, r.now);
    CHECK(r.c.takeSound() == Sfx::Yawn);
    r.c.onTap(r.now);  // startled awake
    CHECK(r.c.takeSound() == Sfx::Wake);
}

static void test_be_quiet_mutes_until_sound_on() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.c.takeSound();
    r.c.onCommand(VoiceCommand::BeQuiet, r.now);
    CHECK(r.c.isMuted());
    CHECK(r.c.takeSound() == Sfx::None);
    r.c.onTap(r.now);
    r.c.onWakeWord(r.now);
    CHECK(r.c.takeSound() == Sfx::None);
    r.c.onCommand(VoiceCommand::SoundOn, r.now);
    CHECK(!r.c.isMuted());
    CHECK(r.c.takeSound() == Sfx::Chirp);
}

static void test_petting_purrs_without_spamming() {
    Rig r;
    int purrs = 0;
    for (int i = 0; i < 120; ++i) {  // ~4 s of holding, one event per frame
        r.c.onHold(r.now);
        if (r.c.takeSound() == Sfx::Purr) ++purrs;
        r.run(33);
    }
    CHECK(purrs >= 2 && purrs <= 3);
}

static void test_yawns_once_when_dozing_off() {
    Rig r;
    int yawns = 0;
    for (int i = 0; i < 200; ++i) {  // 100 s idle
        r.run(500);
        if (r.c.takeSound() == Sfx::Yawn) ++yawns;
    }
    CHECK(yawns == 1);
}

static void test_mouth_sings_along() {
    Rig r;
    r.c.onCommand(VoiceCommand::ILoveYou, r.now);
    r.c.takeSound();
    bool moved = false;
    for (int i = 0; i < 10; ++i) {
        r.run(33);
        if (sfxLevelAt(Sfx::Love, 33 * (i + 1)) > 0.3f) moved = true;
    }
    CHECK(moved);
}

static void test_sfx_render_matches_length() {
    for (int i = 1; i < (int)Sfx::Count; ++i) {
        Sfx s = (Sfx)i;
        size_t n = sfxSamples(s);
        CHECK(n > 0);
        int16_t* buf = new int16_t[n + 16];
        for (size_t k = 0; k < n + 16; ++k) buf[k] = 0x5A5A;
        CHECK(sfxRender(s, buf) == n);
        for (size_t k = n; k < n + 16; ++k) CHECK(buf[k] == 0x5A5A);  // no overrun
        CHECK(buf[0] == 0);                                           // starts silent (fade-in)
        CHECK(sfxLevelAt(s, sfxDurationMs(s) + 1) == 0.0f);
        delete[] buf;
    }
}

// Feeds `ms` worth of 50 ms readings at `raw` (+/- a little noise).
static PresenceDetector::Event feed(PresenceDetector& p, uint32_t& now, uint16_t raw, uint32_t ms,
                                    int* arrivals = nullptr, int* lefts = nullptr) {
    PresenceDetector::Event last = PresenceDetector::Event::None;
    for (uint32_t t = 0; t < ms; t += 50) {
        now += 50;
        uint16_t v = raw + ((now / 50) % 3) - 1;  // tiny jitter
        auto e = p.update(v, now);
        if (e == PresenceDetector::Event::Arrived && arrivals) ++*arrivals;
        if (e == PresenceDetector::Event::Left && lefts) ++*lefts;
        if (e != PresenceDetector::Event::None) last = e;
    }
    return last;
}

static void test_presence_arrive_and_leave() {
    PresenceDetector p;
    uint32_t now = 0;
    int arrivals = 0, lefts = 0;
    feed(p, now, 40, 5000, &arrivals, &lefts);      // empty desk
    CHECK(!p.present() && arrivals == 0);
    feed(p, now, 140, 1000, &arrivals, &lefts);     // someone sits down
    CHECK(p.present() && arrivals == 1);
    feed(p, now, 140, 60000, &arrivals, &lefts);    // stays a minute
    CHECK(p.present() && arrivals == 1 && lefts == 0);
    feed(p, now, 40, 4000, &arrivals, &lefts);      // walks away
    CHECK(!p.present() && lefts == 1);
}

static void test_presence_ignores_blips_and_drift() {
    PresenceDetector p;
    uint32_t now = 0;
    int arrivals = 0;
    feed(p, now, 40, 3000, &arrivals);
    feed(p, now, 200, 150, &arrivals);   // something flicks past
    feed(p, now, 40, 3000, &arrivals);
    CHECK(arrivals == 0);
    for (int level = 40; level <= 70; level += 2)  // slow ambient drift
        feed(p, now, level, 5000, &arrivals);
    CHECK(arrivals == 0);
    CHECK(p.baseline() > 60);
}

static void test_presence_recovers_from_object_left_nearby() {
    PresenceDetector p;
    uint32_t now = 0;
    int arrivals = 0, lefts = 0;
    feed(p, now, 40, 3000, &arrivals, &lefts);
    feed(p, now, 300, 1000, &arrivals, &lefts);           // a mug parked next to it
    CHECK(p.present());
    feed(p, now, 300, 20 * 60000, &arrivals, &lefts);     // 20 minutes later
    CHECK(!p.present() && lefts == 1);                     // accepted as the new "empty"
    feed(p, now, 40, 3000, &arrivals, &lefts);            // mug removed
    CHECK(p.baseline() < 60);                             // follows back down quickly
}

static void test_sitting_down_wakes_and_greets() {
    Rig r;
    r.run(70000);                       // dozed off
    CHECK(r.expr() == Expression::Sleepy);
    r.c.takeSound();
    r.c.onArrived(r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Happy);
    CHECK(r.c.takeSound() == Sfx::Happy);
    r.c.onLeft(r.now);
    r.c.onArrived(r.now);               // hand wave right after: no second greeting
    CHECK(r.c.takeSound() == Sfx::None);
}

static void test_good_night_then_sit_down_wakes() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.c.onCommand(VoiceCommand::GoodNight, r.now);
    r.run(100);
    CHECK(r.c.isAsleep());
    r.c.onArrived(r.now);
    r.run(100);
    CHECK(!r.c.isAsleep());
    CHECK(r.expr() == Expression::Happy);
}

static void test_walking_away_dozes_sooner() {
    Rig r;
    r.c.onTap(r.now);
    r.run(3000);
    r.c.onLeft(r.now);
    r.run(15000);
    CHECK(r.expr() != Expression::Sleepy);  // not yet
    r.run(6000);
    CHECK(r.expr() == Expression::Sleepy);  // ~20 s after leaving, not 60
}

static void test_eyes_follow_face() {
    Rig r;
    r.c.onTap(r.now);
    r.c.onFace(0.6f, -0.2f, r.now);
    r.run(33);
    CHECK(r.face.isLookOverridden());
    CHECK(r.face.lookTargetX() > 0.5f && r.face.lookTargetY() < 0);
    for (int i = 0; i < 10; ++i) {        // you move to the other side
        r.c.onFace(-0.6f, 0.0f, r.now);
        r.run(33);
    }
    CHECK(r.face.lookTargetX() < -0.5f);
    r.run(2000);                          // you leave the frame
    CHECK(!r.face.isLookOverridden());    // back to glancing around
}

static void test_look_command_beats_face() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.c.onFace(0.5f, 0, r.now);
    r.run(33);
    CHECK(r.face.lookTargetX() > 0.4f);   // listening, looking at you
    r.c.onCommand(VoiceCommand::LookLeft, r.now);
    r.c.onFace(0.5f, 0, r.now);
    r.run(33);
    CHECK(r.face.lookTargetX() < -0.9f);  // obeys "look left" first
}

static void test_seeing_face_wakes_dozing_buddy() {
    Rig r;
    r.run(70000);
    CHECK(r.expr() == Expression::Sleepy);
    r.c.takeSound();
    r.c.onFace(0, 0, r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Happy);
    CHECK(r.c.takeSound() == Sfx::Happy);
}

static void test_face_does_not_override_good_night() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.c.onCommand(VoiceCommand::GoodNight, r.now);
    for (int i = 0; i < 30; ++i) {
        r.c.onFace(0.3f, 0, r.now);
        r.run(200);
    }
    CHECK(r.c.isAsleep());
    CHECK(r.expr() == Expression::Sleepy);
}

static void test_face_while_awake_does_not_regreet() {
    Rig r;
    r.c.onTap(r.now);
    r.c.takeSound();
    r.c.onFace(0, 0, r.now);
    CHECK(r.c.takeSound() == Sfx::None);  // already awake: just looks at you
}

static void test_every_ai_emotion_maps_to_an_expression() {
    // xiaozhi-esp32's full emotion vocabulary.
    const char* names[] = {"neutral", "happy", "laughing", "funny", "sad", "angry", "crying",
                           "loving", "embarrassed", "surprised", "shocked", "thinking", "winking",
                           "cool", "relaxed", "delicious", "kissy", "confident", "sleepy", "silly",
                           "confused"};
    CHECK(kEmotionNameCount == 21);
    for (const char* n : names) {
        Expression e;
        if (!expressionForEmotion(n, e)) printf("  no expression for \"%s\"\n", n);
        CHECK(expressionForEmotion(n, e));
    }
    Expression e;
    CHECK(expressionForEmotion("laughing", e) && e == Expression::Laughing);
    CHECK(expressionForEmotion("kissy", e) && e == Expression::Love);
    CHECK(expressionForEmotion("listening", e) && e == Expression::Listening);  // our own names too
    CHECK(!expressionForEmotion("grumpy", e));
    CHECK(!expressionForEmotion(nullptr, e));
}

static void test_expression_names_unique() {
    for (int i = 0; i < (int)Expression::Count; ++i)
        for (int j = i + 1; j < (int)Expression::Count; ++j)
            CHECK(strcmp(expressionName((Expression)i), expressionName((Expression)j)) != 0);
}

static void test_on_emotion_shows_face_and_sound() {
    Rig r;
    CHECK(r.c.onEmotion("crying", r.now, 3000));
    r.run(100);
    CHECK(r.expr() == Expression::Crying);
    CHECK(r.c.takeSound() == Sfx::Sad);
    r.run(3500);
    CHECK(r.expr() == Expression::Neutral);
    CHECK(!r.c.onEmotion("grumpy", r.now));
    CHECK(r.c.takeSound() == Sfx::None);
}

static void test_new_face_commands() {
    Rig r;
    r.c.onWakeWord(r.now);
    r.c.onCommand(VoiceCommand::Wink, r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Winking);
    r.c.onCommand(VoiceCommand::BeSilly, r.now);
    r.run(100);
    CHECK(r.expr() == Expression::Silly);
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
        {"tap makes happy then neutral", test_tap_makes_happy_then_neutral},
        {"idle sleep, dim, wake", test_idle_sleep_and_dim_then_wake},
        {"wake word then command", test_wake_word_listens_then_command_reacts},
        {"command timeout is confused", test_command_timeout_is_confused},
        {"good night sleeps until wake word", test_good_night_sleeps_through_noise_until_wake_word},
        {"clap surprises", test_clap_surprises},
        {"quiet noise rise is not a clap", test_quiet_noise_rise_is_not_a_clap},
        {"speech listens then acknowledges", test_speech_listens_then_acknowledges},
        {"command reaction survives speech end", test_command_reaction_not_overridden_by_speech_end},
        {"low battery is sleepy", test_low_battery_is_sleepy},
        {"phrases valid for recognizer", test_phrases_are_valid_for_recognizer},
        {"sounds follow events", test_sounds_follow_events},
        {"be quiet mutes until sound on", test_be_quiet_mutes_until_sound_on},
        {"petting purrs without spamming", test_petting_purrs_without_spamming},
        {"yawns once when dozing off", test_yawns_once_when_dozing_off},
        {"mouth sings along", test_mouth_sings_along},
        {"sfx render matches length", test_sfx_render_matches_length},
        {"presence arrive and leave", test_presence_arrive_and_leave},
        {"presence ignores blips and drift", test_presence_ignores_blips_and_drift},
        {"presence recovers from object left nearby", test_presence_recovers_from_object_left_nearby},
        {"sitting down wakes and greets", test_sitting_down_wakes_and_greets},
        {"good night then sit down wakes", test_good_night_then_sit_down_wakes},
        {"walking away dozes sooner", test_walking_away_dozes_sooner},
        {"eyes follow face", test_eyes_follow_face},
        {"look command beats face", test_look_command_beats_face},
        {"seeing face wakes dozing buddy", test_seeing_face_wakes_dozing_buddy},
        {"face does not override good night", test_face_does_not_override_good_night},
        {"face while awake does not re-greet", test_face_while_awake_does_not_regreet},
        {"every AI emotion maps to an expression", test_every_ai_emotion_maps_to_an_expression},
        {"expression names unique", test_expression_names_unique},
        {"onEmotion shows face and sound", test_on_emotion_shows_face_and_sound},
        {"new face commands", test_new_face_commands},
    };
    for (auto& t : tests) {
        int before = failures;
        t.fn();
        printf("%s %s\n", failures == before ? "PASS" : "FAIL", t.name);
    }
    printf(failures ? "\n%d check(s) failed\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}
