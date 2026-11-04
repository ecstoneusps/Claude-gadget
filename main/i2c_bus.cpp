#include "i2c_bus.h"

#include <M5Unified.h>
#include "esp_log.h"

static const char* TAG = "i2c_bus";
static constexpr i2c_port_num_t kPort = I2C_NUM_1;  // M5Unified's internal bus on the S3

i2c_master_bus_handle_t sharedI2cBus() {
    static i2c_master_bus_handle_t bus = nullptr;
    if (bus) return bus;

    M5.In_I2C.release();  // hand the port over
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = kPort;
    cfg.sda_io_num = GPIO_NUM_12;
    cfg.scl_io_num = GPIO_NUM_11;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    M5.In_I2C.begin();    // M5GFX sees the open port and shares it
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not open the internal I2C bus (%s)", esp_err_to_name(err));
        bus = nullptr;
    }
    return bus;
}
