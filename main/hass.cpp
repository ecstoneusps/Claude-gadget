#include "hass.h"

#include <math.h>
#include <string.h>

#include <atomic>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include "wifi.h"

static const char* TAG = "hass";

static esp_mqtt_client_handle_t s_mqtt = nullptr;
static std::atomic<bool> s_connected{false};
static std::atomic<bool> s_sendAll{false};  // (re)connected: send discovery and every state
static QueueHandle_t s_commands = nullptr;   // HassCommand*
static HassConfig s_cfg;
static std::string s_node;  // desk_buddy_a1b2c3
static std::string s_base;  // buddy/desk_buddy_a1b2c3

static std::string topic(const char* leaf) { return s_base + "/" + leaf; }

static void publish(const std::string& t, const std::string& payload, bool retain = false) {
    if (!s_mqtt || !s_connected.load()) return;
    esp_mqtt_client_enqueue(s_mqtt, t.c_str(), payload.data(), (int)payload.size(), retain ? 1 : 0, retain, true);
}

// ---------------------------------------------------------------------------
// Discovery: one retained config message per entity, so Home Assistant
// creates the "Desk Buddy" device and its entities by itself.

struct Entity {
    const char* component;  // sensor, binary_sensor, number, switch, text, button, event
    const char* id;         // object id, also the state topic leaf
    const char* name;
    const char* deviceClass;
    const char* unit;
    const char* icon;
    bool diagnostic;
};

static const Entity kEntities[] = {
    {"binary_sensor", "at_desk", "At desk", "occupancy", nullptr, nullptr, false},
    {"binary_sensor", "talking", "Someone talking", "sound", nullptr, nullptr, false},
    {"binary_sensor", "awake", "Awake", nullptr, nullptr, "mdi:sleep-off", false},
    {"sensor", "distance", "Distance", "distance", "cm", nullptr, false},
    {"sensor", "movement", "Movement", "enum", nullptr, "mdi:run", false},
    {"sensor", "moving_energy", "Movement strength", nullptr, "%", "mdi:motion-sensor", false},
    {"sensor", "still_energy", "Stillness strength", nullptr, "%", "mdi:human-handsdown", false},
    {"sensor", "light", "Light level", "illuminance", "lx", nullptr, false},
    {"sensor", "sound_level", "Sound level", "sound_pressure", "dB", nullptr, false},
    {"sensor", "mood", "Mood", nullptr, nullptr, "mdi:emoticon-outline", false},
    {"sensor", "battery", "Battery", "battery", "%", nullptr, true},
    {"sensor", "usb_voltage", "USB voltage", "voltage", "V", nullptr, true},
    {"sensor", "wifi_signal", "Wi-Fi signal", "signal_strength", "dBm", nullptr, true},
    {"sensor", "wifi_network", "Wi-Fi network", nullptr, nullptr, "mdi:wifi", true},
    {"sensor", "uptime", "Uptime", "duration", "s", nullptr, true},
    {"sensor", "free_memory", "Free memory", "data_size", "kB", nullptr, true},
    {"number", "volume", "Volume", nullptr, "%", "mdi:volume-high", false},
    {"number", "brightness", "Screen brightness", nullptr, "%", "mdi:brightness-6", false},
    {"switch", "mute", "Mute", nullptr, nullptr, "mdi:volume-off", false},
    {"text", "say", "Say", nullptr, nullptr, "mdi:account-voice", false},
    {"button", "photo", "Take a photo", nullptr, nullptr, "mdi:camera", false},
    {"button", "sleep", "Sleep", nullptr, nullptr, "mdi:sleep", false},
    {"button", "wake", "Wake up", nullptr, nullptr, "mdi:alarm", false},
    {"event", "interaction", "Interaction", nullptr, nullptr, "mdi:gesture-tap", false},
};

