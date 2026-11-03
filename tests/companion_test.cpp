// Native unit tests for the companion "brain". Run: tests/run_tests.sh
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "Companion.h"
#include "Face.h"

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
    };
    for (auto& t : tests) {
        int before = failures;
        t.fn();
        printf("%s %s\n", failures == before ? "PASS" : "FAIL", t.name);
    }
    printf(failures ? "\n%d check(s) failed\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}
