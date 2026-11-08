#include "sdcard.h"

#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char* TAG = "sdcard";
static constexpr const char* kMount = "/sdcard";
static sdmmc_card_t* s_card = nullptr;

bool sdcardMount() {
    // The display driver (M5GFX) has already set up SPI2 on these pins:
    // SCK 36, MOSI 37, MISO 35 (shared with the display's D/C line). The card
    // just adds itself to that bus with its own chip-select, GPIO 4.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 20 MHz

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = GPIO_NUM_4;

    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;  // never erase someone's card
    mount.max_files = 4;
    mount.allocation_unit_size = 16 * 1024;

    esp_err_t err = esp_vfs_fat_sdspi_mount(kMount, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        s_card = nullptr;
        if (err == ESP_FAIL) ESP_LOGW(TAG, "card found but not readable; is it formatted FAT32?");
        else ESP_LOGI(TAG, "no SD card (%s); using built-in settings", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "SD card mounted: %s, %llu MB", s_card->cid.name,
             (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
    return true;
}

bool sdcardMounted() { return s_card != nullptr; }

static std::string fullPath(const char* path) { return std::string(kMount) + "/" + path; }

bool sdcardReadFile(const char* path, std::string& out) {
    if (!s_card) return false;
    FILE* f = fopen(fullPath(path).c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        out.append(buf, n);
        if (out.size() > 64 * 1024) break;  // a settings file is never this big
    }
    fclose(f);
    return true;
}

bool sdcardWriteFile(const char* path, const std::string& data) {
    if (!s_card) return false;
    FILE* f = fopen(fullPath(path).c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

std::string sdcardSavePhoto(const uint8_t* jpeg, size_t len) {
    if (!s_card || !jpeg || len == 0) return "";
    const std::string dir = fullPath("photos");
    mkdir(dir.c_str(), 0775);  // fine if it's already there
    // Next number after the highest IMG_NNNN.jpg in the folder (so deleting
    // old photos never makes a new one overwrite another).
    int highest = 0;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            int n = 0;
            if (sscanf(e->d_name, "IMG_%d.", &n) == 1 || sscanf(e->d_name, "img_%d.", &n) == 1)
                if (n > highest) highest = n;
        }
        closedir(d);
    }
    char name[32];
    snprintf(name, sizeof name, "IMG_%04d.jpg", highest + 1);
    const std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        ESP_LOGW(TAG, "couldn't create %s", path.c_str());
        return "";
    }
    const bool ok = fwrite(jpeg, 1, len, f) == len;
    if (fclose(f) != 0 || !ok) {
        ESP_LOGW(TAG, "couldn't write %s (card full?)", path.c_str());
        return "";
    }
    return name;
}

bool sdcardReadBinary(const char* path, std::vector<uint8_t>& out, size_t maxBytes) {
    if (!s_card) return false;
    FILE* f = fopen(fullPath(path).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = size > 0 && (size_t)size <= maxBytes;
    if (ok) {
        out.resize(size);
        ok = fread(out.data(), 1, size, f) == (size_t)size;
    }
    fclose(f);
    return ok;
}
