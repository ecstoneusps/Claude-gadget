#include "audio_io.h"

#include <M5Unified.h>
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_bus.h"

static const char* TAG = "audio";

// CoreS3 audio wiring (same as M5Unified and xiaozhi-esp32).
static constexpr gpio_num_t kMclk = GPIO_NUM_0;
static constexpr gpio_num_t kBclk = GPIO_NUM_34;
static constexpr gpio_num_t kWs = GPIO_NUM_33;
static constexpr gpio_num_t kDout = GPIO_NUM_13;  // to the speaker amp
static constexpr gpio_num_t kDin = GPIO_NUM_14;   // from the mic ADC
static constexpr uint8_t kAw9523Addr = 0x58;      // I/O expander; P0_2 enables the amp
static constexpr float kMicGainDb = 30.0f;

static esp_codec_dev_handle_t s_in = nullptr, s_out = nullptr;

// Switch on the AW88298 amplifier through the AW9523 I/O expander (P0 bit 2),
// as M5Unified does. Read-modify-write so the other expander pins keep their state.
static void enableSpeakerAmp() {
    uint8_t p0 = M5.In_I2C.readRegister8(kAw9523Addr, 0x02, 400000);
    M5.In_I2C.writeRegister8(kAw9523Addr, 0x02, p0 | 0x04, 400000);
    vTaskDelay(pdMS_TO_TICKS(50));
}

bool audioBegin() {
    i2c_master_bus_handle_t bus = sharedI2cBus();
    if (!bus) return false;
    enableSpeakerAmp();

    // One I2S port, transmit and receive sharing the clocks: true duplex.
    i2s_chan_handle_t tx = nullptr, rx = nullptr;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear_after_cb = true;  // silence when nothing is playing
    if (i2s_new_channel(&chan, &tx, &rx) != ESP_OK) {
        ESP_LOGE(TAG, "no I2S channel");
        return false;
    }
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = kMclk, .bclk = kBclk, .ws = kWs, .dout = kDout, .din = kDin,
            .invert_flags = {false, false, false},
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_channel_init_std_mode(tx, &std) != ESP_OK || i2s_channel_init_std_mode(rx, &std) != ESP_OK ||
        i2s_channel_enable(tx) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed");
        return false;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {};
    i2s_cfg.port = I2S_NUM_0;
    i2s_cfg.rx_handle = rx;
    i2s_cfg.tx_handle = tx;
    const audio_codec_data_if_t* data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.port = I2C_NUM_1;
    i2c_cfg.bus_handle = bus;

    // Speaker: AW88298 amplifier.
    i2c_cfg.addr = AW88298_CODEC_DEFAULT_ADDR;
    aw88298_codec_cfg_t aw = {};
    aw.ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    aw.gpio_if = audio_codec_new_gpio();
    aw.reset_pin = GPIO_NUM_NC;
    aw.hw_gain.pa_voltage = 5.0f;
    aw.hw_gain.codec_dac_voltage = 3.3f;
    aw.hw_gain.pa_gain = 1;
    esp_codec_dev_cfg_t out_cfg = {};
    out_cfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    out_cfg.codec_if = aw88298_codec_new(&aw);
    out_cfg.data_if = data_if;
    s_out = esp_codec_dev_new(&out_cfg);

    // Microphones: ES7210, the two mics only. With two inputs it outputs plain
    // stereo I2S (left = mic 1, right = mic 2) rather than TDM.
    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    es7210_codec_cfg_t es = {};
    es.ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    es.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2;
    esp_codec_dev_cfg_t in_cfg = {};
    in_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    in_cfg.codec_if = es7210_codec_new(&es);
    in_cfg.data_if = data_if;
    s_in = esp_codec_dev_new(&in_cfg);

    if (!s_out || !s_in) {
        ESP_LOGE(TAG, "codec setup failed (speaker %s, mics %s)", s_out ? "ok" : "failed", s_in ? "ok" : "failed");
        return false;
    }

    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.sample_rate = kAudioSampleRate;
    fs.mclk_multiple = 256;
    fs.channel = 2;
    if (esp_codec_dev_open(s_in, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "could not open the microphones");
        return false;
    }
    esp_codec_dev_set_in_gain(s_in, kMicGainDb);

    fs.channel = 1;
    if (esp_codec_dev_open(s_out, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "could not open the speaker");
        return false;
    }
    esp_codec_dev_set_out_vol(s_out, 60);
    ESP_LOGI(TAG, "full-duplex audio ready: 2 mics + speaker at %u Hz", (unsigned)kAudioSampleRate);
    return true;
}

bool audioReadStereo(int16_t* buf, int frames) {
    return s_in && esp_codec_dev_read(s_in, buf, frames * 2 * sizeof(int16_t)) == ESP_CODEC_DEV_OK;
}

bool audioWrite(const int16_t* samples, int count) {
    return s_out && esp_codec_dev_write(s_out, (void*)samples, count * sizeof(int16_t)) == ESP_CODEC_DEV_OK;
}

void audioSetVolume(int percent) {
    if (s_out) esp_codec_dev_set_out_vol(s_out, percent);
}
