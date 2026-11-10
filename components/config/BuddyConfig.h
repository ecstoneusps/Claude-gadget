#pragma once
#include <string>
#include <utility>
#include <vector>

#include "Companion.h"  // VoiceCommand

// Everything you can change from settings.txt on the SD card. Portable, so the
// parser is unit-tested on a PC. Defaults here are the built-in behavior.
struct BuddyConfig {
    // Sound and screen
    int volume = 63;               // 0-100
    int brightness = 63;           // 0-100
    int dimBrightness = 8;         // 0-100
    bool puppyVoice = true;        // sound_style: puppy (yips, barks) or robot (chirps, boops)
    bool screenFlip = true;        // screen_flip: the CoreS3 is mounted turned around (camera at the top, ports down)

    // Sleepiness
    int sleepAfterSeconds = 60;
    int dimAfterSeconds = 180;
    int sleepAfterLeavingSeconds = 20;

    // Voice
    float commandConfidence = 0.0f;  // 0-1
    bool twoMicrophones = false;     // both mics: better in noise, much more CPU
    std::vector<std::pair<VoiceCommand, std::string>> extraPhrases;

    // Camera
    bool faceTracking = true;
    bool cameraMirrored = false;   // correct for the CoreS3's camera (confirmed on hardware)
    float faceGaze = 1.4f;         // 0-3
    bool cameraPreview = false;    // show the camera image instead of the face (diagnostic)
    // "fast" MSR+MNP (~70 ms, ~14 checks/s) vs "accurate" ESPDet-Pico (~500 ms
    // on this board). On hardware the fast one followed a face better overall,
    // and the accurate one slowed the face animation (shared PSRAM bandwidth).
    bool accurateFaceModel = false;
    bool photoFlash = false;       // light the screen up white while a photo is taken

    // Proximity sensor
    bool proximity = true;
    int proximitySensitivity = 25; // counts above the empty-desk level; lower = more sensitive

    // Presence radar (LD2410 on Port C)
    bool radar = true;             // used when one is plugged in; off ignores it
    int radarDistanceCm = 150;     // how close counts as "at the desk"

    // Motion
    bool flipTiltX = false;
    bool flipTiltY = false;

    // Talking with the AI, through Home Assistant's voice assistant
    std::string wifiName;            // the home network: tried first
    std::string wifiPassword;
    // More networks (a phone's hotspot, a friend's Wi-Fi), tried in order when
    // the ones before them aren't in range.
    static constexpr int kExtraWifi = 2;
    std::string wifiNameN[kExtraWifi];      // wifi_name_2, wifi_name_3
    std::string wifiPasswordN[kExtraWifi];  // wifi_password_2, wifi_password_3
    std::string haUrl;               // http://192.168.1.20:8123 (on the home network)
    std::string haUrlAway;           // used on any other network, e.g. a Tailscale Funnel https address ("" = ha_url)
    std::string haToken;             // long-lived access token: never logged
    std::string haPipeline = "Buddy";  // the voice assistant's name in Home Assistant
    bool conversation = true;        // off: only the built-in commands, even with Wi-Fi set up
    bool followUp = true;            // keep listening after an answer, without "Hey Buddy"
    bool memory = true;              // "remember that ...": notes in memories.txt, given to the AI
    int welcomeBackMinutes = 20;     // say hello out loud when you come back after this long away (0 = off)

    // "What do you see?": photos go straight to a vision model in Ollama
    std::string visionUrl;           // http://192.168.4.98:11434 ("" = no vision)
    std::string visionModel = "gemma3:4b";

    // Every network to try, in order (empty names left out). The first is home.
    std::vector<std::pair<std::string, std::string>> wifiNetworks() const {
        std::vector<std::pair<std::string, std::string>> out;
        if (!wifiName.empty()) out.push_back({wifiName, wifiPassword});
        for (int i = 0; i < kExtraWifi; ++i)
            if (!wifiNameN[i].empty()) out.push_back({wifiNameN[i], wifiPasswordN[i]});
        return out;
    }
    bool aiConfigured() const { return conversation && !wifiNetworks().empty() && !haUrl.empty() && !haToken.empty(); }
};

// Reads settings.txt contents into `cfg` (starting from whatever it holds,
// normally the defaults). Problems are reported as human-readable warnings
// ("line 4: volume must be 0-100") and that line is skipped.
void parseConfig(const std::string& text, BuddyConfig& cfg, std::vector<std::string>& warnings);

// The commented settings.txt written to a fresh card, showing every option at
// its default value.
std::string defaultConfigText();

// For a settings.txt written by an older version: every setting it doesn't
// mention yet, at its default, to append to the file (whole sections with
// their explanations, or single lines under their section's heading). ""
// if nothing is missing. A commented-out setting counts as present.
std::string configUpgradeText(const std::string& existing);

// "say.good_night" style key for a command, and the reverse lookup.
std::string commandKey(VoiceCommand c);
bool commandFromKey(const std::string& key, VoiceCommand& out);

// settings.txt `text` with `key` set to `value`: the first line that sets it
// is changed in place (its comment and alignment kept), or a line is added
// at the end if the file doesn't set it. For settings changed by voice.
std::string configSetValue(const std::string& text, const std::string& key, const std::string& value);
