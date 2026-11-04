#include "voice.h"

#include <atomic>

#include <M5Unified.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

#include "Companion.h"  // command phrase table

static const char* TAG = "voice";

static constexpr uint32_t kSampleRate = 16000;      // what ESP-SR expects
// The CoreS3 has two microphones. With both, ESP-SR's blind source separation
// pulls your voice apart from other sounds (TV, fan, music) and the wake word
// detector listens on both channels. Costs ~25% more of one CPU core and
// ~400 KB more PSRAM than a single mic. Set to false to compare.
static constexpr bool kUseBothMics = true;
static constexpr int kCommandWindowMs = 6000;       // time to say a command after "Hey Buddy"

static const esp_afe_sr_iface_t* s_afe = nullptr;
static esp_afe_sr_data_t* s_afeData = nullptr;
static esp_mn_iface_t* s_mn = nullptr;
static model_iface_data_t* s_mnData = nullptr;
static QueueHandle_t s_events = nullptr;

static std::atomic<float> s_levelDb{-90.0f};
static std::atomic<bool> s_speech{false};

// Pause handshake with the sound player (the mic and speaker can't run together).
static std::atomic<bool> s_feeding{false}, s_pauseReq{false}, s_paused{false};

void voicePause() { s_pauseReq.store(true); }
void voiceResume() { s_pauseReq.store(false); }
bool voicePaused() { return !s_feeding.load() || s_paused.load(); }

float voiceLevelDb() { return s_levelDb.load(); }
bool voiceSpeech() { return s_speech.load(); }

static void post(VoiceEvent::Type type, int id = 0, float prob = 0) {
    VoiceEvent ev{type, id, prob};
    xQueueSend(s_events, &ev, 0);  // drop rather than block if the UI is behind
}

// Mic -> AFE. M5Unified records asynchronously and queues up to two buffers,
// so we rotate three: only queue a new recording when a slot is free, and
// at that point the buffer queued two calls ago is complete.
static void feedTask(void*) {
    const int chunk = s_afe->get_feed_chunksize(s_afeData);
    const int channels = s_afe->get_feed_channel_num(s_afeData);
    const bool stereo = channels == 2;  // interleaved L/R, one channel per mic
    int16_t* bufs[3];
    for (auto& b : bufs) b = (int16_t*)heap_caps_calloc(chunk * channels, sizeof(int16_t), MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "feed: %d samples x %d ch per chunk", chunk, channels);

    int idx = 0, primed = 0;
    s_feeding.store(true);
    while (true) {
        if (s_pauseReq.load()) {
            // Let queued recordings finish, then park until the sound is done.
            while (M5.Mic.isRecording()) vTaskDelay(1);
            s_paused.store(true);
            while (s_pauseReq.load()) vTaskDelay(pdMS_TO_TICKS(5));
            s_paused.store(false);
            primed = 0;  // old buffers are stale
            continue;
        }
        while (M5.Mic.isRecording() >= 2) vTaskDelay(1);
        M5.Mic.record(bufs[idx], chunk * channels, kSampleRate, stereo);
        const int done = (idx + 1) % 3;  // queued two calls ago
        idx = (idx + 1) % 3;
        if (primed < 2) {
            ++primed;
            continue;
        }
        s_afe->feed(s_afeData, bufs[done]);
    }
}

// AFE -> wake word / command recognition.
static void detectTask(void*) {
    const int afeChunk = s_afe->get_fetch_chunksize(s_afeData);
    const int mnChunk = s_mn->get_samp_chunksize(s_mnData);
    if (afeChunk != mnChunk) ESP_LOGW(TAG, "chunk mismatch: afe %d vs multinet %d", afeChunk, mnChunk);

    bool awaiting = false;
    while (true) {
        afe_fetch_result_t* res = s_afe->fetch(s_afeData);
        if (!res || res->ret_value == ESP_FAIL) {
            vTaskDelay(1);
            continue;
        }
        s_levelDb.store(res->data_volume);
        s_speech.store(res->vad_state == VAD_SPEECH);

        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "wake word");
            s_mn->clean(s_mnData);
            s_afe->disable_wakenet(s_afeData);  // don't re-trigger while listening for a command
            awaiting = true;
            post(VoiceEvent::WakeWord);
            continue;
        }
        if (!awaiting) continue;

        esp_mn_state_t st = s_mn->detect(s_mnData, res->data);
        if (st == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t* r = s_mn->get_results(s_mnData);
            if (r->num > 0) {
                ESP_LOGI(TAG, "command %d \"%s\" (p=%.2f)", r->command_id[0], r->string, r->prob[0]);
                post(VoiceEvent::Command, r->command_id[0], r->prob[0]);
            } else {
                post(VoiceEvent::Timeout);
            }
        } else if (st == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGI(TAG, "no command heard");
            post(VoiceEvent::Timeout);
        } else {
            continue;
        }
        awaiting = false;
        s_afe->enable_wakenet(s_afeData);
    }
}

