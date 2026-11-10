#include "assistant.h"

#include <atomic>
#include <string.h>
#include <vector>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "mbedtls/base64.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#include "AssistText.h"
#include "Wav.h"
#include "audio_io.h"
#include "cpustats.h"
#include "memory.h"
#include "Memory.h"
#include "sound.h"
#include "voice.h"
#include "wifi.h"

static const char* TAG = "assistant";

static constexpr int kSampleRate = 16000;
static constexpr uint32_t kRunStartMs = 8000;      // Home Assistant to accept the audio
static constexpr uint32_t kSttMs = 20000;          // speech-to-text after you stop talking
static constexpr uint32_t kIntentMs = 90000;       // the AI's answer (a cold model can take a while)
static constexpr uint32_t kConversationMs = 300000;  // keep the same conversation (memory) this long

static AssistantConfig s_cfg;
static QueueHandle_t s_events = nullptr;
static QueueHandle_t s_inbox = nullptr;  // std::string* JSON messages from the websocket task
static StreamBufferHandle_t s_audio = nullptr;
static esp_websocket_client_handle_t s_ws = nullptr;
static TaskHandle_t s_task = nullptr;

static std::atomic<bool> s_ready{false};       // logged in and the voice assistant is known
static std::atomic<bool> s_turnActive{false};
static std::atomic<bool> s_turnPending{false};
static std::atomic<bool> s_followUpTurn{false};
static std::atomic<bool> s_accepting{false};   // voice.cpp may push audio
static std::atomic<bool> s_audioEnded{false};
static std::atomic<bool> s_heardSpeech{false};
static std::atomic<bool> s_sttDone{false};
static std::atomic<const char*> s_status{"off"};

// "What do you see": the photo waiting to be looked at (main loop -> assistant task).
static uint8_t* s_lookJpeg = nullptr;
static size_t s_lookLen = 0;
static std::string s_lookQuestion;
static bool s_lookFromConversation = false;
static std::string s_lookMarker;  // posted to the inbox (compared by address)
// Home Assistant's address for the Wi-Fi network it's on: ha_url at home (the
// first network), ha_url_away on any other one (if set).
static std::string s_baseUrl;
static int s_baseNet = -2;
static bool isAway(int net) { return net > 0 || (net == 0 && !s_cfg.firstNetworkIsHome); }
static std::string baseUrlFor(int net) { return (isAway(net) && !s_cfg.urlAway.empty()) ? s_cfg.urlAway : s_cfg.url; }
static bool awayFromHome() { return isAway(wifiNetworkIndex()); }
static std::string s_sayMarker;   // likewise: say s_sayText out loud
static std::string s_sayText;
// Assistant task only: the last photo and what was said about it, so
// follow-up questions ("what colour is it?") can go back to the vision model.
static std::string s_visionImage;  // base64 JPEG
static std::vector<std::pair<std::string, std::string>> s_visionChat;  // (role, text)
static uint32_t s_visionUntil = 0;
static constexpr uint32_t kVisionFollowUpMs = 60000;

// Websocket task only.
static std::string s_rx;
// Posted to the inbox when a connection opens (compared by address).
static std::string s_connectedMarker;
// Assistant task only: the token went out on this connection.
static bool s_authSent = false;

// Assistant task only.
static int s_nextId = 1;
static int s_listId = 0;
static std::string s_pipelineId, s_ttsEngine, s_ttsLanguage, s_ttsVoice;
static std::string s_conversationId;
static uint32_t s_conversationUntil = 0;
static bool s_tokenRejected = false;

enum class Stage { Idle, WaitRunStart, Streaming, WaitStt, WaitIntent, WaitVision };
static Stage s_stage = Stage::Idle;
static int s_runId = 0;
static int s_handlerId = -1;
static uint32_t s_deadline = 0;

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// cJSON makes many small allocations; keep them out of the scarce internal RAM.
static void* psramMalloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void psramFree(void* p) { heap_caps_free(p); }

static void post(AssistEvent::Type type, const char* text = "", const char* emotion = "", int command = 0) {
    AssistEvent ev{};
    ev.type = type;
    ev.followUp = s_followUpTurn.load();
    ev.command = command;
    strncpy(ev.emotion, emotion, sizeof ev.emotion - 1);
    strncpy(ev.text, text, sizeof ev.text - 1);
    xQueueSend(s_events, &ev, pdMS_TO_TICKS(100));
}

static const char* jstr(const cJSON* obj, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : nullptr;
}

static bool sendJson(cJSON* msg, int timeoutMs = 3000) {
    char* text = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!text) return false;
    const int len = strlen(text);
    const bool connected = esp_websocket_client_is_connected(s_ws);
    const int sent = connected ? esp_websocket_client_send_text(s_ws, text, len, pdMS_TO_TICKS(timeoutMs)) : -1;
    cJSON_free(text);
    if (sent != len)
        ESP_LOGW(TAG, "send failed (%d of %d bytes%s)", sent, len, connected ? "" : ", not connected");
    return sent == len;
}

// ---------------------------------------------------------------------------
// Websocket task: collect each message (it may arrive in pieces) and hand it over.

