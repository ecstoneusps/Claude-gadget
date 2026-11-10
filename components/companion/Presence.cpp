#include "Presence.h"

PresenceDetector::Event PresenceDetector::update(uint16_t raw, uint32_t now) {
    const float v = raw;
    if (!started_) {
        // The sensor reads 0 until its first measurement is done; learning
        // that as the empty desk made every real reading look like a person.
        if (raw == 0) return Event::None;
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

RadarPresence::Event RadarPresence::update(bool someone, int distanceCm, uint32_t now) {
    const bool near = someone && distanceCm > 0 && distanceCm <= maxDistanceCm;
    if (near != near_) {
        near_ = near;
        since_ = now;
    }
    if (!present_ && near_ && now - since_ >= arriveMs) {
        present_ = true;
        return Event::Arrived;
    }
    if (present_ && !near_ && now - since_ >= leaveMs) {
        present_ = false;
        return Event::Left;
    }
    return Event::None;
}

void WelcomeBack::onLeft(uint32_t now) {
    away_ = true;
    leftAt_ = now;
}

std::string WelcomeBack::onArrived(uint32_t now) {
    if (!away_) return "";
    away_ = false;
    const uint32_t gone = now - leftAt_;
    if (minAwayMs == 0 || gone < minAwayMs) return "";
    static const char* const kShort[] = {
        "[happy] Welcome back!",
        "[happy] Hey, you're back!",
        "[happy] There you are!",
        "[love] Yay, you're back! I missed you.",
    };
    static const char* const kLong[] = {
        "[love] You're back! I missed you so much.",
        "[happy] Welcome back! It was too quiet without you.",
        "[happy] Yay, you're home! I've been waiting for you.",
    };
    static const char* const kAllDay[] = {
        "[love] Hi! It's so good to see you again.",
        "[happy] You're back! I missed you all day.",
        "[happy] There you are! I was wondering when you'd come back.",
    };
    const char* const* list;
    unsigned n;
    if (gone < 2u * 3600u * 1000u) {
        list = kShort;
        n = sizeof kShort / sizeof kShort[0];
    } else if (gone < 8u * 3600u * 1000u) {
        list = kLong;
        n = sizeof kLong / sizeof kLong[0];
    } else {
        list = kAllDay;
        n = sizeof kAllDay / sizeof kAllDay[0];
    }
    return list[pick_++ % n];
}