static void publishDiscovery() {
    for (const Entity& e : kEntities) {
        cJSON* c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "name", e.name);
        cJSON_AddStringToObject(c, "unique_id", (s_node + "_" + e.id).c_str());
        cJSON_AddStringToObject(c, "availability_topic", topic("status").c_str());
        if (e.deviceClass) cJSON_AddStringToObject(c, "device_class", e.deviceClass);
        if (e.unit) cJSON_AddStringToObject(c, "unit_of_measurement", e.unit);
        if (e.icon) cJSON_AddStringToObject(c, "icon", e.icon);
        if (e.diagnostic) cJSON_AddStringToObject(c, "entity_category", "diagnostic");
        const std::string comp = e.component;
        if (comp != "button" && comp != "event") cJSON_AddStringToObject(c, "state_topic", topic(e.id).c_str());
        if (comp == "sensor" && e.unit && strcmp(e.id, "uptime") != 0)
            cJSON_AddStringToObject(c, "state_class", "measurement");
        if (comp == "sensor" && strcmp(e.id, "movement") == 0) {
            cJSON* opts = cJSON_AddArrayToObject(c, "options");
            for (const char* o : {"none", "moving", "still", "both"}) cJSON_AddItemToArray(opts, cJSON_CreateString(o));
        }
        if (comp == "number") {
            cJSON_AddStringToObject(c, "command_topic", topic((std::string(e.id) + "/set").c_str()).c_str());
            cJSON_AddNumberToObject(c, "min", strcmp(e.id, "volume") == 0 ? 0 : 5);
            cJSON_AddNumberToObject(c, "max", 100);
            cJSON_AddNumberToObject(c, "step", 5);
            cJSON_AddStringToObject(c, "mode", "slider");
        } else if (comp == "switch") {
            cJSON_AddStringToObject(c, "command_topic", topic("mute/set").c_str());
        } else if (comp == "text") {
            cJSON_AddStringToObject(c, "command_topic", topic("say/set").c_str());
            cJSON_AddNumberToObject(c, "max", 255);
        } else if (comp == "button") {
            cJSON_AddStringToObject(c, "command_topic", topic((std::string(e.id) + "/press").c_str()).c_str());
        } else if (comp == "event") {
            cJSON_AddStringToObject(c, "state_topic", topic("event").c_str());
            cJSON* types = cJSON_AddArrayToObject(c, "event_types");
            for (const char* t : {"tapped", "petted", "shaken", "wake_word"})
                cJSON_AddItemToArray(types, cJSON_CreateString(t));
        }
        cJSON* dev = cJSON_AddObjectToObject(c, "device");
        cJSON* ids = cJSON_AddArrayToObject(dev, "identifiers");
        cJSON_AddItemToArray(ids, cJSON_CreateString(s_node.c_str()));
        cJSON_AddStringToObject(dev, "name", "Desk Buddy");
        cJSON_AddStringToObject(dev, "manufacturer", "DIY (M5Stack CoreS3)");
        cJSON_AddStringToObject(dev, "model", "Desk Buddy");
        cJSON_AddStringToObject(dev, "sw_version", s_cfg.version.c_str());
        char* text = cJSON_PrintUnformatted(c);
        cJSON_Delete(c);
        if (text) {
            publish("homeassistant/" + comp + "/" + s_node + "/" + e.id + "/config", text, true);
            cJSON_free(text);
        }
    }
    publish(topic("status"), "online", true);
}

// ---------------------------------------------------------------------------
// Commands from Home Assistant.

static void command(HassCommand::Type type, int value, const std::string& text = "") {
    auto* c = new HassCommand{type, value, text};
    if (xQueueSend(s_commands, &c, 0) != pdTRUE) delete c;
}

static void onMessage(const std::string& t, const std::string& data) {
    if (t == "homeassistant/status") {  // Home Assistant restarted: announce everything again
        if (data == "online") s_sendAll.store(true);
        return;
    }
    if (t.compare(0, s_base.size() + 1, s_base + "/") != 0) return;
    const std::string leaf = t.substr(s_base.size() + 1);
    const int n = atoi(data.c_str());
    if (leaf == "volume/set") command(HassCommand::Volume, n);
    else if (leaf == "brightness/set") command(HassCommand::Brightness, n);
    else if (leaf == "mute/set") command(HassCommand::Mute, data == "ON" ? 1 : 0);
    else if (leaf == "say/set") command(HassCommand::Say, 0, data);
    else if (leaf == "photo/press") command(HassCommand::Photo, 0);
    else if (leaf == "sleep/press") command(HassCommand::Sleep, 0);
    else if (leaf == "wake/press") command(HassCommand::Wake, 0);
}

static void onMqttEvent(void*, esp_event_base_t, int32_t id, void* data) {
    auto* ev = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "connected to Home Assistant's MQTT broker");
            s_connected.store(true);
            esp_mqtt_client_subscribe(s_mqtt, (s_base + "/+/set").c_str(), 0);
            esp_mqtt_client_subscribe(s_mqtt, (s_base + "/+/press").c_str(), 0);
            esp_mqtt_client_subscribe(s_mqtt, "homeassistant/status", 0);
            s_sendAll.store(true);
            break;
        case MQTT_EVENT_DISCONNECTED:
            if (s_connected.exchange(false)) ESP_LOGW(TAG, "lost the MQTT broker; reconnecting");
            break;
        case MQTT_EVENT_ERROR:
            if (ev && ev->error_handle && ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
                ESP_LOGW(TAG, "the MQTT broker refused the login (check mqtt_user / mqtt_password)");
            break;
        case MQTT_EVENT_DATA:
            // Small messages arrive whole; ignore anything split into parts.
            if (ev->current_data_offset == 0 && ev->data_len == ev->total_data_len)
                onMessage(std::string(ev->topic, ev->topic_len), std::string(ev->data, ev->data_len));
            break;
        default:
            break;
    }
}