static void onWsEvent(void*, esp_event_base_t, int32_t id, void* data) {
    const auto* d = (esp_websocket_event_data_t*)data;
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "connected to Home Assistant");
            s_status.store("logging in");
            {
                std::string* m = &s_connectedMarker;
                xQueueSend(s_inbox, &m, pdMS_TO_TICKS(200));
            }
            break;
        case WEBSOCKET_EVENT_CLOSED:  // Home Assistant closed it (e.g. no login in time)
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_ERROR:
            s_ready.store(false);
            if (id == WEBSOCKET_EVENT_CLOSED)
                ESP_LOGW(TAG, "Home Assistant closed the connection (code %d); reconnecting", d->close_status_code);
            else if (id == WEBSOCKET_EVENT_DISCONNECTED)
                ESP_LOGW(TAG, "lost the connection to Home Assistant (error type %d, HTTP %d, socket errno %d); reconnecting",
                         (int)d->error_handle.error_type, d->error_handle.esp_ws_handshake_status_code,
                         d->error_handle.esp_transport_sock_errno);
            if (strcmp(s_status.load(), "token rejected") != 0) s_status.store("connecting");
            {
                std::string* none = nullptr;  // tells the assistant task to abort a turn in progress
                xQueueSend(s_inbox, &none, 0);
            }
            break;
        case WEBSOCKET_EVENT_DATA:
            if (d->op_code != 0x1 && d->op_code != 0x0) {  // text frames only
                if (!s_ready.load() && d->op_code != 0x9 && d->op_code != 0xA)
                    ESP_LOGI(TAG, "ignoring frame type %d (%d bytes) while logging in", d->op_code, d->data_len);
                break;
            }
            if (d->payload_offset == 0) s_rx.clear();
            s_rx.append(d->data_ptr, d->data_len);
            if (d->payload_offset + d->data_len >= d->payload_len) {
                auto* msg = new std::string(std::move(s_rx));
                s_rx.clear();
                if (xQueueSend(s_inbox, &msg, pdMS_TO_TICKS(200)) != pdTRUE) {
                    ESP_LOGW(TAG, "dropped a message (busy)");
                    delete msg;
                }
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Turn handling (assistant task).

static void finishTurn() {
    s_accepting.store(false);
    s_stage = Stage::Idle;
    s_runId = 0;
    s_handlerId = -1;
    xStreamBufferReset(s_audio);
    s_turnPending.store(false);
    s_turnActive.store(false);
}

// The turn ended without speech to play: let the wake word listen again.
static void endTurnAndResume() {
    finishTurn();
    voiceResume();
}

static void failTurn(const char* what) {
    ESP_LOGW(TAG, "%s", what);
    post(AssistEvent::Error, what);
    endTurnAndResume();
}

static void startSttRun() {
    s_runId = s_nextId++;
    cJSON* m = cJSON_CreateObject();
    cJSON_AddNumberToObject(m, "id", s_runId);
    cJSON_AddStringToObject(m, "type", "assist_pipeline/run");
    cJSON_AddStringToObject(m, "start_stage", "stt");
    cJSON_AddStringToObject(m, "end_stage", "stt");
    cJSON* in = cJSON_AddObjectToObject(m, "input");
    cJSON_AddNumberToObject(in, "sample_rate", kSampleRate);
    if (!s_pipelineId.empty()) cJSON_AddStringToObject(m, "pipeline", s_pipelineId.c_str());
    s_handlerId = -1;
    s_stage = Stage::WaitRunStart;
    s_deadline = nowMs() + kRunStartMs;
    if (!sendJson(m)) failTurn("couldn't send to Home Assistant");
}

// The notes changed since the AI last saw them (it sees them at the start of
// a conversation; after a change, again with the next question).
static bool s_notesChanged = false;

static void startIntentRun(const std::string& said) {
    const bool newConversation = s_conversationId.empty() || (int32_t)(s_conversationUntil - nowMs()) <= 0;
    std::string text = said;
    if (s_cfg.memory && (newConversation || s_notesChanged)) {
        text = memoryPromptBlock() + said;
        s_notesChanged = false;
    }
    s_runId = s_nextId++;
    cJSON* m = cJSON_CreateObject();
    cJSON_AddNumberToObject(m, "id", s_runId);
    cJSON_AddStringToObject(m, "type", "assist_pipeline/run");
    cJSON_AddStringToObject(m, "start_stage", "intent");
    cJSON_AddStringToObject(m, "end_stage", "intent");
    cJSON* in = cJSON_AddObjectToObject(m, "input");
    cJSON_AddStringToObject(in, "text", text.c_str());
    if (!s_pipelineId.empty()) cJSON_AddStringToObject(m, "pipeline", s_pipelineId.c_str());
    if (!s_conversationId.empty() && (int32_t)(s_conversationUntil - nowMs()) > 0)
        cJSON_AddStringToObject(m, "conversation_id", s_conversationId.c_str());
    s_stage = Stage::WaitIntent;
    s_deadline = nowMs() + kIntentMs;
    if (!sendJson(m)) failTurn("couldn't send to Home Assistant");
}

// Sends buffered microphone audio: one byte of handler ID, then 16-bit PCM.
static void pumpAudio() {
    static uint8_t* buf = (uint8_t*)heap_caps_malloc(1 + 2048, MALLOC_CAP_SPIRAM);
    if (!buf) return;
    buf[0] = (uint8_t)s_handlerId;
    for (int i = 0; i < 8; ++i) {  // at most ~0.5 s per pass, so events keep flowing
        const size_t n = xStreamBufferReceive(s_audio, buf + 1, 2048, 0);
        if (n == 0) break;
        esp_websocket_client_send_bin(s_ws, (const char*)buf, (int)n + 1, pdMS_TO_TICKS(2000));
    }
    const bool ended = s_audioEnded.load() || s_sttDone.load();
    if (ended && xStreamBufferIsEmpty(s_audio)) {
        esp_websocket_client_send_bin(s_ws, (const char*)buf, 1, pdMS_TO_TICKS(2000));  // end of audio
        s_accepting.store(false);
        if (!s_heardSpeech.load() && !s_sttDone.load()) {
            // Nothing was said; voice.cpp already told the UI and went back to the wake word.
            finishTurn();
            return;
        }
        s_stage = Stage::WaitStt;
        s_deadline = nowMs() + kSttMs;
    }
}

// Downloads the reply as 16 kHz mono WAV and starts playing it. Returns false on failure.
static bool speak(const std::string& text) {
    static CpuWindow cpu;
    cpu.mark();
    const int64_t t0 = esp_timer_get_time();
    int64_t tUrl = 0, tFirst = 0, tDone = 0;
    (void)tDone;
    if (s_ttsEngine.empty()) {
        ESP_LOGW(TAG, "the voice assistant has no text-to-speech set up");
        return false;
    }
    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "engine_id", s_ttsEngine.c_str());
    cJSON_AddStringToObject(req, "message", text.c_str());
    if (!s_ttsLanguage.empty()) cJSON_AddStringToObject(req, "language", s_ttsLanguage.c_str());
    cJSON* opt = cJSON_AddObjectToObject(req, "options");
    if (!s_ttsVoice.empty()) cJSON_AddStringToObject(opt, "voice", s_ttsVoice.c_str());
    cJSON_AddStringToObject(opt, "preferred_format", "wav");
    cJSON_AddNumberToObject(opt, "preferred_sample_rate", kSampleRate);
    cJSON_AddNumberToObject(opt, "preferred_sample_channels", 1);
    cJSON_AddNumberToObject(opt, "preferred_sample_bytes", 2);
    char* body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return false;

    const std::string auth = "Bearer " + s_cfg.token;
    std::string url = s_baseUrl + "/api/tts_get_url";
    esp_http_client_config_t hc = {};
    hc.url = url.c_str();
    hc.method = HTTP_METHOD_POST;
    hc.timeout_ms = 30000;
    hc.buffer_size = 2048;
    hc.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t http = esp_http_client_init(&hc);
    esp_http_client_set_header(http, "Authorization", auth.c_str());
    esp_http_client_set_header(http, "Content-Type", "application/json");

    std::string resp;
    bool ok = false;
    const int bodyLen = strlen(body);
    if (esp_http_client_open(http, bodyLen) == ESP_OK &&
        esp_http_client_write(http, body, bodyLen) == bodyLen) {
        esp_http_client_fetch_headers(http);
        char chunk[512];
        int n;
        while ((n = esp_http_client_read(http, chunk, sizeof chunk)) > 0 && resp.size() < 8192) resp.append(chunk, n);
        const int status = esp_http_client_get_status_code(http);
        ok = status == 200;
        if (!ok) ESP_LOGW(TAG, "text-to-speech request failed: HTTP %d %.120s", status, resp.c_str());
    } else {
        ESP_LOGW(TAG, "couldn't reach %s", s_baseUrl.c_str());
    }
    cJSON_free(body);
    esp_http_client_cleanup(http);
    if (!ok) return false;

    cJSON* j = cJSON_Parse(resp.c_str());
    const char* path = j ? jstr(j, "path") : nullptr;
    const char* full = j ? jstr(j, "url") : nullptr;
    if (path) url = s_baseUrl + path;
    else if (full) url = full;
    else url.clear();
    cJSON_Delete(j);
    if (url.empty()) {
        ESP_LOGW(TAG, "text-to-speech gave no audio address");
        return false;
    }

    // Fetch the audio and play it as it arrives. Home Assistant streams it
    // while Piper speaks, but sometimes takes far longer to mark the end than
    // to send the sound (30+ s seen on hardware), so once everything that came
    // has been played and nothing new arrives for a couple of seconds, the
    // reply counts as finished.
    tUrl = esp_timer_get_time();
    hc = {};
    hc.url = url.c_str();
    hc.timeout_ms = 15000;  // for the first byte
    hc.buffer_size = 4096;
    hc.crt_bundle_attach = esp_crt_bundle_attach;
    http = esp_http_client_init(&hc);
    if (esp_http_client_open(http, 0) != ESP_OK || esp_http_client_fetch_headers(http) < 0 ||
        esp_http_client_get_status_code(http) != 200) {
        ESP_LOGW(TAG, "speech download failed (HTTP %d)", esp_http_client_get_status_code(http));
        esp_http_client_cleanup(http);
        return false;
    }
    tFirst = esp_timer_get_time();
    constexpr int kPollMs = 250;
    constexpr int kQuietEndMs = 2000;
    esp_http_client_set_timeout_ms(http, kPollMs);

    static uint8_t* buf = (uint8_t*)heap_caps_malloc(4096 + 2, MALLOC_CAP_SPIRAM);
    std::vector<uint8_t> head;  // until the WAV header has been read
    WavInfo info;
    bool streaming = false;
    SpeechShaper shaper(kSampleRate);
    uint8_t carry = 0;
    bool haveCarry = false;  // an odd byte left over between reads
    size_t total = 0, samplesOut = 0;
    int quietMs = 0;
    const char* ending = "finished";
    ok = buf != nullptr;
    auto play = [&](const uint8_t* data, size_t len) {
        // Whole 16-bit samples only; keep an odd byte for next time.
        uint8_t* p = buf;
        size_t n = 0;
        if (haveCarry && len > 0) {
            p[n++] = carry;
            haveCarry = false;
        }
        memmove(p + n, data, len);
        n += len;
        if (n & 1) {
            carry = p[n - 1];
            haveCarry = true;
            --n;
        }
        int16_t* pcm = (int16_t*)p;
        shaper.process(pcm, n / 2);
        soundSpeechWrite(pcm, n / 2);
        samplesOut += n / 2;
    };

    while (ok) {
        const int n = esp_http_client_read(http, (char*)buf, 4096);
        if (n > 0) {
            total += n;
            quietMs = 0;
            if (streaming) {
                play(buf, n);
                continue;
            }
            head.insert(head.end(), buf, buf + n);
            if (!parseWav(head.data(), head.size(), info)) {
                if (head.size() > 65536) {
                    ESP_LOGW(TAG, "speech isn't a 16-bit WAV (starts %02x %02x %02x %02x)", head[0], head[1],
                             head[2], head[3]);
                    ok = false;
                }
                continue;
            }
            if (info.sampleRate != kSampleRate || info.channels != 1) {
                ESP_LOGW(TAG, "speech is %d Hz, %d channels; expected 16000 Hz mono", info.sampleRate,
                         info.channels);
                ok = false;
                break;
            }
            streaming = true;
            soundSpeechBegin();
            play(head.data() + info.dataOffset, head.size() - info.dataOffset);
            std::vector<uint8_t>().swap(head);
        } else if (n == 0) {
            break;  // Home Assistant marked the end
        } else if (n == -ESP_ERR_HTTP_EAGAIN) {
            quietMs += kPollMs;
            if (streaming && quietMs >= kQuietEndMs && soundSpeechQueued() == 0) {
                ending = "nothing new for 2 s";
                break;
            }
            if (quietMs >= 30000) {
                ending = "timed out";
                break;
            }
        } else {
            ending = "connection error";
            break;
        }
    }
    esp_http_client_cleanup(http);
    if (streaming) soundSpeechEnd();
    tDone = esp_timer_get_time();
    if (!streaming) {
        if (ok) ESP_LOGW(TAG, "no speech audio received");
        return false;
    }
    ESP_LOGI(TAG, "spoke %.1f s; audio address %d ms, first byte %d ms, then %d ms of streaming (%u KB, ended: %s)",
             samplesOut / (float)kSampleRate, (int)((tUrl - t0) / 1000), (int)((tFirst - tUrl) / 1000),
             (int)((tDone - tFirst) / 1000), (unsigned)(total / 1024), ending);
    cpu.log("cpu while fetching the reply");
    return true;
}

