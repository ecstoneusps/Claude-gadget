#include "BuddyConfig.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

std::string lineWarning(int line, const std::string& msg) {
    return "line " + std::to_string(line) + ": " + msg;
}

bool parseBool(const std::string& v, bool& out) {
    const std::string s = lower(v);
    if (s == "on" || s == "true" || s == "yes" || s == "1") { out = true; return true; }
    if (s == "off" || s == "false" || s == "no" || s == "0") { out = false; return true; }
    return false;
}

bool parseInt(const std::string& v, int lo, int hi, int& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    long n = strtol(v.c_str(), &end, 10);
    if (*end != '\0' || n < lo || n > hi) return false;
    out = (int)n;
    return true;
}

bool parseFloat(const std::string& v, float lo, float hi, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    float f = strtof(v.c_str(), &end);
    if (*end != '\0' || !(f >= lo && f <= hi)) return false;
    out = f;
    return true;
}

// Speech phrases: lowercase letters and single spaces, 2-63 characters
// (what the recognizer accepts). Returns false if anything else is in there.
bool normalizePhrase(const std::string& in, std::string& out) {
    out.clear();
    for (char ch : lower(trim(in))) {
        if (ch >= 'a' && ch <= 'z') {
            out += ch;
        } else if (isspace((unsigned char)ch)) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            return false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.size() >= 2 && out.size() <= 63;
}

}  // namespace

std::string commandKey(VoiceCommand c) {
    std::string k = commandName(c);
    for (auto& ch : k) if (ch == ' ') ch = '_';
    return "say." + k;
}

bool commandFromKey(const std::string& key, VoiceCommand& out) {
    for (int i = 1; i < (int)VoiceCommand::Count; ++i) {
        if (commandKey((VoiceCommand)i) == key) {
            out = (VoiceCommand)i;
            return true;
        }
    }
    return false;
}

void parseConfig(const std::string& text, BuddyConfig& cfg, std::vector<std::string>& warnings) {
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;

        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warnings.push_back(lineWarning(lineNo, "expected \"name = value\""));
            continue;
        }
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string val = trim(line.substr(eq + 1));
        auto bad = [&](const char* what) { warnings.push_back(lineWarning(lineNo, key + " " + what)); };

        if (key == "volume") {
            if (!parseInt(val, 0, 100, cfg.volume)) bad("must be 0-100");
        } else if (key == "brightness") {
            if (!parseInt(val, 0, 100, cfg.brightness)) bad("must be 0-100");
        } else if (key == "dim_brightness") {
            if (!parseInt(val, 0, 100, cfg.dimBrightness)) bad("must be 0-100");
        } else if (key == "sleep_after_seconds") {
            if (!parseInt(val, 5, 86400, cfg.sleepAfterSeconds)) bad("must be 5-86400");
        } else if (key == "dim_after_seconds") {
            if (!parseInt(val, 5, 86400, cfg.dimAfterSeconds)) bad("must be 5-86400");
        } else if (key == "sleep_after_leaving_seconds") {
            if (!parseInt(val, 1, 86400, cfg.sleepAfterLeavingSeconds)) bad("must be 1-86400");
        } else if (key == "command_confidence") {
            if (!parseFloat(val, 0.0f, 1.0f, cfg.commandConfidence)) bad("must be 0-1");
        } else if (key == "face_tracking") {
            if (!parseBool(val, cfg.faceTracking)) bad("must be on or off");
        } else if (key == "camera_mirrored") {
            if (!parseBool(val, cfg.cameraMirrored)) bad("must be on or off");
        } else if (key == "face_gaze") {
            if (!parseFloat(val, 0.0f, 3.0f, cfg.faceGaze)) bad("must be 0-3");
        } else if (key == "proximity") {
            if (!parseBool(val, cfg.proximity)) bad("must be on or off");
        } else if (key == "proximity_sensitivity") {
            if (!parseInt(val, 1, 2000, cfg.proximitySensitivity)) bad("must be 1-2000");
        } else if (key == "flip_tilt_x") {
            if (!parseBool(val, cfg.flipTiltX)) bad("must be on or off");
        } else if (key == "flip_tilt_y") {
            if (!parseBool(val, cfg.flipTiltY)) bad("must be on or off");
        } else if (key.rfind("say.", 0) == 0) {
            VoiceCommand cmd;
            if (!commandFromKey(key, cmd)) {
                bad("is not a known command");
                continue;
            }
            size_t start = 0;
            while (start <= val.size()) {
                size_t comma = val.find(',', start);
                if (comma == std::string::npos) comma = val.size();
                const std::string raw = trim(val.substr(start, comma - start));
                start = comma + 1;
                if (raw.empty()) continue;
                std::string phrase;
                if (normalizePhrase(raw, phrase)) {
                    cfg.extraPhrases.emplace_back(cmd, phrase);
                } else {
                    warnings.push_back(lineWarning(lineNo, "\"" + raw +
                        "\" can only use letters and spaces (2-63 characters)"));
                }
            }
        } else {
            warnings.push_back(lineWarning(lineNo, "unknown setting \"" + key + "\""));
        }
    }
}

