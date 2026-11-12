#include "proximity.h"

#include <M5Unified.h>
#include "esp_log.h"

// Register map and bit layout follow M5Stack's LTR5XX driver for the CoreS3
// (github.com/m5stack/M5CoreS3, src/utility/LTR5XX.*), LTR-55x variant.
static constexpr uint8_t kAddr = 0x23;
static constexpr uint32_t kI2cHz = 100000;

static constexpr uint8_t kRegPsContr = 0x81;     // bit1: PS active
static constexpr uint8_t kRegPsLed = 0x82;       // [7:5] pulse freq, [4:3] duty, [2:0] peak current
static constexpr uint8_t kRegPsNPulses = 0x83;   // [3:0] LED pulses per measurement
static constexpr uint8_t kRegPsMeasRate = 0x84;  // [3:0] measurement period
static constexpr uint8_t kRegPsData = 0x8D;      // low byte; 0x8E [2:0] high bits
static constexpr uint8_t kRegAlsContr = 0x80;    // bit0: ALS active; [4:2] gain (0 = 1x)
static constexpr uint8_t kRegAlsMeasRate = 0x85; // [5:3] integration time, [2:0] repeat rate
static constexpr uint8_t kRegAlsData = 0x88;     // CH1 low, CH1 high, CH0 low, CH0 high

static const char* TAG = "proximity";

bool proximityBegin() {
    // Strongest settings for the longest range: 60 kHz, 100% duty, 100 mA,
    // 15 pulses, a reading every 50 ms.
    const uint8_t led = (0x03 << 5) | (0x03 << 3) | 0x04;
    bool ok = M5.In_I2C.writeRegister8(kAddr, kRegPsLed, led, kI2cHz) &&
              M5.In_I2C.writeRegister8(kAddr, kRegPsNPulses, 0x0F, kI2cHz) &&
              M5.In_I2C.writeRegister8(kAddr, kRegPsMeasRate, 0x00, kI2cHz) &&
              M5.In_I2C.writeRegister8(kAddr, kRegPsContr, 0x02, kI2cHz);
    // The light sensor too: gain 1x (up to ~64k lux), 100 ms integration, a reading every 500 ms.
    if (ok && !(M5.In_I2C.writeRegister8(kAddr, kRegAlsMeasRate, 0x03, kI2cHz) &&
                M5.In_I2C.writeRegister8(kAddr, kRegAlsContr, 0x01, kI2cHz)))
        ESP_LOGW(TAG, "light sensor didn't start");
    if (!ok) ESP_LOGW(TAG, "LTR-553 not responding; presence detection disabled");
    else ESP_LOGI(TAG, "LTR-553 proximity sensor ready");
    return ok;
}

bool proximityRead(uint16_t* out) {
    uint8_t b[2];
    if (!M5.In_I2C.readRegister(kAddr, kRegPsData, b, 2, kI2cHz)) return false;
    *out = (uint16_t)(((b[1] & 0x07) << 8) | b[0]);
    return true;
}

bool proximityReadLux(float* lux) {
    uint8_t b[4];
    if (!M5.In_I2C.readRegister(kAddr, kRegAlsData, b, 4, kI2cHz)) return false;
    const float ch1 = (float)(b[0] | (b[1] << 8)), ch0 = (float)(b[2] | (b[3] << 8));
    // The datasheet's formula (gain 1x, 100 ms integration).
    const float sum = ch0 + ch1;
    if (sum <= 0) {
        *lux = 0;
        return true;
    }
    const float ratio = ch1 / sum;
    float l;
    if (ratio < 0.45f) l = 1.7743f * ch0 + 1.1059f * ch1;
    else if (ratio < 0.64f) l = 4.2785f * ch0 - 1.9548f * ch1;
    else if (ratio < 0.85f) l = 0.5926f * ch0 + 0.1185f * ch1;
    else l = 0;
    *lux = l < 0 ? 0 : l;
    return true;
}
