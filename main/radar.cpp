#include "radar.h"

#include <M5Unified.h>
#include "driver/uart.h"
#include "esp_log.h"

static const char* TAG = "radar";

static constexpr uart_port_t kPort = UART_NUM_1;
static constexpr int kBaud = 256000;  // the LD2410's factory setting
static constexpr uint32_t kSwapAfterMs = 2500;

static bool s_open = false;
static int s_rx = -1, s_tx = -1;
static bool s_swapped = false;
static uint32_t s_triedSince = 0;
static uint32_t s_lastReading = 0;
static bool s_found = false;
static Ld2410Parser s_parser;
static int s_swaps = 0;

static void applyPins() {
    // Port C: pin 1 (yellow) receives, pin 2 (white) sends. The radar's TX
    // goes to our RX; if the wires are the other way round, swap in software.
    const int rx = s_swapped ? s_tx : s_rx;
    const int tx = s_swapped ? s_rx : s_tx;
    uart_set_pin(kPort, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_flush_input(kPort);
}

void radarBegin() {
    s_rx = M5.getPin(m5::pin_name_t::port_c_rxd);
    s_tx = M5.getPin(m5::pin_name_t::port_c_txd);
    if (s_rx < 0 || s_tx < 0) {
        ESP_LOGW(TAG, "this board has no Port C");
        return;
    }
    const uart_config_t cfg = {
        .baud_rate = kBaud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    if (uart_driver_install(kPort, 1024, 0, 0, nullptr, 0) != ESP_OK || uart_param_config(kPort, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "couldn't open Port C");
        return;
    }
    applyPins();
    // Swapping RX/TX re-claims pins the driver already holds, which it warns
    // about every time ("GPIO 17 is not usable"): expected here, so hush it.
    esp_log_level_set("uart", ESP_LOG_ERROR);
    s_open = true;
    ESP_LOGI(TAG, "listening on Port C (GPIO %d receive, %d send, %d baud)", s_rx, s_tx, kBaud);
}

void radarPoll(uint32_t now) {
    if (!s_open) return;
    if (s_triedSince == 0) s_triedSince = now;
    uint8_t buf[128];
    int n;
    while ((n = uart_read_bytes(kPort, buf, sizeof buf, 0)) > 0) {
        if (s_parser.feed(buf, n) > 0) {
            s_lastReading = now;
            if (!s_found) {
                s_found = true;
                ESP_LOGI(TAG, "LD2410 radar found%s", s_swapped ? " (its signal wires are crossed; handled)" : "");
            }
        }
    }
    // Nothing yet: maybe RX and TX are the other way round. Keep alternating
    // (cheap) so a radar plugged in later is picked up too.
    // After the first minute, try the other wiring only every 30 s.
    const uint32_t swapAfter = s_swaps < 24 ? kSwapAfterMs : 30000;
    if (!s_found && now - s_triedSince > swapAfter) {
        s_swapped = !s_swapped;
        s_triedSince = now;
        applyPins();
        if (++s_swaps == 4)
            ESP_LOGI(TAG, "no radar data on Port C (none plugged in? then ignore this)");
    }
}

bool radarFound() { return s_found; }
bool radarFresh(uint32_t now) { return s_found && now - s_lastReading < 2000; }
const RadarReading& radarLatest() { return s_parser.latest(); }
