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
