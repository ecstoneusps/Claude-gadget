#include "camera.h"

#include <atomic>

#include <M5Unified.h>
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "human_face_detect.hpp"
#include "i2c_bus.h"

static const char* TAG = "camera";

// GC0308 wiring on the CoreS3, from M5Stack's CoreS3 library
// (github.com/m5stack/M5CoreS3, src/utility/GC0308.cpp).
static camera_config_t makeConfig() {
    camera_config_t c = {};
    c.pin_pwdn = -1;
    c.pin_reset = -1;
    c.pin_xclk = -1;  // the CoreS3 clocks the sensor itself
    c.pin_sccb_sda = -1;  // -1: reuse the already-open internal I2C bus (see i2c_bus.h)
    c.pin_sccb_scl = -1;
    c.pin_d7 = 47;
    c.pin_d6 = 48;
    c.pin_d5 = 16;
    c.pin_d4 = 15;
    c.pin_d3 = 42;
    c.pin_d2 = 41;
    c.pin_d1 = 40;
    c.pin_d0 = 39;
    c.pin_vsync = 46;
    c.pin_href = 38;
    c.pin_pclk = 45;
    c.xclk_freq_hz = 20000000;
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = FRAMESIZE_QQVGA;  // 160x120, full field of view (subsample mode)
    c.jpeg_quality = 0;
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    // Always hand face detection the newest frame. (Capturing only on request
    // gave it stale frames, so it lagged far behind someone moving.)
    c.grab_mode = CAMERA_GRAB_LATEST;
    c.sccb_i2c_port = 1;
    return c;
}

// esp32-camera delivers RGB565 high byte first.
static constexpr dl::image::pix_type_t kPixType = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static FaceSighting s_latest = {};
static std::atomic<uint32_t> s_intervalMs{200};
static bool s_cameraOk = false;
static uint16_t* s_preview = nullptr;      // latest frame copy, only in preview mode
static uint32_t s_previewSeq = 0;
static SemaphoreHandle_t s_previewLock = nullptr;

void cameraEnablePreview() {
    if (s_preview) return;
    s_preview = (uint16_t*)heap_caps_malloc(kCamW * kCamH * 2, MALLOC_CAP_SPIRAM);
    s_previewLock = xSemaphoreCreateMutex();
}

bool cameraCopyPreview(uint16_t* dst, uint32_t* seq) {
    if (!s_preview || !s_previewLock) return false;
    bool fresh = false;
    if (xSemaphoreTake(s_previewLock, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_previewSeq != *seq) {
            memcpy(dst, s_preview, kCamW * kCamH * 2);
            *seq = s_previewSeq;
            fresh = true;
        }
        xSemaphoreGive(s_previewLock);
    }
    return fresh;
}

bool cameraBegin() {
    // The camera's control bus (SCCB) is the CoreS3's internal I2C bus, shared
    // with M5Unified and the audio chips through one ESP-IDF bus handle.
    if (!sharedI2cBus()) return false;
    camera_config_t cfg = makeConfig();
    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "camera init failed (%s); face tracking disabled", esp_err_to_name(err));
        return false;
    }
    if (sensor_t* s = esp_camera_sensor_get()) {
        // Brightest auto-exposure target: indoors the default image is dark
        // (mean green ~8/63 measured), which made faces hard to find.
        if (s->set_ae_level) s->set_ae_level(s, 2);
        ESP_LOGI(TAG, "camera ready (sensor PID 0x%02x)", s->id.PID);
    }
    s_cameraOk = true;
    return true;
}

// Brightens a dark big-endian RGB565 frame in place toward a mid-level mean,
// so the face model (trained on normally exposed photos) sees contrast.
// Returns the mean green level (0-63) before brightening.
static int autoBrighten(uint8_t* px, size_t pixels) {
    uint32_t sum = 0, count = 0;
    for (size_t i = 0; i < pixels; i += 7, ++count) sum += ((px[2 * i] << 8 | px[2 * i + 1]) >> 5) & 0x3F;
    const int mean = count ? (int)(sum / count) : 0;
    constexpr int kTarget = 28;  // about 45% brightness
    if (mean <= 0 || mean >= kTarget) return mean;
    int gain256 = kTarget * 256 / mean;  // fixed point
    if (gain256 > 4 * 256) gain256 = 4 * 256;
    for (size_t i = 0; i < pixels; ++i) {
        const uint16_t v = (uint16_t)(px[2 * i] << 8 | px[2 * i + 1]);
        int r = ((v >> 11) & 0x1F) * gain256 >> 8;
        int g = ((v >> 5) & 0x3F) * gain256 >> 8;
        int b = (v & 0x1F) * gain256 >> 8;
        if (r > 31) r = 31;
        if (g > 63) g = 63;
        if (b > 31) b = 31;
        const uint16_t o = (uint16_t)(r << 11 | g << 5 | b);
        px[2 * i] = o >> 8;
        px[2 * i + 1] = o & 0xFF;
    }
    return mean;
}