static std::string askVision(const std::string& question);
static void startVision(const std::string& question, bool micsWaiting);
static void deliverReply(const std::string& reply, bool micsWaiting);

// Remembers or forgets a note and says so (in Buddy's voice, with a face).
static void handleMemory(const MemoryCommand& mc) {
    std::string removed, reply;
    switch (mc.type) {
        case MemoryCommand::Remember:
            reply = memoryRemember(mc.fact) == MemoryResult::Added
                        ? "[happy] Got it! I'll remember that " + toSecondPerson(mc.fact) + "."
                        : "[winking] I already knew that, " + toSecondPerson(mc.fact) + "!";
            break;
        case MemoryCommand::ForgetLast:
            reply = memoryForgetLast(&removed) == MemoryResult::Removed
                        ? "[thinking] Okay, I forgot that " + toSecondPerson(removed) + "."
                        : "[confused] There's nothing to forget yet!";
            break;
        case MemoryCommand::Forget:
            switch (memoryForget(mc.fact, &removed)) {
                case MemoryResult::Removed: reply = "[thinking] Okay, I forgot that " + toSecondPerson(removed) + "."; break;
                case MemoryResult::Empty: reply = "[confused] I don't have anything written down yet!"; break;
                default: reply = "[confused] Hmm, I don't remember anything like that."; break;
            }
            break;
        case MemoryCommand::ForgetAll:
            reply = memoryForgetAll() == MemoryResult::Cleared
                        ? "[sad] Okay, my notes are wiped clean. I kept a backup on the SD card, just in case."
                        : "[confused] My notes are already empty!";
            break;
        default:
            break;
    }
    s_notesChanged = true;
    ESP_LOGI(TAG, "-> notes: %u", (unsigned)memoryCount());
    deliverReply(reply, true);
}

