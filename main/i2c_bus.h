#pragma once
#include "driver/i2c_master.h"

// The CoreS3's internal I2C bus (SDA 12, SCL 11, port 1) carries the power
// chip, touch, IMU, proximity sensor, camera control and both audio chips.
// M5Unified talks to it through M5GFX; the camera driver and esp_codec_dev
// need an ESP-IDF bus handle. This opens one ESP-IDF bus on the port and lets
// M5GFX re-attach to it as a sharer (it detects the open port).
//
// Call from app_main before other tasks start. Afterwards, use the bus from
// one task at a time (the UI task), as M5GFX's sharing isn't serialized.
i2c_master_bus_handle_t sharedI2cBus();
