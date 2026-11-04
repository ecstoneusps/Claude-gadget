#include "sound.h"

#include <M5Unified.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "voice.h"

static const char* TAG = "sound";

static uint8_t s_volume = 160;                   // 0..255, set from settings.txt
static constexpr uint32_t kPauseTimeoutMs = 300; // don't wait forever for the mic to park

static int16_t* s_pcm[(int)Sfx::Count];
static size_t s_len[(int)Sfx::Count];

enum class State { Idle, WaitMic, Playing };
static State s_state = State::Idle;
static Sfx s_pending = Sfx::None;
static uint32_t s_since = 0;

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
    M5.Speaker.setVolume(s_volume);
    ESP_LOGI(TAG, "rendered %d effects, %u KB", (int)Sfx::Count - 1, (unsigned)(total / 1024));
}

void soundSetVolume(uint8_t volume) {
    s_volume = volume;
    M5.Speaker.setVolume(volume);
}

void soundPlay(Sfx s) {
    if (s != Sfx::None && s_len[(int)s] > 0) s_pending = s;
}

static void startPending(uint32_t now) {
    const int i = (int)s_pending;
    s_pending = Sfx::None;
    // stop_current_sound = true: a new reaction interrupts the old one.
    M5.Speaker.playRaw(s_pcm[i], s_len[i], kSfxSampleRate, false, 1, 0, true);
    s_since = now;
}

void soundUpdate(uint32_t now) {
    switch (s_state) {
        case State::Idle:
            if (s_pending == Sfx::None) return;
            voicePause();
            s_state = State::WaitMic;
            s_since = now;
            [[fallthrough]];

        case State::WaitMic:
            if (!voicePaused() && now - s_since < kPauseTimeoutMs) return;
            M5.Mic.end();
            M5.Speaker.begin();
            startPending(now);
            s_state = State::Playing;
            return;

        case State::Playing:
            if (s_pending != Sfx::None) {
                startPending(now);
                return;
            }
            if (M5.Speaker.isPlaying() || now - s_since < 50) return;
            M5.Speaker.end();
            M5.Mic.begin();
            voiceResume();
            s_state = State::Idle;
            return;
    }
}
