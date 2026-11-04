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

static const char* TAG = "proximity";

bool proximityBegin() {
    // Strongest settings for the longest range: 60 kHz, 100% duty, 100 mA,
    // 15 pulses, a reading every 50 ms.
    const uint8_t led = (0x03 << 5) | (0x03 << 3) | 0x04;
    bool ok = M5.In_I2C.writeRegister8(kAddr, kRegPsLed, led, kI2cHz) &&
              M5.In_I2C.writeRegister8(kAddr, kRegPsNPulses, 0x0F, kI2cHz) &&
              M5.In_I2C.writeRegister8(kAddr, kRegPsMeasRate, 0x00, kI2cHz) &&
              M5.In_I2C.writeRegister8(kAddr, kRegPsContr, 0x02, kI2cHz);
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
