#include "wifi.h"

#include <atomic>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char* TAG = "wifi";

static std::atomic<bool> s_connected{false};
static std::atomic<const char*> s_status{"off"};
static int s_failures = 0;
static esp_timer_handle_t s_retry = nullptr;

static void retry(void*) { esp_wifi_connect(); }

static void onEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_status.store("connecting");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* d = (wifi_event_sta_disconnected_t*)data;
        const bool wasConnected = s_connected.exchange(false);
        switch (d->reason) {
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                s_status.store("wrong password");
                break;
            case WIFI_REASON_NO_AP_FOUND:
                s_status.store("network not found");
                break;
            default:
                s_status.store("connecting");
        }
        // Log the first few failures and then only now and then.
        if (wasConnected || s_failures < 3 || s_failures % 30 == 0)
            ESP_LOGW(TAG, "disconnected (reason %d: %s), retrying", d->reason, s_status.load());
        ++s_failures;
        // Back off a little so a missing network doesn't spin the radio.
        esp_timer_stop(s_retry);
        esp_timer_start_once(s_retry, (s_failures < 5 ? 500 : 5000) * 1000ull);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* e = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "connected, address " IPSTR, IP2STR(&e->ip_info.ip));
        s_failures = 0;
        s_status.store("connected");
        s_connected.store(true);
    }
}

bool wifiStart(const std::string& ssid, const std::string& password) {
    if (ssid.empty()) return false;
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;  // already created
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network stack init failed: %s", esp_err_to_name(err));
        return false;
    }
    const esp_timer_create_args_t targs = {.callback = retry, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                           .name = "wifi_retry", .skip_unhandled_events = true};
    esp_timer_create(&targs, &s_retry);
    esp_netif_t* netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "desk-buddy");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr);

    wifi_config_t cfg = {};
    strncpy((char*)cfg.sta.ssid, ssid.c_str(), sizeof cfg.sta.ssid);
    strncpy((char*)cfg.sta.password, password.c_str(), sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;  // pick the strongest access point
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // settings.txt is the source of truth
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if ((err = esp_wifi_start()) != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        return false;
    }
    // Power saving adds up to a few hundred ms of delay per packet; answers
    // should come back as fast as possible.
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid.c_str());
    return true;
}

bool wifiConnected() { return s_connected.load(); }
const char* wifiStatus() { return s_status.load(); }