// You said something: run it as a built-in command, or ask the AI.
static void handleHeard(const std::string& text) {
    ESP_LOGI(TAG, "heard: \"%s\"", text.c_str());
    if (normalizeSpeech(text).empty()) {
        post(AssistEvent::NotHeard);
        endTurnAndResume();
        return;
    }
    // Just the wake word (said while it was already listening for a
    // follow-up): treat it as one and listen for the actual question.
    const std::string said = normalizeSpeech(text);
    if (said == "hey buddy" || said == "hi buddy" || said == "buddy" || said == "okay buddy") {
        ESP_LOGI(TAG, "-> that was the wake word; listening again");
        finishTurn();
        post(AssistEvent::WakeAgain);
        if (assistantBeginTurn(false)) voiceFollowUp();
        else voiceResume();
        return;
    }
    // "Remember that ...", "forget that": notes on the SD card.
    if (s_cfg.memory) {
        const MemoryCommand mc = parseMemoryCommand(text);
        if (mc.type != MemoryCommand::None) {
            handleMemory(mc);
            return;
        }
    }
    // Asking about what it sees: a fresh photo (the main loop takes it and
    // calls assistantLook). The microphones keep waiting meanwhile.
    if (!s_cfg.visionUrl.empty() && isVisionRequest(text)) {
        ESP_LOGI(TAG, "-> taking a look");
        finishTurn();
        s_turnActive.store(true);  // until the photo arrives
        post(AssistEvent::Look, text.c_str());
        return;
    }
    if (isPhotoRequest(text)) {
        ESP_LOGI(TAG, "-> built-in command %s", commandName(VoiceCommand::TakePicture));
        post(AssistEvent::Command, "", "", (int)VoiceCommand::TakePicture);
        endTurnAndResume();
        return;
    }
    auto local = [&](VoiceCommand cmd, const std::string& phrase) {
        if (!isActionCommand(cmd) || !speechMatchesPhrase(text, phrase)) return false;
        ESP_LOGI(TAG, "-> built-in command %s", commandName(cmd));
        post(AssistEvent::Command, "", "", (int)cmd);
        endTurnAndResume();
        return true;
    };
    for (int i = 0; i < kCommandPhraseCount; ++i)
        if (local(kCommandPhrases[i].command, kCommandPhrases[i].text)) return;
    for (const auto& [cmd, phrase] : s_cfg.extraPhrases)
        if (local(cmd, phrase)) return;
    // A follow-up right after it described a photo: ask the vision model
    // again, with the same photo. (A new "Hey Buddy" goes to the chat AI.)
    if (s_followUpTurn.load() && !s_visionImage.empty() && (int32_t)(s_visionUntil - nowMs()) > 0) {
        finishTurn();
        s_turnActive.store(true);
        post(AssistEvent::Thinking);
        startVision(text, true);
        return;
    }
    s_visionImage.clear();  // moved on from the photo
    s_visionChat.clear();
    startIntentRun(text);
}

// Shows and speaks a reply ("[happy] Sure! ..."), then hands the microphones
// back: listening for a follow-up, or waiting for "Hey Buddy" again.
// `micsWaiting`: voice.cpp is waiting for this turn to finish.
static void deliverReply(const std::string& reply, bool micsWaiting) {
    const ReplyParts parts = splitReply(reply);
    ESP_LOGI(TAG, "reply [%s]: \"%s\"", parts.emotion.c_str(), parts.speech.c_str());
    finishTurn();
    s_turnActive.store(true);  // still busy until it has spoken

    post(AssistEvent::Reply, "", parts.emotion.c_str());
    bool spoke = false;
    if (!parts.speech.empty()) {
        spoke = s_ready.load() && speak(parts.speech);
        if (!spoke) post(AssistEvent::Error, "couldn't get the spoken reply");
    }
    if (spoke) {
        while (soundSpeaking()) vTaskDelay(pdMS_TO_TICKS(20));
        vTaskDelay(pdMS_TO_TICKS(250));  // let the speaker's echo die away before listening
    }
    if (spoke || parts.speech.empty()) post(AssistEvent::Done);  // (a failure already showed confusion)
    s_turnActive.store(false);
    if (!micsWaiting) return;
    if (spoke && s_cfg.followUp && s_ready.load() && assistantBeginTurn(true)) {
        voiceFollowUp();
    } else {
        voiceResume();
    }
}

