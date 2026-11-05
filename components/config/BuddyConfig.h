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
    bool cameraMirrored = false;
    float faceGaze = 1.4f;         // 0-3
    bool cameraPreview = false;    // show the camera image instead of the face (diagnostic)

    // Proximity sensor
    bool proximity = true;
    int proximitySensitivity = 25; // counts above the empty-desk level; lower = more sensitive

    // Motion
    bool flipTiltX = false;
    bool flipTiltY = false;
};

// Reads settings.txt contents into `cfg` (starting from whatever it holds,
// normally the defaults). Problems are reported as human-readable warnings
// ("line 4: volume must be 0-100") and that line is skipped.
void parseConfig(const std::string& text, BuddyConfig& cfg, std::vector<std::string>& warnings);

// The commented settings.txt written to a fresh card, showing every option at
// its default value.
std::string defaultConfigText();

// "say.good_night" style key for a command, and the reverse lookup.
std::string commandKey(VoiceCommand c);
bool commandFromKey(const std::string& key, VoiceCommand& out);
