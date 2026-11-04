#include "Presence.h"

PresenceDetector::Event PresenceDetector::update(uint16_t raw, uint32_t now) {
    const float v = raw;
    if (!started_) {
        started_ = true;
        baseline_ = v;
        farSince_ = now;
        return Event::None;
    }

    float rise = baseline_ * riseFraction;
    if (rise < minRise) rise = minRise;
    const bool isNear = v > baseline_ + rise;

    // Learn the empty-desk level only while nothing is close, so a person
    // sitting there for an hour doesn't become the new "empty".
    if (!isNear && !present_) baseline_ += (v - baseline_) * baselineRate;
    // If "someone" has been there for ages it may really be an object left
    // near the sensor: creep the baseline up so it eventually stops counting.
    else if (present_ && now - presentSince_ > relearnAfterMs) baseline_ += (v - baseline_) * 0.002f;
    // If the reading drops well below the baseline (sensor warmed up, object
    // removed from the desk), follow it down quickly.
    if (v < baseline_ - rise) baseline_ += (v - baseline_) * 0.2f;

    if (isNear != near_) {
        near_ = isNear;
        (isNear ? nearSince_ : farSince_) = now;
    }

    if (!present_ && near_ && now - nearSince_ >= arriveMs) {
        present_ = true;
        presentSince_ = now;
        return Event::Arrived;
    }
    if (present_ && !near_ && now - farSince_ >= leaveMs) {
        present_ = false;
        return Event::Left;
    }
    return Event::None;
}