static void handleReply(const cJSON* intentOutput) {
    const cJSON* resp = cJSON_GetObjectItemCaseSensitive(intentOutput, "response");
    const cJSON* speech = cJSON_GetObjectItemCaseSensitive(resp, "speech");
    const cJSON* plain = cJSON_GetObjectItemCaseSensitive(speech, "plain");
    const char* said = jstr(plain, "speech");
    const char* cid = jstr(intentOutput, "conversation_id");
    if (cid) {
        s_conversationId = cid;
        s_conversationUntil = nowMs() + kConversationMs;
    }
    deliverReply(said ? said : "", true);
}

// ---------------------------------------------------------------------------
// Seeing: a photo and a question straight to a vision model in Ollama.

static const char* kVisionPrompt =
    "You are Buddy, a small, cheerful robot that lives on your owner's desk. The image is what you see "
    "right now through your camera (it may be a bit dark or blurry). Answer what your owner asked about "
    "it in one to three short, friendly sentences, the way you'd say them out loud: no lists, no "
    "markdown, no emoji. If you can't tell what something is, say so honestly. Start your reply with "
    "exactly one emotion tag in square brackets, chosen from: [happy] [laughing] [loving] [surprised] "
    "[shocked] [thinking] [confused] [cool] [silly] [embarrassed] [sad] [neutral].";

// Asks the vision model about the current photo (s_visionImage). Returns the
// reply text, or "" (and logs why) if it failed.
// The chat for the vision model: instructions, the conversation about this
// photo so far (the photo goes with the first question), and the new question.
static void addVisionMessages(cJSON* msgs, const std::string& question) {
    auto add = [&](const char* role, const std::string& text, bool withImage) {
        cJSON* m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", role);
        cJSON_AddStringToObject(m, "content", text.c_str());
        if (withImage) {
            cJSON* imgs = cJSON_AddArrayToObject(m, "images");
            cJSON_AddItemToArray(imgs, cJSON_CreateString(s_visionImage.c_str()));
        }
        cJSON_AddItemToArray(msgs, m);
    };
    add("system", std::string(kVisionPrompt) + (s_cfg.memory ? "\n" + memoryPromptBlock() : std::string()), false);
    bool first = true;
    for (const auto& [role, text] : s_visionChat) {
        add(role.c_str(), text, first && role == "user");
        if (role == "user") first = false;
    }
    add("user", question, first);
}

static void rememberVisionAnswer(const std::string& question, const std::string& answer) {
    if (answer.empty()) return;
    s_visionChat.emplace_back("user", question);
    s_visionChat.emplace_back("assistant", answer);
    s_visionUntil = nowMs() + kVisionFollowUpMs;
}

// At home: straight to Ollama on the home network. Blocks until it answers.
static std::string askVision(const std::string& question) {
    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", s_cfg.visionModel.c_str());
    cJSON_AddBoolToObject(req, "stream", false);
    cJSON_AddStringToObject(req, "keep_alive", "10m");
    addVisionMessages(cJSON_AddArrayToObject(req, "messages"), question);
    char* body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return "";

    const std::string url = s_cfg.visionUrl + "/api/chat";
    esp_http_client_config_t hc = {};
    hc.url = url.c_str();
    hc.method = HTTP_METHOD_POST;
    hc.timeout_ms = 90000;  // loading the model the first time can take a while
    hc.buffer_size = 2048;
    hc.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t http = esp_http_client_init(&hc);
    esp_http_client_set_header(http, "Content-Type", "application/json");
    const int bodyLen = strlen(body);
    const int64_t t0 = esp_timer_get_time();
    std::string resp;
    int status = 0;
    if (esp_http_client_open(http, bodyLen) == ESP_OK && esp_http_client_write(http, body, bodyLen) == bodyLen) {
        esp_http_client_fetch_headers(http);
        char chunk[512];
        int n;
        while ((n = esp_http_client_read(http, chunk, sizeof chunk)) > 0 && resp.size() < 32768) resp.append(chunk, n);
        status = esp_http_client_get_status_code(http);
    } else {
        ESP_LOGW(TAG, "couldn't reach the vision model at %s (is Ollama running, and vision_url right?)",
                 s_cfg.visionUrl.c_str());
    }
    esp_http_client_cleanup(http);
    cJSON_free(body);
    if (status == 0) return "";
    if (status != 200) {
        if (status == 404)
            ESP_LOGW(TAG, "Ollama doesn't have \"%s\": on the PC run  ollama pull %s", s_cfg.visionModel.c_str(),
                     s_cfg.visionModel.c_str());
        else
            ESP_LOGW(TAG, "vision model error: HTTP %d %.160s", status, resp.c_str());
        return "";
    }
    cJSON* j = cJSON_Parse(resp.c_str());
    const cJSON* m = j ? cJSON_GetObjectItemCaseSensitive(j, "message") : nullptr;
    const char* content = jstr(m, "content");
    std::string answer = content ? content : "";
    cJSON_Delete(j);
    ESP_LOGI(TAG, "vision model answered in %d ms", (int)((esp_timer_get_time() - t0) / 1000));
    rememberVisionAnswer(question, answer);
    return answer;
}

// Away from home Ollama can't be reached (and must not be put on the
// internet: it has no password), so the photo goes through Home Assistant:
// its rest_command.buddy_vision passes the request to Ollama on the home
// network and hands back the answer. The answer arrives as a websocket
// result (handleVisionResult).
static int s_visionReqId = 0;
static std::string s_visionQuestion;
static bool s_visionMicsWaiting = false;
static int64_t s_visionT0 = 0;

static void finishVision(const std::string& answer, bool micsWaiting) {
    if (answer.empty()) {
        s_turnActive.store(false);
        post(AssistEvent::Error, "couldn't see");
        if (micsWaiting) voiceResume();
        return;
    }
    deliverReply(answer, micsWaiting);
}

