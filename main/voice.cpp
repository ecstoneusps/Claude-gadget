#include "voice.h"

#include <atomic>

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
#include "assistant.h"
#include "audio_io.h"
#include "esp_timer.h"

static const char* TAG = "voice";

static_assert(kAudioSampleRate == 16000, "ESP-SR expects 16 kHz audio");
static constexpr int kCommandWindowMs = 6000;       // time to say a command after "Hey Buddy"
// Conversation mode: when to stop sending what you say.
static constexpr uint32_t kStartTalkingMs = 6000;    // nothing said this long after "Hey Buddy": give up
static constexpr uint32_t kFollowUpStartMs = 5000;   // ...or after an answer
static constexpr uint32_t kEndSilenceMs = 1000;      // a pause this long ends what you're saying
static constexpr uint32_t kMaxUtteranceMs = 15000;
static constexpr uint32_t kBusyMaxMs = 180000;       // safety net if the assistant never hands back

static std::atomic<int> s_handBack{0};  // 1 = resume wake word, 2 = follow-up listening

void voiceResume() { s_handBack.store(1); }
void voiceFollowUp() { s_handBack.store(2); }

static const esp_afe_sr_iface_t* s_afe = nullptr;
static esp_afe_sr_data_t* s_afeData = nullptr;
static esp_mn_iface_t* s_mn = nullptr;
static model_iface_data_t* s_mnData = nullptr;
static QueueHandle_t s_events = nullptr;

static const char* s_format = "off";  // front-end input format, for the status line
const char* voiceFrontEnd() { return s_format; }

static std::atomic<float> s_levelDb{-90.0f};
static std::atomic<bool> s_speech{false};

float voiceLevelDb() { return s_levelDb.load(); }
bool voiceSpeech() { return s_speech.load(); }

static void post(VoiceEvent::Type type, int id = 0, float prob = 0) {
    VoiceEvent ev{type, id, prob};
    xQueueSend(s_events, &ev, 0);  // drop rather than block if the UI is behind
}

