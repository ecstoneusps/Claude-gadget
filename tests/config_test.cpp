// Native unit tests for the settings.txt parser. Run: tests/run_tests.sh
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <sstream>

#include "BuddyConfig.h"

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void test_default_file_round_trips() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig(defaultConfigText(), cfg, w);
    for (auto& s : w) printf("  unexpected warning: %s\n", s.c_str());
    CHECK(w.empty());
    const BuddyConfig d;
    CHECK(cfg.volume == d.volume && cfg.brightness == d.brightness && cfg.dimBrightness == d.dimBrightness);
    CHECK(cfg.sleepAfterSeconds == d.sleepAfterSeconds && cfg.dimAfterSeconds == d.dimAfterSeconds);
    CHECK(cfg.sleepAfterLeavingSeconds == d.sleepAfterLeavingSeconds);
    CHECK(cfg.commandConfidence == d.commandConfidence);
    CHECK(cfg.twoMicrophones == d.twoMicrophones);
    CHECK(cfg.faceTracking == d.faceTracking && cfg.cameraMirrored == d.cameraMirrored);
    CHECK(cfg.cameraPreview == d.cameraPreview);
    CHECK(cfg.accurateFaceModel == d.accurateFaceModel);
    CHECK(cfg.faceGaze > 1.39f && cfg.faceGaze < 1.41f);
    CHECK(cfg.proximity == d.proximity && cfg.proximitySensitivity == d.proximitySensitivity);
    CHECK(cfg.flipTiltX == d.flipTiltX && cfg.flipTiltY == d.flipTiltY);
    CHECK(cfg.extraPhrases.empty());  // examples are commented out
}

static void test_values_and_formats() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig("Volume = 40\r\n"
                "brightness=100   # max\n"
                "  sleep_after_seconds = 300\n"
                "command_confidence = 0.35\n"
                "camera_mirrored = YES\n"
                "face_tracking = off\n"
                "flip_tilt_x = true\n"
                "face_model = Fast\n",
                cfg, w);
    CHECK(w.empty());
    CHECK(cfg.volume == 40 && cfg.brightness == 100 && cfg.sleepAfterSeconds == 300);
    CHECK(cfg.commandConfidence > 0.34f && cfg.commandConfidence < 0.36f);
    CHECK(cfg.cameraMirrored && !cfg.faceTracking && cfg.flipTiltX);
    CHECK(!cfg.accurateFaceModel);
}

static void test_bad_lines_warn_and_keep_defaults() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig("volume = 150\n"
                "brightness = bright\n"
                "proximity = maybe\n"
                "colour = blue\n"
                "just some words\n"
                "say.fly_away = up up\n",
                cfg, w);
    CHECK(w.size() == 6);
    CHECK(cfg.volume == 63 && cfg.brightness == 63 && cfg.proximity);
    if (w.size() == 6) {
        CHECK(w[0] == "line 1: volume must be 0-100");
        CHECK(w[3] == "line 4: unknown setting \"colour\"");
        CHECK(w[4] == "line 5: expected \"name = value\"");
    }
}

static void test_extra_phrases() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig("say.hello = Howdy,  whats   up buddy , ,yo\n"
                "say.good_night = nighty night, sleep tight!\n",
                cfg, w);
    CHECK(cfg.extraPhrases.size() == 4);  // howdy, whats up buddy, yo, nighty night
    if (cfg.extraPhrases.size() == 4) {
        CHECK(cfg.extraPhrases[0].first == VoiceCommand::Hello && cfg.extraPhrases[0].second == "howdy");
        CHECK(cfg.extraPhrases[1].second == "whats up buddy");  // spaces tidied, lowercased
        CHECK(cfg.extraPhrases[2].second == "yo");
        CHECK(cfg.extraPhrases[3].first == VoiceCommand::GoodNight && cfg.extraPhrases[3].second == "nighty night");
    }
    CHECK(w.size() == 1);  // "sleep tight!" has punctuation
    if (w.size() == 1) CHECK(w[0] == "line 2: \"sleep tight!\" can only use letters and spaces (2-63 characters)");
}

static void test_command_keys_round_trip() {
    for (int i = 1; i < (int)VoiceCommand::Count; ++i) {
        VoiceCommand c;
        CHECK(commandFromKey(commandKey((VoiceCommand)i), c) && c == (VoiceCommand)i);
    }
    CHECK(commandKey(VoiceCommand::GoodNight) == "say.good_night");
}

// sdcard/settings.txt in the repo is a copy of what the buddy writes to a fresh
// card. Regenerate with: tests/run_tests.sh --write-settings
static const char* g_writeSettings = nullptr;
static void test_repo_copy_matches_firmware_default() {
    const std::string path = "sdcard/settings.txt";
    if (g_writeSettings) {
        std::ofstream(path, std::ios::binary) << defaultConfigText();
        return;
    }
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    if (ss.str() != defaultConfigText())
        printf("  sdcard/settings.txt is out of date; run tests/run_tests.sh --write-settings\n");
    CHECK(ss.str() == defaultConfigText());
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--write-settings") == 0) g_writeSettings = argv[1];
    struct { const char* name; void (*fn)(); } tests[] = {
        {"default file round trips", test_default_file_round_trips},
        {"values and formats", test_values_and_formats},
        {"bad lines warn and keep defaults", test_bad_lines_warn_and_keep_defaults},
        {"extra phrases", test_extra_phrases},
        {"command keys round trip", test_command_keys_round_trip},
        {"repo copy of settings.txt matches firmware", test_repo_copy_matches_firmware_default},
    };
    for (auto& t : tests) {
        int before = failures;
        t.fn();
        printf("%s %s\n", failures == before ? "PASS" : "FAIL", t.name);
    }
    printf(failures ? "\n%d check(s) failed\n" : "\nall config tests passed\n", failures);
    return failures ? 1 : 0;
}
