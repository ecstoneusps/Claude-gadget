#include "wifi.h"

#include <atomic>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char* TAG = "wifi";

static std::vector<std::pair<std::string, std::string>> s_networks;
static std::atomic<bool> s_connected{false};
static std::atomic<const char*> s_status{"off"};
static std::atomic<int> s_current{-1};  // network connected to (or being tried)
static std::atomic<int> s_onNet{-1};    // network connected to
static int s_failures = 0;              // failed attempts on the current network
static int s_rotate = 0;                // when none show up in a scan, try them in turn (hidden networks)
static bool s_started = false;
static bool s_scanning = false;
static bool s_wantScan = false;        // the next retry looks around first
static esp_timer_handle_t s_retry = nullptr;

// Setup hotspot: when no saved network has worked for a while (or none is
// saved), Buddy opens his own open network so the settings page can still be
// reached. Anything changed there still needs a tap on his screen.
static constexpr const char* kSetupSsid = "Buddy Setup";
static constexpr uint32_t kSetupAfterMs = 120000;
static bool s_setupAllowed = true;
static std::atomic<bool> s_apOn{false};
static std::atomic<int> s_apClients{0};
static uint32_t s_offlineSince = 0;       // when it last had no connection (ms)
static esp_timer_handle_t s_watch = nullptr;

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

static void startSetupHotspot() {
    wifi_config_t ap = {};
    strncpy((char*)ap.ap.ssid, kSetupSsid, sizeof ap.ap.ssid);
    ap.ap.ssid_len = strlen(kSetupSsid);
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 2;
    ap.ap.channel = 1;
    esp_wifi_set_mode(s_networks.empty() ? WIFI_MODE_AP : WIFI_MODE_APSTA);
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) == ESP_OK) {
        s_apOn.store(true);
        ESP_LOGW(TAG, "%s: started the setup hotspot \"%s\": join it and open http://192.168.71.1/ (%u KB internal RAM free)",
                 s_networks.empty() ? "no Wi-Fi network saved" : "no saved network worked for 2 minutes", kSetupSsid,
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
}

static void stopSetupHotspot() {
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_apOn.store(false);
    s_apClients.store(0);
    ESP_LOGI(TAG, "connected again: setup hotspot off");
}

// Every few seconds: start the setup hotspot after a long time offline; turn
// it off once connected again (unless someone is still using it).
static void watch(void*) {
    if (s_connected.load()) {
        if (s_apOn.load() && s_apClients.load() == 0) stopSetupHotspot();
        return;
    }
    if (!s_apOn.load() && s_setupAllowed && nowMs() - s_offlineSince > kSetupAfterMs) startSetupHotspot();
}

static void useNetwork(int i) {
    s_current.store(i);
    const auto& n = s_networks[i];
    wifi_config_t cfg = {};
    strncpy((char*)cfg.sta.ssid, n.first.c_str(), sizeof cfg.sta.ssid);
    strncpy((char*)cfg.sta.password, n.second.c_str(), sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = n.second.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;  // pick the strongest access point
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    ESP_LOGI(TAG, "connecting to \"%s\"%s", n.first.c_str(), i == 0 ? "" : " (not the first network)");
    esp_wifi_connect();
}

// Looks around for the saved networks; onEvent(SCAN_DONE) picks one.
static void startScan() {
    if (s_networks.size() == 1) {  // nothing to choose between
        useNetwork(0);
        return;
    }
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    if (esp_wifi_scan_start(&sc, false) == ESP_OK) s_scanning = true;
    else useNetwork(0);
}

static void pickFromScan() {
    s_scanning = false;
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    std::vector<bool> seen(s_networks.size(), false);
    if (count > 0) {
        auto* recs = (wifi_ap_record_t*)heap_caps_calloc(count, sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
        if (recs && esp_wifi_scan_get_ap_records(&count, recs) == ESP_OK) {
            for (uint16_t r = 0; r < count; ++r)
                for (size_t i = 0; i < s_networks.size(); ++i)
                    if (s_networks[i].first == (const char*)recs[r].ssid) seen[i] = true;
        } else {
            esp_wifi_clear_ap_list();
        }
        heap_caps_free(recs);
    }
    for (size_t i = 0; i < s_networks.size(); ++i) {
        if (seen[i]) {
            useNetwork((int)i);
            return;
        }
    }
    // None in sight: maybe one is hidden. Try them in turn between scans.
    s_status.store("no saved network in range");
    useNetwork(s_rotate++ % (int)s_networks.size());
}

static void retry(void*) {
    // Someone is on the setup hotspot: hold off, since trying networks makes
    // the radio hop channels and drops them.
    if (s_apClients.load() > 0) {
        esp_timer_start_once(s_retry, 30000 * 1000ull);
        return;
    }
    // A few quick retries on the same network, then look around again (it may
    // be out of range now, and another saved network in range).
    if (s_wantScan) startScan();
    else esp_wifi_connect();
}

static void onEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "someone joined the setup hotspot");
        s_apClients.fetch_add(1);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_apClients.fetch_sub(1) <= 0) s_apClients.store(0);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_networks.empty()) return;
        s_status.store("connecting");
        startScan();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        pickFromScan();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* d = (wifi_event_sta_disconnected_t*)data;
        const bool wasConnected = s_connected.exchange(false);
        if (wasConnected) s_offlineSince = nowMs();
        s_onNet.store(-1);
        if (wasConnected) s_failures = 0;
        switch (d->reason) {
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                s_status.store("wrong password");
                break;
            case WIFI_REASON_NO_AP_FOUND:
                s_status.store(s_networks.size() > 1 ? "no saved network in range" : "network not found");
                break;
            default:
                s_status.store("connecting");
        }
        // Log the first few failures and then only now and then.
        static int logged = 0;
        if (wasConnected || logged < 3 || logged % 30 == 0) {
            const int cur = s_current.load();
            ESP_LOGW(TAG, "disconnected from \"%s\" (reason %d: %s), retrying",
                     cur >= 0 ? s_networks[cur].first.c_str() : "?", d->reason, s_status.load());
        }
        ++logged;
        // A few quick retries, then a pause and a fresh look around (backing
        // off so a missing network doesn't spin the radio).
        s_wantScan = ++s_failures >= 3;
        if (s_wantScan) s_failures = 0;
        esp_timer_stop(s_retry);
        esp_timer_start_once(s_retry, (s_wantScan ? 5000 : 500) * 1000ull);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* e = (ip_event_got_ip_t*)data;
        const int cur = s_current.load();
        ESP_LOGI(TAG, "connected to \"%s\", address " IPSTR, cur >= 0 ? s_networks[cur].first.c_str() : "?",
                 IP2STR(&e->ip_info.ip));
        s_failures = 0;
        s_status.store("connected");
        s_onNet.store(cur);
        s_connected.store(true);
    }
}