static bool askVisionViaHomeAssistant(const std::string& question, bool micsWaiting) {
    cJSON* m = cJSON_CreateObject();
    s_visionReqId = s_nextId++;
    cJSON_AddNumberToObject(m, "id", s_visionReqId);
    cJSON_AddStringToObject(m, "type", "call_service");
    cJSON_AddStringToObject(m, "domain", "rest_command");
    cJSON_AddStringToObject(m, "service", "buddy_vision");
    cJSON* data = cJSON_AddObjectToObject(m, "service_data");
    cJSON_AddStringToObject(data, "model", s_cfg.visionModel.c_str());
    addVisionMessages(cJSON_AddArrayToObject(data, "messages"), question);
    cJSON_AddBoolToObject(m, "return_response", true);
    if (!sendJson(m, 30000)) return false;  // ~30 KB with the photo, maybe over mobile data
    s_visionQuestion = question;
    s_visionMicsWaiting = micsWaiting;
    s_visionT0 = esp_timer_get_time();
    s_stage = Stage::WaitVision;
    s_deadline = nowMs() + 130000;  // rest_command's own timeout is 120 s
    return true;
}

static void handleVisionResult(const cJSON* j) {
    s_stage = Stage::Idle;
    const int id = s_visionReqId;
    s_visionReqId = 0;
    (void)id;
    std::string answer;
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "success"))) {
        const char* msg = jstr(cJSON_GetObjectItemCaseSensitive(j, "error"), "message");
        ESP_LOGW(TAG, "Home Assistant couldn't pass the photo on: %s%s", msg ? msg : "?",
                 msg && strstr(msg, "not found") ? " (add rest_command.buddy_vision to configuration.yaml: see the README)" : "");
    } else {
        const cJSON* resp = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "result"), "response");
        const cJSON* status = cJSON_GetObjectItemCaseSensitive(resp, "status");
        const cJSON* content = cJSON_GetObjectItemCaseSensitive(resp, "content");
        cJSON* parsed = nullptr;
        if (cJSON_IsString(content)) content = parsed = cJSON_Parse(content->valuestring);  // not sent as JSON
        const char* text = jstr(cJSON_GetObjectItemCaseSensitive(content, "message"), "content");
        if (cJSON_IsNumber(status) && status->valueint != 200)
            ESP_LOGW(TAG, "the vision model answered HTTP %d through Home Assistant", status->valueint);
        else if (!text)
            ESP_LOGW(TAG, "no answer from the vision model through Home Assistant (is Ollama running on the PC?)");
        if (text) answer = text;
        cJSON_Delete(parsed);
    }
    ESP_LOGI(TAG, "vision model answered through Home Assistant in %d ms",
             (int)((esp_timer_get_time() - s_visionT0) / 1000));
    rememberVisionAnswer(s_visionQuestion, answer);
    finishVision(answer, s_visionMicsWaiting);
}

// Asks the vision model at home directly, or away through Home Assistant
// (then the answer comes later, in handleVisionResult).
static void startVision(const std::string& question, bool micsWaiting) {
    if (awayFromHome()) {
        ESP_LOGI(TAG, "away from home: asking the vision model through Home Assistant");
        if (!askVisionViaHomeAssistant(question, micsWaiting)) finishVision("", micsWaiting);
        return;
    }
    finishVision(askVision(question), micsWaiting);
}

static void handleLook() {
    uint8_t* jpeg = s_lookJpeg;
    const size_t len = s_lookLen;
    const std::string question = s_lookQuestion.empty() ? "What do you see?" : s_lookQuestion;
    const bool micsWaiting = s_lookFromConversation;
    s_lookJpeg = nullptr;

    // A new photo starts a new little conversation about it.
    s_visionChat.clear();
    s_visionImage.clear();
    size_t b64len = 0;
    mbedtls_base64_encode(nullptr, 0, &b64len, jpeg, len);
    s_visionImage.resize(b64len);
    size_t written = 0;
    if (mbedtls_base64_encode((unsigned char*)&s_visionImage[0], b64len, &written, jpeg, len) == 0)
        s_visionImage.resize(written);
    else
        s_visionImage.clear();
    heap_caps_free(jpeg);

    s_turnActive.store(true);
    post(AssistEvent::Thinking);
    ESP_LOGI(TAG, "looking: \"%s\" (%u KB photo -> %s)", question.c_str(), (unsigned)(len / 1024),
             s_cfg.visionModel.c_str());
    if (s_visionImage.empty()) finishVision("", micsWaiting);
    else startVision(question, micsWaiting);
}

static void handleRunEvent(const cJSON* ev) {
    const char* type = jstr(ev, "type");
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(ev, "data");
    if (!type) return;
    if (strcmp(type, "run-start") == 0) {
        const cJSON* runner = cJSON_GetObjectItemCaseSensitive(data, "runner_data");
        const cJSON* h = cJSON_GetObjectItemCaseSensitive(runner, "stt_binary_handler_id");
        if (s_stage == Stage::WaitRunStart) {
            if (!cJSON_IsNumber(h)) {
                failTurn("Home Assistant didn't accept audio");
                return;
            }
            s_handlerId = h->valueint;
            s_stage = Stage::Streaming;
            s_deadline = nowMs() + 30000;  // voice.cpp ends the audio well before this
        }
    } else if (strcmp(type, "stt-vad-end") == 0) {
        s_sttDone.store(true);
    } else if (strcmp(type, "stt-end") == 0) {
        const cJSON* out = cJSON_GetObjectItemCaseSensitive(data, "stt_output");
        const char* text = jstr(out, "text");
        s_accepting.store(false);
        handleHeard(text ? text : "");
    } else if (strcmp(type, "intent-end") == 0) {
        handleReply(cJSON_GetObjectItemCaseSensitive(data, "intent_output"));
    } else if (strcmp(type, "error") == 0) {
        const char* code = jstr(data, "code");
        const char* message = jstr(data, "message");
        if (code && strcmp(code, "stt-no-text-recognized") == 0) {
            ESP_LOGI(TAG, "no words recognized");
            post(AssistEvent::NotHeard);
            endTurnAndResume();
        } else {
            ESP_LOGW(TAG, "Home Assistant error %s: %s", code ? code : "?", message ? message : "");
            post(AssistEvent::Error, code ? code : "error");
            endTurnAndResume();
        }
    } else if (strcmp(type, "run-end") == 0) {
        if (s_stage != Stage::Idle) failTurn("Home Assistant ended the request without an answer");
    }
}

