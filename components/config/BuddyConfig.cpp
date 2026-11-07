#include "BuddyConfig.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>

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
    std::map<std::string, int> seen;  // setting -> line it was first given on
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;

        if (!line.empty() && line.back() == '\r') line.pop_back();  // Windows line endings
        const size_t hash = line.find('#');
        const size_t eq = line.find('=');
        if (eq == std::string::npos || (hash != std::string::npos && hash < eq)) {
            if (!trim(hash == std::string::npos ? line : line.substr(0, hash)).empty())
                warnings.push_back(lineWarning(lineNo, "expected \"name = value\""));
            continue;
        }
        const std::string key = lower(trim(line.substr(0, eq)));
        auto bad = [&](const char* what) { warnings.push_back(lineWarning(lineNo, key + " " + what)); };

        // The value: up to a # comment, or "in quotes" to keep a # or spaces in it.
        std::string val = trim(line.substr(eq + 1));
        if (!val.empty() && val[0] == '"') {
            const size_t close = val.find('"', 1);
            if (close == std::string::npos) {
                bad("is missing its closing quote");
                continue;
            }
            const std::string after = trim(val.substr(close + 1));
            if (!after.empty() && after[0] != '#') {
                bad("has text after the closing quote");
                continue;
            }
            val = val.substr(1, close - 1);
        } else {
            const size_t h = val.find('#');
            if (h != std::string::npos) val = trim(val.substr(0, h));
        }

        // The same setting twice (say.* lines add up, so they're fine). An
        // empty copy doesn't erase a filled-in one: that's usually the
        // template's own line left below the one you added.
        if (key.rfind("say.", 0) != 0) {
            auto it = seen.find(key);
            if (it != seen.end()) {
                const std::string first = std::to_string(it->second);
                if (val.empty()) {
                    warnings.push_back(lineWarning(lineNo, key + " is empty here; using line " + first));
                    continue;
                }
                warnings.push_back(lineWarning(lineNo, key + " was already set on line " + first + "; using this one"));
            } else if (!val.empty()) {
                seen[key] = lineNo;
            }
        }

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
        } else if (key == "two_microphones") {
            if (!parseBool(val, cfg.twoMicrophones)) bad("must be on or off");
        } else if (key == "face_tracking") {
            if (!parseBool(val, cfg.faceTracking)) bad("must be on or off");
        } else if (key == "camera_mirrored") {
            if (!parseBool(val, cfg.cameraMirrored)) bad("must be on or off");
        } else if (key == "face_model") {
            const std::string v = lower(val);
            if (v == "accurate") cfg.accurateFaceModel = true;
            else if (v == "fast") cfg.accurateFaceModel = false;
            else bad("must be accurate or fast");
        } else if (key == "camera_preview") {
            if (!parseBool(val, cfg.cameraPreview)) bad("must be on or off");
        } else if (key == "face_gaze") {
            if (!parseFloat(val, 0.0f, 3.0f, cfg.faceGaze)) bad("must be 0-3");
        } else if (key == "proximity") {
            if (!parseBool(val, cfg.proximity)) bad("must be on or off");
        } else if (key == "proximity_sensitivity") {
            if (!parseInt(val, 1, 2000, cfg.proximitySensitivity)) bad("must be 1-2000");
        } else if (key == "radar") {
            if (!parseBool(val, cfg.radar)) bad("must be on or off");
        } else if (key == "radar_distance_cm") {
            if (!parseInt(val, 30, 600, cfg.radarDistanceCm)) bad("must be 30-600");
        } else if (key == "flip_tilt_x") {
            if (!parseBool(val, cfg.flipTiltX)) bad("must be on or off");
        } else if (key == "flip_tilt_y") {
            if (!parseBool(val, cfg.flipTiltY)) bad("must be on or off");
        } else if (key == "wifi_name") {
            if (val.size() > 32) bad("can be at most 32 characters");
            else cfg.wifiName = val;
        } else if (key == "wifi_password") {
            if (!val.empty() && (val.size() < 8 || val.size() > 63)) bad("must be 8-63 characters");
            else cfg.wifiPassword = val;
        } else if (key == "ha_url") {
            std::string v = val;
            while (!v.empty() && v.back() == '/') v.pop_back();
            const std::string lv = lower(v);
            if (!v.empty() && lv.rfind("http://", 0) != 0 && lv.rfind("https://", 0) != 0)
                bad("must start with http:// or https:// (e.g. http://192.168.1.20:8123)");
            else cfg.haUrl = v;
        } else if (key == "ha_token") {
            if (val.find(' ') != std::string::npos) bad("must not contain spaces");
            else cfg.haToken = val;
        } else if (key == "ha_pipeline") {
            cfg.haPipeline = val;
        } else if (key == "conversation") {
            if (!parseBool(val, cfg.conversation)) bad("must be on or off");
        } else if (key == "vision_url") {
            std::string v = val;
            while (!v.empty() && v.back() == '/') v.pop_back();
            const std::string lv = lower(v);
            if (!v.empty() && lv.rfind("http://", 0) != 0 && lv.rfind("https://", 0) != 0)
                bad("must start with http:// (e.g. http://192.168.1.30:11434)");
            else cfg.visionUrl = v;
        } else if (key == "vision_model") {
            if (val.empty() || val.find(' ') != std::string::npos) bad("must be a model name like gemma3:4b");
            else cfg.visionModel = val;
        } else if (key == "follow_up") {
            if (!parseBool(val, cfg.followUp)) bad("must be on or off");
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

namespace {
// The "talking with the AI" section, also appended to older settings files.
void addAiSection(std::string& t) {
    const BuddyConfig d;
    char buf[200];
    auto add = [&](const char* line) {
        std::string l = line;
        while (!l.empty() && l.back() == ' ') l.pop_back();
        t += l;
        t += '\n';
    };
    auto str = [&](const char* key, const std::string& v, const char* help) {
        snprintf(buf, sizeof buf, "%-28s = %-6s # %s", key, v.c_str(), help);
        add(buf);
    };
    auto onoff = [&](const char* key, bool v, const char* help) {
        snprintf(buf, sizeof buf, "%-28s = %-6s # %s", key, v ? "on" : "off", help);
        add(buf);
    };
    add("# --- Talking with the AI (through Home Assistant) ---");
    add("# Fill in all four to talk to Buddy's AI. Put \"quotes\" around the password");
    add("# if it has a # in it. Keep this card private: the token is a password.");
    str("wifi_name", d.wifiName, "your Wi-Fi network (2.4 GHz)");
    str("wifi_password", d.wifiPassword, "its password");
    str("ha_url", d.haUrl, "Home Assistant's address, e.g. http://192.168.1.20:8123");
    str("ha_token", d.haToken, "long-lived access token (from the Desk Buddy user)");
    str("ha_pipeline", d.haPipeline, "name of the voice assistant in Home Assistant");
    onoff("conversation", d.conversation, "off = only the built-in commands");
    onoff("follow_up", d.followUp, "keep listening after an answer, no \"Hey Buddy\" needed");
    add("");
    add("# --- Seeing (\"what do you see?\", \"what am I holding?\") ---");
    add("# Photos go straight to a vision model in Ollama on your PC; the answer is");
    add("# spoken through Home Assistant. Install it first: ollama pull gemma3:4b");
    str("vision_url", d.visionUrl, "Ollama's address, e.g. http://192.168.1.30:11434");
    str("vision_model", d.visionModel, "a vision model you've pulled in Ollama");
}
}  // namespace

namespace {
// The setting a line is about: "volume = 40" and "# volume = 40" both give
// "volume" (a commented-out setting counts as present: you chose the default).
// "" for anything else, including the say.* examples.
std::string settingKeyOf(const std::string& rawLine) {
    std::string line = trim(rawLine);
    while (!line.empty() && line[0] == '#') line = trim(line.substr(1));
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return "";
    const std::string key = lower(trim(line.substr(0, eq)));
    if (key.empty() || key.rfind("say.", 0) == 0) return "";
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return "";
    return key;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string l = text.substr(pos, nl - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        pos = nl + 1;
    }
    return lines;
}
}  // namespace

std::string configUpgradeText(const std::string& existing) {
    std::vector<std::string> have;
    for (const auto& l : splitLines(existing)) {
        const std::string k = settingKeyOf(l);
        if (!k.empty()) have.push_back(k);
    }
    auto present = [&](const std::string& k) {
        for (const auto& h : have) if (h == k) return true;
        return false;
    };

    // Walk the default file section by section ("# --- Camera ---"). A section
    // missing entirely is added whole, with its explanations; otherwise just
    // the missing lines, under the section's heading.
    std::string out;
    std::vector<std::string> section;
    auto flush = [&]() {
        if (section.empty()) return;
        std::vector<std::string> missing;
        int settings = 0;
        for (const auto& l : section) {
            if (trim(l).rfind("#", 0) == 0) continue;  // only real setting lines count
            const std::string k = settingKeyOf(l);
            if (k.empty()) continue;
            ++settings;
            if (!present(k)) missing.push_back(l);
        }
        if (!missing.empty()) {
            out += '\n';
            if ((int)missing.size() == settings) {
                for (const auto& l : section) {
                    out += l;
                    out += '\n';
                }
                while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
            } else {
                out += section[0] + " (added by an update)\n";
                for (const auto& l : missing) {
                    out += l;
                    out += '\n';
                }
            }
        }
        section.clear();
    };
    for (const auto& l : splitLines(defaultConfigText())) {
        if (l.rfind("# --- ", 0) == 0) flush();
        if (l.rfind("# --- ", 0) == 0 || !section.empty()) section.push_back(l);
    }
    flush();
    if (out.empty()) return "";
    if (!existing.empty() && existing.back() != '\n') out.insert(out.begin(), '\n');
    return out;
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
    onoff("two_microphones", d.twoMicrophones, "both mics: hears you better over noise, but slows everything else down");
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
    onoff("camera_mirrored", d.cameraMirrored, "flip (on/off) if the eyes look away from you instead of at you");
    flt("face_gaze", d.faceGaze, "how far the eyes turn toward you, 0-3");
    snprintf(buf, sizeof buf, "%-28s = %-6s # %s", "face_model", d.accurateFaceModel ? "accurate" : "fast",
             "fast checks ~14 times a second; accurate finds you more often per check but is ~7x slower");
    add(buf);
    onoff("camera_preview", d.cameraPreview, "show what the camera sees (green box = face found) instead of the face");
    add("");
    add("# --- Proximity sensor (above the screen) ---");
    onoff("proximity", d.proximity, "wake up when you sit down");
    num("proximity_sensitivity", d.proximitySensitivity, "lower notices you from further away (but more false alarms)");
    add("");
    add("# --- Presence radar (LD2410, plugged into Port C) ---");
    onoff("radar", d.radar, "use the radar when one is plugged in (instead of the proximity sensor)");
    num("radar_distance_cm", d.radarDistanceCm, "how close (cm) counts as sitting at the desk, 30-600");
    add("");
    add("# --- Motion ---");
    onoff("flip_tilt_x", d.flipTiltX, "turn on if tilting left/right makes the eyes go the wrong way");
    onoff("flip_tilt_y", d.flipTiltY, "same for tilting forward/back");
    add("");
    addAiSection(t);
    return t;
}

std::string configSetValue(const std::string& text, const std::string& key, const std::string& value) {
    const std::string want = lower(key);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        const size_t first = line.find_first_not_of(" \t");
        const size_t eq = line.find('=');
        const size_t hash = line.find('#');
        const bool setsIt = first != std::string::npos && line[first] != '#' && eq != std::string::npos &&
                            (hash == std::string::npos || eq < hash) &&
                            lower(trim(line.substr(0, eq))) == want;
        if (setsIt) {
            // Value: after "= " up to the comment (or the end, minus a \r).
            size_t valStart = line.find_first_not_of(' ', eq + 1);
            if (valStart == std::string::npos || (hash != std::string::npos && valStart >= hash)) valStart = eq + 2;
            size_t valEnd = hash != std::string::npos ? hash : line.size();
            if (hash == std::string::npos && !line.empty() && line.back() == '\r') --valEnd;
            std::string newLine = line.substr(0, eq) + "= ";
            if (valStart > eq + 2 && valStart <= line.size()) newLine = line.substr(0, valStart);
            newLine += value;
            if (hash != std::string::npos) {
                // Keep the comment where it was if there's room, else one space before it.
                size_t col = hash;
                newLine += newLine.size() < col ? std::string(col - newLine.size(), ' ') : std::string(" ");
                newLine += line.substr(hash);
            } else if (!line.empty() && line.back() == '\r') {
                newLine += '\r';
            }
            return text.substr(0, pos) + newLine + text.substr(nl);
        }
        pos = nl + 1;
    }
    std::string out = text;
    if (!out.empty() && out.back() != '\n') out += '\n';
    return out + key + " = " + value + "\n";
}
