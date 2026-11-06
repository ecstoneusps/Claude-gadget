#pragma once
#include <stddef.h>
#include <stdint.h>

// HLK-LD2410 / LD2410B / LD2410C 24 GHz presence radar: parser for the data
// frames it streams over its serial port (256000 baud by default, about 10
// per second). Portable, so it is unit-tested on a PC.
//
// Frame: F4 F3 F2 F1, length (2 bytes, little-endian), payload, F8 F7 F6 F5.
// Payload: type (0x02 basic, 0x01 engineering), 0xAA, target state,
// moving distance (cm, 2 bytes), moving energy, still distance (cm, 2 bytes),
// still energy, detection distance (cm, 2 bytes), [engineering data], 0x55, 0x00.
struct RadarReading {
    enum State : uint8_t { None = 0, Moving = 1, Still = 2, Both = 3 };
    uint8_t state = None;
    uint16_t movingCm = 0;
    uint8_t movingEnergy = 0;  // 0-100
    uint16_t stillCm = 0;
    uint8_t stillEnergy = 0;   // 0-100

    bool someone() const { return state != None; }
    // Distance to the nearest target it reports (0 if none).
    uint16_t nearestCm() const;
};

class Ld2410Parser {
public:
    // Feed received bytes; returns true each time a complete, valid reading
    // arrives (then latest() holds it). Garbage and command replies are skipped.
    bool feed(uint8_t byte);
    size_t feed(const uint8_t* data, size_t len);  // returns how many readings completed
    const RadarReading& latest() const { return latest_; }
    uint32_t frames() const { return frames_; }

private:
    bool finish();
    uint8_t buf_[64];
    size_t len_ = 0;
    size_t expected_ = 0;  // total frame length once known
    RadarReading latest_;
    uint32_t frames_ = 0;
};
