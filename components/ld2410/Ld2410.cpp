#include "Ld2410.h"

namespace {
const uint8_t kHead[4] = {0xF4, 0xF3, 0xF2, 0xF1};
const uint8_t kTail[4] = {0xF8, 0xF7, 0xF6, 0xF5};
}  // namespace

uint16_t RadarReading::nearestCm() const {
    const bool moving = state == Moving || state == Both;
    const bool still = state == Still || state == Both;
    if (moving && still) return movingCm < stillCm ? movingCm : stillCm;
    if (moving) return movingCm;
    if (still) return stillCm;
    return 0;
}

bool Ld2410Parser::feed(uint8_t b) {
    // Header: resynchronize on any mismatch.
    if (len_ < 4) {
        if (b == kHead[len_]) {
            buf_[len_++] = b;
        } else {
            len_ = (b == kHead[0]) ? 1 : 0;
            if (len_) buf_[0] = b;
        }
        return false;
    }
    buf_[len_++] = b;
    if (len_ == 6) {
        const size_t payload = buf_[4] | (buf_[5] << 8);
        expected_ = 6 + payload + 4;
        if (payload < 13 || expected_ > sizeof buf_) {  // not a data frame we read
            len_ = 0;
            return false;
        }
    }
    if (len_ >= 6 && len_ == expected_) {
        const bool ok = finish();
        len_ = 0;
        return ok;
    }
    return false;
}

size_t Ld2410Parser::feed(const uint8_t* data, size_t len) {
    size_t readings = 0;
    for (size_t i = 0; i < len; ++i) readings += feed(data[i]) ? 1 : 0;
    return readings;
}

bool Ld2410Parser::finish() {
    for (int i = 0; i < 4; ++i)
        if (buf_[expected_ - 4 + i] != kTail[i]) return false;
    const uint8_t* p = buf_ + 6;
    const size_t payload = expected_ - 10;
    if ((p[0] != 0x01 && p[0] != 0x02) || p[1] != 0xAA) return false;
    if (p[payload - 2] != 0x55) return false;
    RadarReading r;
    r.state = p[2] <= 3 ? p[2] : (uint8_t)RadarReading::None;
    r.movingCm = p[3] | (p[4] << 8);
    r.movingEnergy = p[5];
    r.stillCm = p[6] | (p[7] << 8);
    r.stillEnergy = p[8];
    latest_ = r;
    ++frames_;
    return true;
}
