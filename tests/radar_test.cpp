// Native unit tests for the LD2410 radar parser and radar presence. Run: tests/run_tests.sh
#include <stdio.h>
#include <algorithm>
#include <vector>

#include "Ld2410.h"
#include "Presence.h"

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                 \
        }                                                               \
    } while (0)

// A basic-mode data frame as the radar sends it.
static std::vector<uint8_t> frame(uint8_t state, uint16_t movingCm, uint8_t movingE, uint16_t stillCm, uint8_t stillE,
                                  bool engineering = false) {
    std::vector<uint8_t> payload = {uint8_t(engineering ? 0x01 : 0x02), 0xAA, state,
                                    uint8_t(movingCm), uint8_t(movingCm >> 8), movingE,
                                    uint8_t(stillCm), uint8_t(stillCm >> 8), stillE,
                                    0x2C, 0x01};  // detection distance 300 cm
    if (engineering) payload.insert(payload.end(), {8, 8, 10, 20, 30, 40, 50, 60, 70, 80, 90,
                                                    15, 25, 35, 45, 55, 65, 75, 85, 95, 30, 1});
    payload.insert(payload.end(), {0x55, 0x00});
    std::vector<uint8_t> f = {0xF4, 0xF3, 0xF2, 0xF1, uint8_t(payload.size()), uint8_t(payload.size() >> 8)};
    f.insert(f.end(), payload.begin(), payload.end());
    f.insert(f.end(), {0xF8, 0xF7, 0xF6, 0xF5});
    return f;
}

static void test_parse_basic_frame() {
    Ld2410Parser p;
    const auto f = frame(RadarReading::Still, 0, 0, 82, 51);
    CHECK(p.feed(f.data(), f.size()) == 1);
    CHECK(p.latest().state == RadarReading::Still);
    CHECK(p.latest().stillCm == 82 && p.latest().stillEnergy == 51);
    CHECK(p.latest().someone());
    CHECK(p.latest().nearestCm() == 82);
}

static void test_parse_engineering_and_both() {
    Ld2410Parser p;
    const auto f = frame(RadarReading::Both, 140, 60, 95, 40, true);
    CHECK(p.feed(f.data(), f.size()) == 1);
    CHECK(p.latest().state == RadarReading::Both);
    CHECK(p.latest().nearestCm() == 95);  // the nearer of the two targets
}

static void test_resync_after_garbage_and_split_reads() {
    Ld2410Parser p;
    std::vector<uint8_t> stream = {0x00, 0xF4, 0xF3, 0x12, 0xF4, 0xFF};  // junk and a false start
    const auto a = frame(RadarReading::Moving, 210, 77, 0, 0);
    const auto b = frame(RadarReading::None, 0, 0, 0, 0);
    stream.insert(stream.end(), a.begin(), a.end());
    stream.insert(stream.end(), b.begin(), b.end());
    // Delivered a few bytes at a time, as the UART does.
    size_t readings = 0;
    for (size_t i = 0; i < stream.size(); i += 5)
        readings += p.feed(stream.data() + i, std::min<size_t>(5, stream.size() - i));
    CHECK(readings == 2);
    CHECK(p.frames() == 2);
    CHECK(p.latest().state == RadarReading::None && !p.latest().someone());
}

static void test_rejects_corrupt_frames() {
    Ld2410Parser p;
    auto f = frame(RadarReading::Still, 0, 0, 82, 51);
    f[f.size() - 1] = 0x00;  // broken tail
    CHECK(p.feed(f.data(), f.size()) == 0);
    auto g = frame(RadarReading::Still, 0, 0, 82, 51);
    g[7] = 0xAB;             // wrong head marker
    CHECK(p.feed(g.data(), g.size()) == 0);
    // A command reply (FD FC FB FA ... 04 03 02 01) is ignored.
    const uint8_t ack[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    CHECK(p.feed(ack, sizeof ack) == 0);
    // ...and a good frame right after still parses.
    const auto h = frame(RadarReading::Moving, 120, 50, 0, 0);
    CHECK(p.feed(h.data(), h.size()) == 1);
}

static void test_radar_presence() {
    RadarPresence rp;
    rp.maxDistanceCm = 150;
    uint32_t t = 1000;
    // Someone across the room (300 cm) doesn't count as at the desk.
    for (int i = 0; i < 30; ++i, t += 100) CHECK(rp.update(true, 300, t) == RadarPresence::Event::None);
    CHECK(!rp.present());
    // Sitting down at 80 cm: arrives after ~0.8 s.
    RadarPresence::Event e = RadarPresence::Event::None;
    for (int i = 0; i < 12 && e == RadarPresence::Event::None; ++i, t += 100) e = rp.update(true, 80, t);
    CHECK(e == RadarPresence::Event::Arrived);
    CHECK(rp.present());
    // Brief dropouts (the radar blinks) don't count as leaving.
    for (int i = 0; i < 30; ++i, t += 100) {
        CHECK(rp.update(i % 10 != 0, 80, t) == RadarPresence::Event::None);
    }
    CHECK(rp.present());
    // Gone for 8 s: left.
    e = RadarPresence::Event::None;
    int steps = 0;
    for (; steps < 100 && e == RadarPresence::Event::None; ++steps, t += 100) e = rp.update(false, 0, t);
    CHECK(e == RadarPresence::Event::Left);
    CHECK(steps >= 80 && steps <= 82);
    CHECK(!rp.present());
}

int main() {
    test_parse_basic_frame();
    test_parse_engineering_and_both();
    test_resync_after_garbage_and_split_reads();
    test_rejects_corrupt_frames();
    test_radar_presence();
    if (failures) {
        printf("radar tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("radar tests: all passed\n");
    return 0;
}
