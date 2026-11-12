#pragma once
#include <stdint.h>
#include <string>

// Buddy as a device in Home Assistant, over MQTT (Mosquitto): his sensors
// show up as entities on their own (MQTT discovery), and Home Assistant can
// change his volume, make him say something, take a photo... Home network only.

struct HassConfig {
    std::string url;       // mqtt://192.168.4.97:1883
    std::string user;
    std::string password;  // never logged
    std::string version;   // firmware version, for the device page
};
bool hassStart(const HassConfig& cfg);  // after Wi-Fi has started; false if not set up
bool hassConnected();

// What he senses and how he is, from the UI task (a few times a second is
// fine: only changes are sent, plus everything once a minute).
struct HassState {
    bool atDesk = false;
    bool haveRadar = false;
    int distanceCm = -1;            // -1: unknown
    const char* movement = "none";  // none, moving, still, both
    int movingEnergy = 0, stillEnergy = 0;
    float lux = -1;                 // -1: unknown
    float soundDb = -100;
    bool talking = false;
    const char* mood = "neutral";
    bool awake = true;
    int volume = 0, brightness = 0;
    bool muted = false;
    int battery = 0;
    int usbMv = 0;
};
void hassUpdate(const HassState& s, uint32_t now);

// Something happened: "tapped", "petted", "shaken", "wake_word".
void hassEvent(const char* type, uint32_t now);

// Commands from Home Assistant, for the UI task to carry out.
struct HassCommand {
    enum Type { Volume, Brightness, Mute, Say, Photo, Sleep, Wake } type;
    int value = 0;      // Volume/Brightness: 0-100; Mute: 0/1
    std::string text;   // Say
};
bool hassTakeCommand(HassCommand& out);