bool hassStart(const HassConfig& cfg) {
    if (cfg.url.empty()) return false;
    s_cfg = cfg;
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char node[32];
    snprintf(node, sizeof node, "desk_buddy_%02x%02x%02x", mac[3], mac[4], mac[5]);
    s_node = node;
    s_base = "buddy/" + s_node;
    s_commands = xQueueCreate(8, sizeof(HassCommand*));

    static std::string lwt;
    lwt = topic("status");
    esp_mqtt_client_config_t mc = {};
    mc.broker.address.uri = s_cfg.url.c_str();
    mc.credentials.username = s_cfg.user.empty() ? nullptr : s_cfg.user.c_str();
    mc.credentials.client_id = s_node.c_str();
    mc.credentials.authentication.password = s_cfg.password.empty() ? nullptr : s_cfg.password.c_str();
    mc.session.last_will.topic = lwt.c_str();
    mc.session.last_will.msg = "offline";
    mc.session.last_will.qos = 1;
    mc.session.last_will.retain = 1;
    mc.session.keepalive = 30;
    mc.task.stack_size = 4096;
    mc.buffer.size = 1024;
    mc.buffer.out_size = 2048;
    mc.network.reconnect_timeout_ms = 10000;
    s_mqtt = esp_mqtt_client_init(&mc);
    if (!s_mqtt) return false;
    esp_mqtt_client_register_event(s_mqtt, MQTT_EVENT_ANY, onMqttEvent, nullptr);
    if (esp_mqtt_client_start(s_mqtt) != ESP_OK) return false;
    ESP_LOGI(TAG, "connecting to %s as \"%s\" (device %s)", s_cfg.url.c_str(), s_cfg.user.c_str(), s_node.c_str());
    return true;
}

bool hassConnected() { return s_connected.load(); }

// ---------------------------------------------------------------------------
// States: sent when they change (numbers: by enough to matter), and all of
// them once a minute.

static std::string fmt(float v, int decimals) {
    char b[24];
    snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

void hassUpdate(const HassState& s, uint32_t now) {
    if (!s_connected.load()) return;
    static HassState last;
    static uint32_t lastAll = 0, lastSound = 0;
    bool all = false;
    if (s_sendAll.exchange(false)) {
        publishDiscovery();
        all = true;
    }
    if (now - lastAll > 60000) all = true;
    if (all) lastAll = now;

    auto onoff = [](bool b) { return std::string(b ? "ON" : "OFF"); };
    if (all || s.atDesk != last.atDesk) publish(topic("at_desk"), onoff(s.atDesk));
    if (all || s.talking != last.talking) publish(topic("talking"), onoff(s.talking));
    if (all || s.awake != last.awake) publish(topic("awake"), onoff(s.awake));
    if (all || s.muted != last.muted) publish(topic("mute"), onoff(s.muted));
    if (all || strcmp(s.mood, last.mood) != 0) publish(topic("mood"), s.mood);
    if (all || s.volume != last.volume) publish(topic("volume"), std::to_string(s.volume));
    if (all || s.brightness != last.brightness) publish(topic("brightness"), std::to_string(s.brightness));
    // Radar: distance by 10 cm or more, strengths by 10 points.
    if (all || strcmp(s.movement, last.movement) != 0) publish(topic("movement"), s.haveRadar ? s.movement : "None");
    if (all || (s.distanceCm < 0) != (last.distanceCm < 0) || abs(s.distanceCm - last.distanceCm) >= 10)
        publish(topic("distance"), s.distanceCm < 0 ? "None" : std::to_string(s.distanceCm));
    if (all || abs(s.movingEnergy - last.movingEnergy) >= 10)
        publish(topic("moving_energy"), s.haveRadar ? std::to_string(s.movingEnergy) : "None");
    if (all || abs(s.stillEnergy - last.stillEnergy) >= 10)
        publish(topic("still_energy"), s.haveRadar ? std::to_string(s.stillEnergy) : "None");
    // Light: by 10% (and at least 1 lx).
    if (all || (s.lux >= 0 && fabsf(s.lux - last.lux) >= fmaxf(1.0f, last.lux * 0.1f)))
        publish(topic("light"), s.lux < 0 ? "None" : fmt(s.lux, s.lux < 10 ? 1 : 0));
    // Sound level: every 10 s at most (it changes all the time).
    if (all || now - lastSound > 10000) {
        lastSound = now;
        publish(topic("sound_level"), fmt(s.soundDb, 0));
    }
    if (all) {
        publish(topic("battery"), std::to_string(s.battery));
        publish(topic("usb_voltage"), fmt(s.usbMv / 1000.0f, 2));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) publish(topic("wifi_signal"), std::to_string(ap.rssi));
        publish(topic("wifi_network"), wifiNetworkName());
        publish(topic("uptime"), std::to_string((long)(esp_timer_get_time() / 1000000)));
        publish(topic("free_memory"), std::to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
    last = s;
}

void hassEvent(const char* type, uint32_t now) {
    // Petting repeats while your hand stays on him: once per touch is enough.
    static uint32_t lastPet = 0;
    if (strcmp(type, "petted") == 0) {
        if (now - lastPet < 5000) {
            lastPet = now;
            return;
        }
        lastPet = now;
    }
    publish(topic("event"), std::string("{\"event_type\":\"") + type + "\"}");
}

bool hassTakeCommand(HassCommand& out) {
    HassCommand* c = nullptr;
    if (!s_commands || xQueueReceive(s_commands, &c, 0) != pdTRUE) return false;
    out = *c;
    delete c;
    return true;
}