static void handlePipelines(const cJSON* result) {
    const cJSON* list = cJSON_GetObjectItemCaseSensitive(result, "pipelines");
    const char* preferred = jstr(result, "preferred_pipeline");
    const cJSON *chosen = nullptr, *fallback = nullptr, *p;
    std::string names;
    cJSON_ArrayForEach(p, list) {
        const char* name = jstr(p, "name");
        const char* id = jstr(p, "id");
        if (!name || !id) continue;
        names += names.empty() ? "" : ", ";
        names += name;
        if (normalizeSpeech(name) == normalizeSpeech(s_cfg.pipeline) && !s_cfg.pipeline.empty()) chosen = p;
        if (preferred && strcmp(id, preferred) == 0) fallback = p;
    }
    if (!chosen) {
        if (!s_cfg.pipeline.empty())
            ESP_LOGW(TAG, "no voice assistant named \"%s\" (found: %s); using the preferred one",
                     s_cfg.pipeline.c_str(), names.c_str());
        chosen = fallback;
    }
    if (!chosen) {
        s_status.store("no voice assistant");
        ESP_LOGE(TAG, "Home Assistant has no voice assistant to use");
        return;
    }
    auto get = [&](const char* key) { const char* v = jstr(chosen, key); return std::string(v ? v : ""); };
    s_pipelineId = get("id");
    s_ttsEngine = get("tts_engine");
    s_ttsLanguage = get("tts_language");
    s_ttsVoice = get("tts_voice");
    ESP_LOGI(TAG, "using voice assistant \"%s\": speech-to-text %s, conversation %s, text-to-speech %s (%s)",
             get("name").c_str(), get("stt_engine").c_str(), get("conversation_engine").c_str(),
             s_ttsEngine.c_str(), s_ttsVoice.empty() ? "default voice" : s_ttsVoice.c_str());
    if (get("stt_engine").empty()) ESP_LOGW(TAG, "that voice assistant has no speech-to-text set up");
    s_status.store("ready");
    s_ready.store(true);
}

// Logs in. Sent as soon as the connection opens rather than in reply to
// Home Assistant's "auth_required": ESP-IDF 5.4's websocket transport keeps
// that first message (which arrives in the same packet as the handshake)
// buffered without noticing it, so waiting for it waits forever, and Home
// Assistant drops the connection after 10 s. Its reply to the token is new
// data, which also flushes the buffered message.
static void sendAuth() {
    if (s_authSent) return;
    cJSON* m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "type", "auth");
    cJSON_AddStringToObject(m, "access_token", s_cfg.token.c_str());
    s_authSent = sendJson(m);
    if (s_authSent) ESP_LOGI(TAG, "token sent; waiting for the answer");
}

static void handleMessage(const std::string& text) {
    cJSON* j = cJSON_Parse(text.c_str());
    if (!j) {
        ESP_LOGW(TAG, "couldn't read a message from Home Assistant (%u bytes): %.60s", (unsigned)text.size(),
                 text.c_str());
        return;
    }
    const char* type = jstr(j, "type");
    const cJSON* idItem = cJSON_GetObjectItemCaseSensitive(j, "id");
    const int id = cJSON_IsNumber(idItem) ? idItem->valueint : 0;
    if (!type) {
        cJSON_Delete(j);
        return;
    }
    if (strcmp(type, "auth_required") == 0) {
        sendAuth();  // normally already sent on connect
    } else if (strcmp(type, "auth_ok") == 0) {
        ESP_LOGI(TAG, "logged in (Home Assistant %s)", jstr(j, "ha_version") ? jstr(j, "ha_version") : "?");
        s_status.store("finding voice assistant");
        s_nextId = 1;
        s_listId = s_nextId++;
        cJSON* m = cJSON_CreateObject();
        cJSON_AddNumberToObject(m, "id", s_listId);
        cJSON_AddStringToObject(m, "type", "assist_pipeline/pipeline/list");
        sendJson(m);
    } else if (strcmp(type, "auth_invalid") == 0) {
        // Don't keep retrying: Home Assistant can ban addresses after failed logins.
        ESP_LOGE(TAG, "Home Assistant rejected the access token (ha_token in settings.txt); "
                      "not retrying until restart");
        s_tokenRejected = true;
        s_status.store("token rejected");
    } else if (strcmp(type, "result") == 0) {
        const cJSON* ok = cJSON_GetObjectItemCaseSensitive(j, "success");
        if (id == s_visionReqId && s_visionReqId != 0 && s_stage == Stage::WaitVision) {
            handleVisionResult(j);
        } else if (id == s_listId) {
            if (cJSON_IsTrue(ok)) handlePipelines(cJSON_GetObjectItemCaseSensitive(j, "result"));
            else ESP_LOGE(TAG, "couldn't list voice assistants");
        } else if (id == s_runId && !cJSON_IsTrue(ok)) {
            const cJSON* err = cJSON_GetObjectItemCaseSensitive(j, "error");
            const char* msg = jstr(err, "message");
            ESP_LOGW(TAG, "request refused: %s", msg ? msg : "?");
            failTurn("Home Assistant refused the request");
        }
    } else if (strcmp(type, "event") == 0 && id == s_runId && s_runId != 0) {
        handleRunEvent(cJSON_GetObjectItemCaseSensitive(j, "event"));
    }
    cJSON_Delete(j);
}

static void startWebsocket() {
    static std::string uri;
    s_baseNet = wifiNetworkIndex();
    s_baseUrl = baseUrlFor(s_baseNet);
    uri = s_baseUrl;
    if (uri.rfind("https://", 0) == 0) uri = "wss://" + uri.substr(8);
    else if (uri.rfind("http://", 0) == 0) uri = "ws://" + uri.substr(7);
    uri += "/api/websocket";

    esp_websocket_client_config_t wc = {};
    wc.uri = uri.c_str();
    wc.buffer_size = 4096;
    wc.task_stack = 5120;
    wc.task_prio = 4;
    wc.task_core_id_set = true;
    wc.task_core_id = 1;
    wc.reconnect_timeout_ms = 10000;
    wc.enable_close_reconnect = true;  // also reconnect after Home Assistant closes the connection
    wc.network_timeout_ms = 10000;
    wc.ping_interval_sec = 20;
    wc.pingpong_timeout_sec = 60;
    wc.crt_bundle_attach = esp_crt_bundle_attach;
    s_ws = esp_websocket_client_init(&wc);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, onWsEvent, nullptr);
    s_status.store("connecting");
    ESP_LOGI(TAG, "connecting to %s", uri.c_str());
    esp_websocket_client_start(s_ws);
}