std::string defaultConfigText() {
    const BuddyConfig d;
    char buf[160];
    std::string t;
    auto add = [&](const char* line) {
        std::string l = line;
        while (!l.empty() && l.back() == ' ') l.pop_back();
        t += l;
        t += '\n';
    };
    auto num = [&](const char* key, int v, const char* help) {
        snprintf(buf, sizeof buf, "%-28s = %-6d # %s", key, v, help);
        add(buf);
    };
    auto flt = [&](const char* key, float v, const char* help) {
        snprintf(buf, sizeof buf, "%-28s = %-6.2g # %s", key, v, help);
        add(buf);
    };
    auto onoff = [&](const char* key, bool v, const char* help) {
        snprintf(buf, sizeof buf, "%-28s = %-6s # %s", key, v ? "on" : "off", help);
        add(buf);
    };

    add("# Desk Buddy settings");
    add("#");
    add("# Edit this file on your computer, put the card back in, and restart the");
    add("# buddy (or unplug it and plug it back in). Anything after # is a comment.");
    add("# Delete a line, or the whole file, to go back to the built-in default.");
    add("# Mistakes are shown in the serial log and that line is ignored.");
    add("");
    add("# --- Sound and screen ---");
    num("volume", d.volume, "speaker volume, 0-100 (\"be quiet\" still mutes it)");
    num("brightness", d.brightness, "screen brightness, 0-100");
    num("dim_brightness", d.dimBrightness, "brightness while dozing, 0-100");
    add("");
    add("# --- Sleepiness ---");
    num("sleep_after_seconds", d.sleepAfterSeconds, "doze off after this long with nothing happening");
    num("dim_after_seconds", d.dimAfterSeconds, "dim the screen after this long");
    num("sleep_after_leaving_seconds", d.sleepAfterLeavingSeconds, "doze sooner once you walk away");
    add("");
    add("# --- Voice ---");
    flt("command_confidence", d.commandConfidence, "0-1; raise to 0.3 or so if it reacts to things you didn't say");
    add("");
    add("# Extra ways to say a command, separated by commas. Letters and spaces only.");
    add("# Remove the # in front of a line to use it. Commands you can add to:");
    std::string keys = "#  ";
    for (int i = 1; i < (int)VoiceCommand::Count; ++i) {
        std::string k = commandKey((VoiceCommand)i);
        if (keys.size() + k.size() > 76) {
            add(keys.c_str());
            keys = "#  ";
        }
        keys += k + "  ";
    }
    add(keys.c_str());
    add("# say.hello = howdy, whats up buddy");
    add("# say.good_night = nighty night");
    add("");
    add("# --- Camera ---");
    onoff("face_tracking", d.faceTracking, "eyes follow your face");
    onoff("camera_mirrored", d.cameraMirrored, "turn on if the eyes look away from you instead of at you");
    flt("face_gaze", d.faceGaze, "how far the eyes turn toward you, 0-3");
    add("");
    add("# --- Proximity sensor (above the screen) ---");
    onoff("proximity", d.proximity, "wake up when you sit down");
    num("proximity_sensitivity", d.proximitySensitivity, "lower notices you from further away (but more false alarms)");
    add("");
    add("# --- Motion ---");
    onoff("flip_tilt_x", d.flipTiltX, "turn on if tilting left/right makes the eyes go the wrong way");
    onoff("flip_tilt_y", d.flipTiltY, "same for tilting forward/back");
    return t;
}