bool voiceMicBegin() {
    // The CoreS3 can't use the speaker and the mic at the same time in M5Unified.
    M5.Speaker.end();
    auto micCfg = M5.Mic.config();
    micCfg.sample_rate = kSampleRate;
    M5.Mic.config(micCfg);
    if (!M5.Mic.begin()) {
        ESP_LOGE(TAG, "mic init failed");
        return false;
    }
    return true;
}

// Creates the audio front-end for the given input format ("M" or "MM").
static bool createAfe(srmodel_list_t* models, const char* format) {
    afe_config_t* cfg = afe_config_init(format, models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!cfg) return false;
    cfg->wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    cfg->wakenet_init = cfg->wakenet_model_name != nullptr;
    cfg->vad_init = true;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_perferred_core = 0;
    if (!cfg->wakenet_init) ESP_LOGW(TAG, "no wake word model; voice commands disabled");
    else ESP_LOGI(TAG, "wake word model: %s", cfg->wakenet_model_name);

    s_afe = esp_afe_handle_from_config(cfg);
    s_afeData = s_afe ? s_afe->create_from_config(cfg) : nullptr;
    afe_config_free(cfg);
    if (!s_afeData) return false;
    ESP_LOGI(TAG, "audio front-end (%s, %d mic channel%s):", format,
             s_afe->get_feed_channel_num(s_afeData), format[1] ? "s" : "");
    s_afe->print_pipeline(s_afeData);  // e.g. |SE(BSS)| -> |VAD| -> |WakeNet|
    return true;
}

bool voiceStart(QueueHandle_t events,
                const std::vector<std::pair<VoiceCommand, std::string>>& extraPhrases) {
    s_events = events;

    srmodel_list_t* models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "no speech models found in the 'model' partition (flash with `idf.py flash`)");
        return false;
    }

    // Audio front-end: noise handling, voice activity detection and the wake
    // word. Prefer both mics ("MM"); fall back to one ("M") if that fails.
    if (kUseBothMics && !createAfe(models, "MM")) {
        ESP_LOGW(TAG, "dual-mic front-end failed; falling back to one mic");
    }
    if (!s_afeData && !createAfe(models, "M")) {
        ESP_LOGE(TAG, "failed to create audio front-end");
        return false;
    }

    // Command recognizer with our phrase list.
    char* mnName = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mnName) {
        ESP_LOGE(TAG, "no English MultiNet model found");
        return false;
    }
    s_mn = esp_mn_handle_from_name(mnName);
    s_mnData = s_mn->create(mnName, kCommandWindowMs);
    if (!s_mnData) {
        ESP_LOGE(TAG, "failed to create MultiNet");
        return false;
    }
    esp_mn_commands_alloc(s_mn, s_mnData);
    for (int i = 0; i < kCommandPhraseCount; ++i) {
        if (esp_mn_commands_add((int)kCommandPhrases[i].command, kCommandPhrases[i].text) != ESP_OK)
            ESP_LOGW(TAG, "could not add phrase \"%s\"", kCommandPhrases[i].text);
    }
    for (const auto& [cmd, text] : extraPhrases) {
        if (esp_mn_commands_add((int)cmd, text.c_str()) == ESP_OK)
            ESP_LOGI(TAG, "extra phrase \"%s\" -> %s", text.c_str(), commandName(cmd));
        else
            ESP_LOGW(TAG, "could not add extra phrase \"%s\"", text.c_str());
    }
    esp_mn_error_t* err = esp_mn_commands_update();
    if (err) {
        for (int i = 0; i < err->num; ++i) ESP_LOGW(TAG, "rejected phrase: %s", err->phrases[i]->string);
    }
    s_mn->print_active_speech_commands(s_mnData);
    ESP_LOGI(TAG, "MultiNet %s ready, %d phrases", mnName, kCommandPhraseCount + (int)extraPhrases.size());

    xTaskCreatePinnedToCore(feedTask, "sr_feed", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(detectTask, "sr_detect", 8192, nullptr, 5, nullptr, 0);
    return true;
}