// Mics -> AFE. The duplex driver delivers both mics as interleaved stereo,
// exactly the "MM" layout the dual-mic front-end wants. In single-mic
// fallback the left mic is used on its own.
static void feedTask(void*) {
    const int chunk = s_afe->get_feed_chunksize(s_afeData);
    const int channels = s_afe->get_feed_channel_num(s_afeData);
    int16_t* stereo = (int16_t*)heap_caps_calloc(chunk * 2, sizeof(int16_t), MALLOC_CAP_DEFAULT);
    int16_t* mono = channels == 1 ? (int16_t*)heap_caps_calloc(chunk, sizeof(int16_t), MALLOC_CAP_DEFAULT) : nullptr;
    ESP_LOGI(TAG, "feed: %d samples x %d ch per chunk", chunk, channels);

    while (true) {
        if (!audioReadStereo(stereo, chunk)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (mono) {
            for (int i = 0; i < chunk; ++i) mono[i] = stereo[2 * i];
            s_afe->feed(s_afeData, mono);
        } else {
            s_afe->feed(s_afeData, stereo);
        }
    }
}

// AFE -> wake word / command recognition / streaming to the assistant.
static void detectTask(void*) {
    const int afeChunk = s_afe->get_fetch_chunksize(s_afeData);
    const int mnChunk = s_mn->get_samp_chunksize(s_mnData);
    if (afeChunk != mnChunk) ESP_LOGW(TAG, "chunk mismatch: afe %d vs multinet %d", afeChunk, mnChunk);

    enum class Mode { Idle, Command, Stream, Busy } mode = Mode::Idle;
    bool followUp = false, heard = false;
    uint32_t since = 0, lastSpeech = 0;
    auto ms = [] { return (uint32_t)(esp_timer_get_time() / 1000); };
    auto toIdle = [&] {
        mode = Mode::Idle;
        s_afe->enable_wakenet(s_afeData);
    };
    auto startStream = [&](bool isFollowUp) {
        mode = Mode::Stream;
        followUp = isFollowUp;
        heard = false;
        since = lastSpeech = ms();
    };

    while (true) {
        afe_fetch_result_t* res = s_afe->fetch(s_afeData);
        if (!res || res->ret_value == ESP_FAIL) {
            vTaskDelay(1);
            continue;
        }
        const bool speech = res->vad_state == VAD_SPEECH;
        s_levelDb.store(res->data_volume);
        s_speech.store(speech);
        const uint32_t now = ms();

        // The assistant finished a turn (it's been speaking its answer meanwhile).
        // Only taken once we're done streaming, so an early hand-back isn't lost.
        if (mode == Mode::Busy) {
            const int handBack = s_handBack.exchange(0);
            if (handBack == 1 || now - since > kBusyMaxMs) {
                toIdle();
            } else if (handBack == 2) {
                ESP_LOGI(TAG, "listening for a follow-up");
                startStream(true);
                post(VoiceEvent::FollowUp);
            }
        }

        if (res->wakeup_state == WAKENET_DETECTED) {
            s_afe->disable_wakenet(s_afeData);  // don't re-trigger while listening
            s_handBack.store(0);                // left over from an earlier turn
            if (assistantAvailable() && assistantBeginTurn(false)) {
                ESP_LOGI(TAG, "wake word; listening (AI)");
                startStream(false);
            } else {
                ESP_LOGI(TAG, "wake word; listening for a command");
                s_mn->clean(s_mnData);
                mode = Mode::Command;
            }
            post(VoiceEvent::WakeWord);
            continue;
        }

        if (mode == Mode::Stream) {
            assistantAudio(res->data, res->data_size / (int)sizeof(int16_t));
            if (speech) {
                heard = true;
                lastSpeech = now;
            }
            const bool sttDone = assistantSttDone();  // Home Assistant heard you stop
            // A voice sample for "learn my voice": more time to start, and
            // pauses while thinking don't end it.
            const bool patient = assistantWantsLongAnswer();
            const uint32_t endSilence = patient ? 2000 : kEndSilenceMs;
            const uint32_t startWindow = patient ? 10000 : followUp ? kFollowUpStartMs : kStartTalkingMs;
            const bool done = sttDone || (heard && now - lastSpeech > endSilence) ||
                              (!heard && now - since > startWindow) || now - since > kMaxUtteranceMs;
            if (done) {
                const bool spoke = heard || sttDone;
                assistantEndAudio(spoke);
                // A voice sample with nothing in it: the assistant asks again,
                // so wait for it to hand back (as after an answer).
                if (spoke || patient) {
                    mode = Mode::Busy;
                    since = now;
                } else {
                    ESP_LOGI(TAG, "%s", followUp ? "no follow-up" : "nothing said");
                    post(followUp ? VoiceEvent::FollowUpEnd : VoiceEvent::Timeout);
                    toIdle();
                }
            }
            continue;
        }

        if (mode != Mode::Command) continue;

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
        toIdle();
    }
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
    s_format = format;
    ESP_LOGI(TAG, "audio front-end (%s, %d mic channel%s):", format,
             s_afe->get_feed_channel_num(s_afeData), format[1] ? "s" : "");
    s_afe->print_pipeline(s_afeData);  // e.g. |SE(BSS)| -> |VAD| -> |WakeNet|
    return true;
}

bool voiceStart(QueueHandle_t events,
                const std::vector<std::pair<VoiceCommand, std::string>>& extraPhrases, bool twoMics,
                int detectCore) {
    s_events = events;

    srmodel_list_t* models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "no speech models found in the 'model' partition (flash with `idf.py flash`)");
        return false;
    }

    // Audio front-end: noise handling, voice activity detection and the wake
    // word. Prefer both mics ("MM"); fall back to one ("M") if that fails.
    // Two mics ("MM") add blind source separation and run the wake word on
    // both channels. On the CoreS3's quad PSRAM that measured far heavier than
    // Espressif's benchmarks (it saturated a core and slowed the UI to 5 fps),
    // so one mic is the default; settings.txt `two_microphones = on` to compare.
    if (twoMics && !createAfe(models, "MM")) {
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

    // Single-mic speech fits on core 0 next to face detection (lower
    // priority), leaving core 1 to drawing, sound and the camera driver.
    // Two-mic mode is too heavy for one core next to face detection, so it
    // splits across both then; without face tracking core 0 has room.
    xTaskCreatePinnedToCore(feedTask, "sr_feed", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(detectTask, "sr_detect", 8192, nullptr, 5, nullptr, twoMics ? detectCore : 0);
    ESP_LOGI(TAG, "recognizer on core %d", twoMics ? detectCore : 0);
    return true;
}
