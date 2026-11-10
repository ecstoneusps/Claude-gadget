#pragma once
#include <stdint.h>
#include <string>

// Turns raw proximity-sensor readings into "someone arrived" / "they left".
//
// IR proximity sensors are short-range and their idle reading drifts with
// ambient light, temperature and whatever sits on the desk, so instead of a
// fixed threshold this learns the empty-desk baseline and looks for a clear
// rise above it that lasts a moment. Hardware-free; unit-tested on a PC.
class PresenceDetector {
public:
    enum class Event : uint8_t { None, Arrived, Left };

    // Feed one reading (raw sensor counts) taken at time `now` (ms).
    Event update(uint16_t raw, uint32_t now);

    bool present() const { return present_; }
    float baseline() const { return baseline_; }

    // Tunables
    float minRise = 25.0f;        // counts above baseline that count as "close"
    float riseFraction = 0.3f;    // ...or this fraction of the baseline, whichever is larger
    uint32_t arriveMs = 300;      // must stay close this long to arrive
    uint32_t leaveMs = 3000;      // must stay far this long to leave
    float baselineRate = 0.01f;   // how fast the empty-desk baseline adapts (per reading)
    uint32_t relearnAfterMs = 600000;  // after 10 min "present", slowly accept it as the new empty

private:
    bool started_ = false;
    bool present_ = false;
    float baseline_ = 0;
    uint32_t nearSince_ = 0, farSince_ = 0, presentSince_ = 0;
    bool near_ = false;
};

// Someone at the desk, from a presence radar (LD2410) instead of the
// short-range proximity sensor: it notices you sitting still, a few metres
// away. Only people within `maxDistanceCm` count as being at the desk.
class RadarPresence {
public:
    enum class Event { None, Arrived, Left };

    // Each radar reading: is someone there, and how far (cm, nearest target).
    Event update(bool someone, int distanceCm, uint32_t now);
    bool present() const { return present_; }

    int maxDistanceCm = 150;
    uint32_t arriveMs = 800;   // seen this long before counting as arrived
    uint32_t leaveMs = 8000;   // gone this long before counting as left (the radar blinks out briefly)

private:
    bool present_ = false;
    bool near_ = false;
    uint32_t since_ = 0;  // when `near_` last changed
};

// "Welcome back": after you've been away a while, the buddy says hello out
// loud when you return. Tell it when you leave and arrive; on arriving after
// at least `minAwayMs` it hands back what to say (an emotion tag and a short
// phrase, picked by how long you were gone, not the same one twice in a row).
class WelcomeBack {
public:
    void onLeft(uint32_t now);
    // "" if there's nothing to say (not away long enough, or turned off).
    std::string onArrived(uint32_t now);

    uint32_t minAwayMs = 20u * 60u * 1000u;  // 0 = never

private:
    bool away_ = false;
    uint32_t leftAt_ = 0;
    unsigned pick_ = 0;
};
