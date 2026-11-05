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
        ESP_LOGI(TAG, "camera ready (sensor PID 0x%02x)", s->id.PID);
    }
    s_cameraOk = true;
    return true;
}

static void faceTask(void*) {
    HumanFaceDetect* detector = new HumanFaceDetect();
    ESP_LOGI(TAG, "face detection model loaded");
    uint32_t seq = 0, logged = 0;

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
        if (s_preview && fb->len >= (size_t)kCamW * kCamH * 2 &&
            xSemaphoreTake(s_previewLock, pdMS_TO_TICKS(20)) == pdTRUE) {
            memcpy(s_preview, fb->buf, kCamW * kCamH * 2);
            ++s_previewSeq;
            xSemaphoreGive(s_previewLock);
        }
        // Every ~10 s, a status line whether or not a face was found, so a
        // camera that runs but never sees anyone can be told from one that
        // isn't running. Brightness is the frame's mean green channel (0-63)
        // read both ways round: the plausible-looking one shows the byte order.
        if (seq % 50 == 1) {
            const uint8_t* px = fb->buf;
            const size_t n = fb->width * fb->height;
            uint32_t sumBE = 0, sumLE = 0;
            for (size_t i = 0; i < n; i += 97) {
                const uint16_t be = (uint16_t)(px[2 * i] << 8 | px[2 * i + 1]);
                const uint16_t le = (uint16_t)(px[2 * i + 1] << 8 | px[2 * i]);
                sumBE += (be >> 5) & 0x3F;
                sumLE += (le >> 5) & 0x3F;
            }
            const size_t samples = (n + 96) / 97;
            ESP_LOGI(TAG, "frame %ux%u, %u bytes, detect %d ms, %d face(s); green BE %u / LE %u",
                     fb->width, fb->height, (unsigned)fb->len, detectMs, (int)results.size(),
                     (unsigned)(sumBE / samples), (unsigned)(sumLE / samples));
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

        const TickType_t elapsed = xTaskGetTickCount() - start;
        const TickType_t wait = pdMS_TO_TICKS(interval);
        vTaskDelay(elapsed < wait ? wait - elapsed : 1);
    }
}

void faceTrackingStart() {
    if (!s_cameraOk) return;
    // Core 0, below the mic front-end (priority 5). Core 1 carries wake word +
    // commands, drawing and sound; at the lowest priority there, detection
    // was starved and only caught a face if you held still.
    xTaskCreatePinnedToCore(faceTask, "face", 12 * 1024, nullptr, 3, nullptr, 0);
}

void faceTrackingSetInterval(uint32_t ms) { s_intervalMs.store(ms); }

FaceSighting faceTrackingLatest() {
    taskENTER_CRITICAL(&s_lock);
    FaceSighting f = s_latest;
    taskEXIT_CRITICAL(&s_lock);
    return f;
}
