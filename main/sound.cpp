#include "sound.h"

#include <atomic>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_io.h"

static const char* TAG = "sound";

static_assert(kSfxSampleRate == kAudioSampleRate, "effects must be rendered at the I2S rate");

static int16_t* s_pcm[(int)Sfx::Count];
static size_t s_len[(int)Sfx::Count];

static TaskHandle_t s_player = nullptr;
static std::atomic<int> s_request{(int)Sfx::None};
static std::atomic<bool> s_playing{false};
static std::atomic<int64_t> s_lastSoundUs{0};

// Streams the requested sound in short chunks, so a newer request cuts in
// within a few milliseconds instead of waiting for the old sound to end.
static void playerTask(void*) {
    constexpr int kChunk = 256;  // 16 ms
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int req;
        while ((req = s_request.exchange((int)Sfx::None)) != (int)Sfx::None) {
            s_playing.store(true);
            const int16_t* pcm = s_pcm[req];
            size_t left = s_len[req];
            while (left > 0 && s_request.load() == (int)Sfx::None) {
                const int n = left < (size_t)kChunk ? (int)left : kChunk;
                audioWrite(pcm, n);
                pcm += n;
                left -= n;
                s_lastSoundUs.store(esp_timer_get_time());
            }
        }
        s_playing.store(false);
        s_lastSoundUs.store(esp_timer_get_time());
    }
}

void soundInit() {
    size_t total = 0;
    for (int i = 1; i < (int)Sfx::Count; ++i) {
        const Sfx s = (Sfx)i;
        s_len[i] = sfxSamples(s);
        s_pcm[i] = (int16_t*)heap_caps_malloc(s_len[i] * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!s_pcm[i]) {
            ESP_LOGE(TAG, "out of memory rendering %s", sfxName(s));
            s_len[i] = 0;
            continue;
        }
        sfxRender(s, s_pcm[i]);
        total += s_len[i] * sizeof(int16_t);
    }
    // Core 1 next to the UI; it mostly waits on the I2S DMA.
    xTaskCreatePinnedToCore(playerTask, "sound", 3072, nullptr, 5, &s_player, 1);
    ESP_LOGI(TAG, "rendered %d effects, %u KB", (int)Sfx::Count - 1, (unsigned)(total / 1024));
}

void soundSetVolume(int percent) { audioSetVolume(percent); }

void soundPlay(Sfx s) {
    if (s == Sfx::None || s_len[(int)s] == 0 || !s_player) return;
    s_request.store((int)s);
    xTaskNotifyGive(s_player);
}

bool soundHeardRecently(uint32_t tailMs) {
    return s_playing.load() || esp_timer_get_time() - s_lastSoundUs.load() < (int64_t)tailMs * 1000;
}
