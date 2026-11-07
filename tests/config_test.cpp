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
    CHECK(cfg.radar && cfg.radarDistanceCm == 150);
    CHECK(cfg.extraPhrases.empty());  // examples are commented out
    CHECK(cfg.wifiName.empty() && cfg.wifiPassword.empty() && cfg.haUrl.empty() && cfg.haToken.empty());
    CHECK(cfg.haPipeline == "Buddy");
    CHECK(cfg.conversation && cfg.followUp);
    CHECK(!cfg.aiConfigured());
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

static void test_ai_settings() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig("wifi_name = My Home WiFi   # the house network\r\n"
                "wifi_password = \"pa ss#word \"  # quoted: keeps the # and spaces\n"
                "ha_url = http://192.168.1.20:8123/\n"
                "ha_token = eyJhbGciOi.abc.def\n"
                "ha_pipeline = Buddy brain\n"
                "follow_up = off\n",
                cfg, w);
    for (auto& s : w) printf("  unexpected warning: %s\n", s.c_str());
    CHECK(w.empty());
    CHECK(cfg.wifiName == "My Home WiFi");
    CHECK(cfg.wifiPassword == "pa ss#word ");
    CHECK(cfg.haUrl == "http://192.168.1.20:8123");  // trailing slash dropped
    CHECK(cfg.haToken == "eyJhbGciOi.abc.def");
    CHECK(cfg.haPipeline == "Buddy brain");
    CHECK(!cfg.followUp);
    CHECK(cfg.aiConfigured());
    cfg.conversation = false;
    CHECK(!cfg.aiConfigured());
}

static void test_ai_settings_mistakes() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig("ha_url = 192.168.1.20:8123\n"
                "wifi_password = short\n"
                "wifi_password = \"unterminated\n"
                "ha_token = abc def\n"
                "wifi_name = \"x\" y\n"
                "# comment = not a setting\n"
                "just words # and a comment\n",
                cfg, w);
    CHECK(w.size() == 6);
    CHECK(cfg.haUrl.empty() && cfg.wifiPassword.empty() && cfg.haToken.empty() && cfg.wifiName.empty());
    // Warnings name the setting but never echo its value (the token is a secret).
    for (auto& s : w) CHECK(s.find("abc def") == std::string::npos);
}

static void test_upgrade_old_file() {
    // A file from before the AI settings existed gets the new section appended once.
    std::string old = "volume = 40\nbrightness = 50";
    const std::string extra = configUpgradeText(old);
    CHECK(extra.find("ha_token") != std::string::npos);
    CHECK(extra.find("wifi_name") != std::string::npos);
    const std::string upgraded = old + extra;
    CHECK(configUpgradeText(upgraded).empty());
    CHECK(configUpgradeText(defaultConfigText()).empty());
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig(upgraded, cfg, w);
    CHECK(w.empty());
    CHECK(cfg.volume == 40 && cfg.brightness == 50 && cfg.haPipeline == "Buddy");
}

static void test_upgrade_adds_single_missing_settings() {
    // An older file: everything except face_model and the camera preview, and
    // with proximity commented out on purpose.
    std::string old;
    {
        const std::string d = defaultConfigText();
        size_t pos = 0;
        while (pos < d.size()) {
            size_t nl = d.find('\n', pos);
            std::string l = d.substr(pos, nl - pos);
            pos = nl + 1;
            if (l.rfind("face_model", 0) == 0 || l.rfind("camera_preview", 0) == 0) continue;
            if (l.rfind("proximity ", 0) == 0) l = "# " + l;
            old += l + "\n";
        }
    }
    const std::string extra = configUpgradeText(old);
    CHECK(extra.find("face_model") != std::string::npos);
    CHECK(extra.find("camera_preview") != std::string::npos);
    CHECK(extra.find("# --- Camera --- (added by an update)") != std::string::npos);
    CHECK(extra.find("proximity ") == std::string::npos);  // commented out = your choice, left alone
    CHECK(extra.find("volume") == std::string::npos);
    CHECK(extra.find("wifi_name") == std::string::npos);
    CHECK(configUpgradeText(old + extra).empty());  // only once
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig(old + extra, cfg, w);
    for (auto& s : w) printf("  unexpected warning: %s\n", s.c_str());
    CHECK(w.empty());
}

static void test_set_value() {
    const std::string t = "# volume = 10\nvolume                       = 63     # speaker volume, 0-100\nbrightness = 50\n";
    const std::string u = configSetValue(t, "volume", "100");
    CHECK(u == "# volume = 10\nvolume                       = 100    # speaker volume, 0-100\nbrightness = 50\n");
    CHECK(configSetValue("brightness = 50", "volume", "70") == "brightness = 50\nvolume = 70\n");
    CHECK(configSetValue("volume=5\r\nx = 1\r\n", "volume", "90") == "volume= 90\r\nx = 1\r\n");
    CHECK(configSetValue("volume = 5 #c\n", "VOLUME", "100000") == "volume = 100000 #c\n");
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig(configSetValue(defaultConfigText(), "volume", "85"), cfg, w);
    CHECK(w.empty() && cfg.volume == 85);
}

static void test_vision_settings() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    CHECK(cfg.visionUrl.empty() && cfg.visionModel == "gemma3:4b");
    parseConfig("vision_url = http://192.168.4.98:11434/\nvision_model = qwen2.5vl:7b\nvision_model = two words\n",
                cfg, w);
    CHECK(cfg.visionUrl == "http://192.168.4.98:11434");
    CHECK(cfg.visionModel == "qwen2.5vl:7b");
    CHECK(w.size() == 2);  // set twice, and not a model name
}

static void test_radar_settings() {
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig("radar = off\nradar_distance_cm = 90\nradar_distance_cm = 5\n", cfg, w);
    CHECK(!cfg.radar && cfg.radarDistanceCm == 90);
    CHECK(w.size() == 2);  // "set again" and "must be 30-600"
}

static void test_duplicate_settings() {
    // You fill in a line, but the template's empty line is still further down.
    BuddyConfig cfg;
    std::vector<std::string> w;
    parseConfig("wifi_name = airport\n"
                "volume = 40\n"
                "wifi_name =        # your Wi-Fi network\n"
                "volume = 50\n"
                "say.hello = howdy\n"
                "say.hello = yo buddy\n",
                cfg, w);
    CHECK(cfg.wifiName == "airport");  // the empty copy doesn't erase it
    CHECK(cfg.volume == 50);            // a second real value wins
    CHECK(cfg.extraPhrases.size() == 2);
    CHECK(w.size() == 2);
    CHECK(w.size() == 2 && w[0].find("line 3") == 0 && w[0].find("using line 1") != std::string::npos);
    CHECK(w.size() == 2 && w[1].find("line 4") == 0 && w[1].find("already set on line 2") != std::string::npos);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--write-settings") == 0) g_writeSettings = argv[1];
    struct { const char* name; void (*fn)(); } tests[] = {
        {"default file round trips", test_default_file_round_trips},
        {"values and formats", test_values_and_formats},
        {"bad lines warn and keep defaults", test_bad_lines_warn_and_keep_defaults},
        {"extra phrases", test_extra_phrases},
        {"command keys round trip", test_command_keys_round_trip},
        {"AI settings", test_ai_settings},
        {"AI settings mistakes", test_ai_settings_mistakes},
        {"older settings file is upgraded", test_upgrade_old_file},
        {"duplicate settings", test_duplicate_settings},
        {"radar settings", test_radar_settings},
        {"vision settings", test_vision_settings},
        {"set a value in the file", test_set_value},
        {"upgrade adds single missing settings", test_upgrade_adds_single_missing_settings},
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