static void faceTask(void* arg) {
    const bool accurate = arg != nullptr;
    // Hardware test: MSR+MNP found a face in only 14-20% of frames even in
    // bright light; ESPDet-Pico has a much higher published accuracy (mAP
    // 0.504 vs 0.367).
    HumanFaceDetect* detector = new HumanFaceDetect(
        accurate ? HumanFaceDetect::ESPDET_PICO_224_224_FACE : HumanFaceDetect::MSRMNP_S8_V1, false);
    ESP_LOGI(TAG, "face detection model loaded: %s", accurate ? "ESPDet-Pico 224 (accurate)" : "MSR+MNP (fast)");
    uint32_t seq = 0, logged = 0;
    uint32_t hits = 0, tries = 0;    // detection hit rate, for the status line
    TickType_t lastHit = 0;

    while (true) {
        const uint32_t interval = s_intervalMs.load();
        if (interval == 0) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        const TickType_t start = xTaskGetTickCount();

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const int rawBrightness = autoBrighten(fb->buf, (size_t)fb->width * fb->height);
        dl::image::img_t img = {fb->buf, (uint16_t)fb->width, (uint16_t)fb->height, kPixType};
        const int64_t t0 = esp_timer_get_time();
        std::list<dl::detect::result_t>& results = detector->run(img);
        const int detectMs = (int)((esp_timer_get_time() - t0) / 1000);

        FaceSighting f = {};
        f.seq = ++seq;
        int bestArea = 0;
        for (const auto& r : results) {
            const int w = r.box[2] - r.box[0], h = r.box[3] - r.box[1];
            if (w * h <= bestArea) continue;  // the biggest face is probably you
            bestArea = w * h;
            f.found = true;
            f.x = ((r.box[0] + r.box[2]) * 0.5f / fb->width) * 2.0f - 1.0f;
            f.y = ((r.box[1] + r.box[3]) * 0.5f / fb->height) * 2.0f - 1.0f;
            f.size = (float)h / fb->height;
            for (int k = 0; k < 4; ++k) f.box[k] = r.box[k];
        }
        ++tries;
        if (f.found) {
            ++hits;
            lastHit = xTaskGetTickCount();
        }
        if (s_preview && fb->len >= (size_t)kCamW * kCamH * 2 &&
            xSemaphoreTake(s_previewLock, pdMS_TO_TICKS(20)) == pdTRUE) {
            memcpy(s_preview, fb->buf, kCamW * kCamH * 2);
            ++s_previewSeq;
            xSemaphoreGive(s_previewLock);
        }
        // Every ~10 s, a status line whether or not a face was found: detection
        // time, hit rate and the camera's raw brightness (mean green, 0-63).
        if (seq % 50 == 1) {
            ESP_LOGI(TAG, "frame %ux%u, detect %d ms, face found in %u of %u frames; brightness %d/63 (boosted for detection)",
                     fb->width, fb->height, detectMs, (unsigned)hits, (unsigned)tries, rawBrightness);
            hits = tries = 0;
        }
        esp_camera_fb_return(fb);

        taskENTER_CRITICAL(&s_lock);
        s_latest = f;
        taskEXIT_CRITICAL(&s_lock);

        // Log now and then so the direction and range can be checked.
        if (f.found && seq - logged >= 25) {
            logged = seq;
            ESP_LOGI(TAG, "face at x=%+.2f y=%+.2f size=%.2f", f.x, f.y, f.size);
        }

        // Just lost you (seen in the last 2 s)? You probably moved and the
        // frame blurred: look again right away instead of waiting a full
        // interval. With nobody there, it settles back to the normal rate.
        const TickType_t now = xTaskGetTickCount();
        const bool justLost = !f.found && lastHit && now - lastHit < pdMS_TO_TICKS(2000);
        const TickType_t elapsed = now - start;
        const TickType_t wait = pdMS_TO_TICKS(justLost ? 60 : interval);
        // Always rest at least half as long as the detection took. The
        // accurate model takes ~0.5 s, and running it back to back left core 0
        // no idle time at all (task watchdog warnings).
        TickType_t rest = elapsed < wait ? wait - elapsed : 1;
        if (rest < elapsed / 2) rest = elapsed / 2;
        vTaskDelay(rest);
    }
}

void faceTrackingStart(bool accurate) {
    if (!s_cameraOk) return;
    // Core 0, below the mic front-end (priority 5). Core 1 carries wake word +
    // commands, drawing and sound; at the lowest priority there, detection
    // was starved and only caught a face if you held still.
    xTaskCreatePinnedToCore(faceTask, "face", 12 * 1024, accurate ? (void*)1 : nullptr, 3, nullptr, 0);
}

void faceTrackingSetInterval(uint32_t ms) { s_intervalMs.store(ms); }

FaceSighting faceTrackingLatest() {
    taskENTER_CRITICAL(&s_lock);
    FaceSighting f = s_latest;
    taskEXIT_CRITICAL(&s_lock);
    return f;
}
