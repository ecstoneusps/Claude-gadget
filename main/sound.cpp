#include "sound.h"

#include <atomic>
#include <math.h>

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

static std::atomic<int16_t*> s_speech{nullptr};
static std::atomic<size_t> s_speechLen{0};
static std::atomic<bool> s_speaking{false};
static std::atomic<bool> s_stopSpeech{false};
static std::atomic<float> s_speechLevel{0};

// Loudness of a chunk mapped to 0..1 mouth opening (-42 dBFS closed, -12 wide open).
static float chunkLevel(const int16_t* p, int n) {
    double sum = 0;
    for (int i = 0; i < n; ++i) sum += (double)p[i] * p[i];
    const double rms = sqrt(sum / (n > 0 ? n : 1)) / 32768.0;
    const float db = rms > 1e-6 ? 20.0f * log10f((float)rms) : -120.0f;
    const float l = (db + 42.0f) / 30.0f;
    return l < 0 ? 0 : l > 1 ? 1 : l;
}

static bool playSpeech() {
    constexpr int kChunk = 256;
    int16_t* pcm = s_speech.exchange(nullptr);
    if (!pcm) return false;
    s_stopSpeech.store(false);  // a stop meant for an earlier reply
    size_t left = s_speechLen.load();
    s_speaking.store(true);
    s_playing.store(true);
    const int16_t* p = pcm;
    while (left > 0 && !s_stopSpeech.load()) {
        const int n = left < (size_t)kChunk ? (int)left : kChunk;
        s_speechLevel.store(chunkLevel(p, n));
        audioWrite(p, n);
        p += n;
        left -= n;
        s_lastSoundUs.store(esp_timer_get_time());
    }
    heap_caps_free(pcm);
    s_speechLevel.store(0);
    s_speaking.store(false);
    return true;
}

// Streams the requested sound in short chunks, so a newer request cuts in
// within a few milliseconds instead of waiting for the old sound to end.
static void playerTask(void*) {
    constexpr int kChunk = 256;  // 16 ms
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (playSpeech()) s_request.store((int)Sfx::None);  // effects asked for during speech are dropped
        int req;
        while ((req = s_request.exchange((int)Sfx::None)) != (int)Sfx::None) {
            s_playing.store(true);
            const int16_t* pcm = s_pcm[req];
            size_t left = s_len[req];
            while (left > 0 && s_request.load() == (int)Sfx::None && !s_speech.load()) {
                const int n = left < (size_t)kChunk ? (int)left : kChunk;
                audioWrite(pcm, n);
                pcm += n;
                left -= n;
                s_lastSoundUs.store(esp_timer_get_time());
            }
        }
        playSpeech();  // a reply that arrived during an effect
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
    if (s == Sfx::None || s_len[(int)s] == 0 || !s_player || s_speaking.load() || s_speech.load()) return;
    s_request.store((int)s);
    xTaskNotifyGive(s_player);
}

bool soundHeardRecently(uint32_t tailMs) {
    return s_playing.load() || esp_timer_get_time() - s_lastSoundUs.load() < (int64_t)tailMs * 1000;
}

void soundSpeak(int16_t* pcm, size_t samples) {
    if (!s_player || !pcm || samples == 0) {
        heap_caps_free(pcm);
        return;
    }
    s_speechLen.store(samples);
    int16_t* old = s_speech.exchange(pcm);
    if (old) heap_caps_free(old);  // an unplayed earlier reply
    s_speaking.store(true);        // from now on, so callers waiting for the end don't miss it
    xTaskNotifyGive(s_player);
}

void soundStopSpeech() {
    int16_t* pending = s_speech.exchange(nullptr);
    if (pending) {
        heap_caps_free(pending);
        s_speaking.store(false);
    }
    if (s_speaking.load()) s_stopSpeech.store(true);
}

bool soundSpeaking() { return s_speaking.load() || s_speech.load() != nullptr; }
float soundSpeechLevel() { return s_speechLevel.load(); }
