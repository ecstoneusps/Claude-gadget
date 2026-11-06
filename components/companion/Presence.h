#pragma once
#include <stdint.h>

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