static void assistantTask(void*) {
    s_status.store("waiting for Wi-Fi");
    while (!wifiConnected()) vTaskDelay(pdMS_TO_TICKS(200));
    startWebsocket();

    while (true) {
        std::string* msg = nullptr;
        const TickType_t wait = s_stage == Stage::Idle ? pdMS_TO_TICKS(30) : pdMS_TO_TICKS(10);
        if (xQueueReceive(s_inbox, &msg, wait) == pdTRUE) {
            if (msg == &s_lookMarker) {
                handleLook();
            } else if (msg == &s_sayMarker) {
                ESP_LOGI(TAG, "saying on its own: \"%s\"", s_sayText.c_str());
                deliverReply(s_sayText, false);  // the microphones aren't waiting: no follow-up
            } else if (msg == &s_connectedMarker) {
                s_authSent = false;  // new connection
                sendAuth();
            } else if (msg) {
                handleMessage(*msg);
                delete msg;
            } else if (s_stage != Stage::Idle) {
                failTurn("lost the connection to Home Assistant");  // disconnected mid-turn
            }
        }
        // Moved to another Wi-Fi network: if Home Assistant has a different
        // address there (ha_url at home, ha_url_away elsewhere), reconnect to it.
        if (s_ws && wifiConnected() && wifiNetworkIndex() != s_baseNet) {
            const int net = wifiNetworkIndex();
            if (baseUrlFor(net) == s_baseUrl) {
                s_baseNet = net;
            } else {
                ESP_LOGI(TAG, "now on \"%s\": switching to %s", wifiNetworkName(), baseUrlFor(net).c_str());
                if (s_stage != Stage::Idle) failTurn("switched Wi-Fi networks");
                s_ready.store(false);
                esp_websocket_client_stop(s_ws);
                esp_websocket_client_destroy(s_ws);
                s_ws = nullptr;
                startWebsocket();
            }
        }
        if (s_tokenRejected) {
            s_tokenRejected = false;
            s_ready.store(false);
            esp_websocket_client_stop(s_ws);
        }

        if (s_stage == Stage::Idle && s_turnPending.exchange(false)) startSttRun();
        if (s_stage == Stage::Streaming) pumpAudio();
        if (s_stage != Stage::Idle && (int32_t)(nowMs() - s_deadline) > 0) {
            failTurn(s_stage == Stage::WaitVision ? "the vision model took too long (through Home Assistant)"
                     : s_stage == Stage::WaitIntent ? "the AI took too long to answer"
                     : s_stage == Stage::WaitStt  ? "speech-to-text took too long"
                                                  : "Home Assistant didn't respond");
        }
    }
}

// ---------------------------------------------------------------------------

bool assistantStart(const AssistantConfig& cfg, QueueHandle_t events) {
    if (cfg.url.empty() || cfg.token.empty()) return false;
    s_cfg = cfg;
    s_events = events;
    cJSON_Hooks hooks = {psramMalloc, psramFree};
    cJSON_InitHooks(&hooks);
    s_inbox = xQueueCreate(24, sizeof(std::string*));
    s_audio = xStreamBufferCreateWithCaps(kSampleRate * 2 * 4, 1, MALLOC_CAP_SPIRAM);  // 4 s
    if (!s_inbox || !s_audio) return false;
    // Stack in PSRAM: internal RAM is scarce, and this task never touches flash.
    // Priority 5, above the UI (4): otherwise fetching a reply only ran in the
    // UI's short pauses, which made replies slow and starved the idle task.
    if (xTaskCreatePinnedToCoreWithCaps(assistantTask, "assistant", 8192, nullptr, 5, &s_task, 1,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "out of memory starting the assistant");
        return false;
    }
    return true;
}

bool assistantAvailable() { return s_ready.load() && !s_turnActive.load(); }
const char* assistantStatus() { return s_status.load(); }

bool assistantBeginTurn(bool followUp) {
    if (!s_ready.load()) return false;
    bool expected = false;
    if (!s_turnActive.compare_exchange_strong(expected, true)) return false;
    s_followUpTurn.store(followUp);
    s_audioEnded.store(false);
    s_heardSpeech.store(false);
    s_sttDone.store(false);
    s_accepting.store(true);
    s_turnPending.store(true);
    return true;
}

void assistantAudio(const int16_t* pcm, int samples) {
    if (!s_accepting.load()) return;
    xStreamBufferSend(s_audio, pcm, samples * sizeof(int16_t), 0);  // drops if Home Assistant is far behind
}

void assistantEndAudio(bool heardSpeech) {
    s_heardSpeech.store(heardSpeech);
    s_audioEnded.store(true);
    if (heardSpeech) post(AssistEvent::Thinking);
}

bool assistantSttDone() { return s_sttDone.load(); }

bool assistantCanSee() { return !s_cfg.visionUrl.empty() && s_ready.load(); }

bool assistantSay(const std::string& text) {
    if (!s_inbox || !s_ready.load()) return false;
    bool expected = false;
    if (!s_turnActive.compare_exchange_strong(expected, true)) return false;  // mid-conversation
    s_sayText = text;
    std::string* m = &s_sayMarker;
    if (xQueueSend(s_inbox, &m, pdMS_TO_TICKS(100)) != pdTRUE) {
        s_turnActive.store(false);
        return false;
    }
    return true;
}

void assistantLook(uint8_t* jpeg, size_t len, const std::string& question, bool fromConversation) {
    if (!jpeg || !s_inbox || s_lookJpeg) {  // no photo, not running, or still busy with the last one
        heap_caps_free(jpeg);
        if (fromConversation) {
            s_turnActive.store(false);
            voiceResume();
        }
        return;
    }
    s_lookJpeg = jpeg;
    s_lookLen = len;
    s_lookQuestion = question;
    s_lookFromConversation = fromConversation;
    std::string* m = &s_lookMarker;
    xQueueSend(s_inbox, &m, pdMS_TO_TICKS(100));
}
