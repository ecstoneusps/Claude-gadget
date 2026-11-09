#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "settings";
static nvs_handle_t s_nvs = 0;

void settingsBegin() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partition is full or from an incompatible version: start fresh.
        ESP_LOGW(TAG, "resetting settings storage (%s)", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err == ESP_OK) err = nvs_open("buddy", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings unavailable (%s); using defaults", esp_err_to_name(err));
        s_nvs = 0;
    }
}

bool settingsGetBool(const char* key, bool fallback) {
    uint8_t v;
    if (!s_nvs || nvs_get_u8(s_nvs, key, &v) != ESP_OK) return fallback;
    return v != 0;
}

uint32_t settingsGetU32(const char* key, uint32_t fallback) {
    uint32_t v;
    if (!s_nvs || nvs_get_u32(s_nvs, key, &v) != ESP_OK) return fallback;
    return v;
}

void settingsSetU32(const char* key, uint32_t value) {
    if (!s_nvs) return;
    uint32_t old;
    if (nvs_get_u32(s_nvs, key, &old) == ESP_OK && old == value) return;  // spare the flash
    if (nvs_set_u32(s_nvs, key, value) == ESP_OK && nvs_commit(s_nvs) == ESP_OK) {
        ESP_LOGI(TAG, "saved %s = %u", key, (unsigned)value);
    } else {
        ESP_LOGW(TAG, "could not save %s", key);
    }
}

void settingsSetBool(const char* key, bool value) {
    if (!s_nvs) return;
    uint8_t old;
    if (nvs_get_u8(s_nvs, key, &old) == ESP_OK && (old != 0) == value) return;  // spare the flash
    if (nvs_set_u8(s_nvs, key, value ? 1 : 0) == ESP_OK && nvs_commit(s_nvs) == ESP_OK) {
        ESP_LOGI(TAG, "saved %s = %s", key, value ? "true" : "false");
    } else {
        ESP_LOGW(TAG, "could not save %s", key);
    }
}
