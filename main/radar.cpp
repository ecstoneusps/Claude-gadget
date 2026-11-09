#include "radar.h"

#include <M5Unified.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "settings.h"

static const char* TAG = "radar";

static constexpr uart_port_t kPort = UART_NUM_1;
// The LD2410's factory speed first, then the others its app can set.
static constexpr int kBauds[] = {256000, 115200, 57600, 38400, 9600};
static constexpr int kNumBauds = sizeof kBauds / sizeof kBauds[0];
static constexpr uint32_t kSwapAfterMs = 2500;
static int s_baud = 0;              // index into kBauds
static uint32_t s_bytes = 0;        // bytes heard while searching (any at all = it has power)

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
    // The ports' 5 V comes from a switch the CoreS3 turns on at start. Without
    // a battery, M5Unified first checks that nothing else is feeding the
    // ports, and if it can't tell it leaves them off, so the radar gets no
    // power. Ask again (it waits for the reading), and say what happened.
    if (!M5.Power.getExtOutput()) {
        M5.Power.setExtOutput(true);
        ESP_LOGI(TAG, "5 V to the ports was off; switched on again: %s",
                 M5.Power.getExtOutput() ? "now on" : "STILL OFF (the radar has no power)");
    } else {
        ESP_LOGI(TAG, "5 V to the ports is on");
    }
    // Start with the speed and wiring that worked last time (found by trying
    // them all, which takes a few seconds), so a known radar is found at once.
    const uint32_t savedBaud = settingsGetU32("radar_baud", 0);
    for (int i = 0; i < kNumBauds; ++i)
        if ((uint32_t)kBauds[i] == savedBaud) s_baud = i;
    s_swapped = savedBaud && settingsGetBool("radar_swap", false);
    const uart_config_t cfg = {
        .baud_rate = kBauds[s_baud],
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
    ESP_LOGI(TAG, "listening on Port C (GPIO %d receive, %d send, %d baud%s)", s_rx, s_tx, kBauds[s_baud],
             savedBaud ? ", as last time" : "");
}

void radarPoll(uint32_t now) {
    if (!s_open) return;
    if (s_triedSince == 0) s_triedSince = now;
    uint8_t buf[128];
    int n;
    while ((n = uart_read_bytes(kPort, buf, sizeof buf, 0)) > 0) {
        if (!s_found) s_bytes += n;
        if (s_parser.feed(buf, n) > 0) {
            s_lastReading = now;
            if (!s_found) {
                s_found = true;
                ESP_LOGI(TAG, "LD2410 radar found at %d baud%s", kBauds[s_baud],
                         s_swapped ? " (its signal wires are crossed; handled)" : "");
                settingsSetU32("radar_baud", (uint32_t)kBauds[s_baud]);
                settingsSetBool("radar_swap", s_swapped);
            }
        }
    }
    // Nothing yet: maybe RX and TX are the other way round. Keep alternating
    // (cheap) so a radar plugged in later is picked up too.
    // After the first minute, try the other wiring only every 30 s.
    const uint32_t swapAfter = s_swaps < 24 ? kSwapAfterMs : 30000;
    // Each speed is tried with the wires both ways round, then the next speed.
    if (!s_found && now - s_triedSince > swapAfter) {
        s_swapped = !s_swapped;
        if (!s_swapped) {
            s_baud = (s_baud + 1) % kNumBauds;
            uart_set_baudrate(kPort, kBauds[s_baud]);
        }
        s_triedSince = now;
        applyPins();
        if (++s_swaps == 2 * kNumBauds) {
            if (s_bytes == 0)
                ESP_LOGI(TAG, "no radar on Port C: not a single byte at any speed, either wiring. "
                              "None plugged in? Then ignore this. Otherwise it has no power: check VCC/GND.");
            else
                ESP_LOGI(TAG, "Port C hears something (%u bytes) but no radar data at any speed: "
                              "check the yellow/white wires, or its baud rate in the HLKRadarTool app", (unsigned)s_bytes);
        }
    }
}

bool radarFound() { return s_found; }
bool radarFresh(uint32_t now) { return s_found && now - s_lastReading < 2000; }
const RadarReading& radarLatest() { return s_parser.latest(); }
