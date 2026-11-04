#pragma once
#include <stdint.h>

// LTR-553ALS proximity sensor on the CoreS3 (internal I2C, address 0x23),
// just above the screen. Call from the UI task only (it owns the I2C bus).

bool proximityBegin();             // configure for maximum range; false if not found
bool proximityRead(uint16_t* out); // latest reading, 0..2047 (higher = closer)