bool wifiStart(const std::vector<std::pair<std::string, std::string>>& networks, bool setupHotspot) {
    if (networks.empty() && !setupHotspot) return false;
    s_networks = networks;
    s_setupAllowed = setupHotspot;
    ESP_LOGI(TAG, "starting (%u KB internal RAM free, %u saved network%s)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)networks.size(),
             networks.size() == 1 ? "" : "s");
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
    if (setupHotspot) {
        // Its own address range: the default (192.168.4.x) can be the same as a
        // home network's, which confuses routing while both are up.
        esp_netif_t* ap = esp_netif_create_default_wifi_ap();
        esp_netif_ip_info_t ip = {};
        esp_netif_set_ip4_addr(&ip.ip, 192, 168, 71, 1);
        esp_netif_set_ip4_addr(&ip.gw, 192, 168, 71, 1);
        esp_netif_set_ip4_addr(&ip.netmask, 255, 255, 255, 0);
        esp_netif_dhcps_stop(ap);
        esp_netif_set_ip_info(ap, &ip);
        esp_netif_dhcps_start(ap);
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s (%u KB internal RAM free, largest block %u KB)", esp_err_to_name(err),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
        s_status.store(err == ESP_ERR_NO_MEM ? "FAILED: out of memory" : "FAILED to start");
        return false;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // settings.txt is the source of truth
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (networks.empty()) startSetupHotspot();  // nothing to join: straight to the setup hotspot
    if ((err = esp_wifi_start()) != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        s_status.store(err == ESP_ERR_NO_MEM ? "FAILED: out of memory" : "FAILED to start");
        return false;
    }
    // Power saving adds up to a few hundred ms of delay per packet; answers
    // should come back as fast as possible.
    esp_wifi_set_ps(WIFI_PS_NONE);
    s_offlineSince = nowMs();
    if (setupHotspot) {
        const esp_timer_create_args_t wargs = {.callback = watch, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                               .name = "wifi_watch", .skip_unhandled_events = true};
        if (esp_timer_create(&wargs, &s_watch) == ESP_OK) esp_timer_start_periodic(s_watch, 5000 * 1000ull);
    }
    s_started = true;
    return true;
}

bool wifiConnected() { return s_connected.load(); }
bool wifiSetupHotspotOn() { return s_apOn.load(); }
bool wifiConnectedOrStarting() { return s_started; }
const char* wifiStatus() { return s_status.load(); }
int wifiNetworkIndex() { return s_onNet.load(); }
const char* wifiNetworkName() {
    const int i = s_onNet.load();
    return i >= 0 ? s_networks[i].first.c_str() : "";
}
