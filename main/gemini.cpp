#include "gemini.h"

#include <atomic>
#include <deque>
#include <vector>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

#include "AssistText.h"
#include "Memory.h"
#include "Wav.h"
#include "memory.h"
#include "sound.h"
#include "voice.h"
#include "wifi.h"

static const char* TAG = "gemini";

// The Live API over a websocket, logged in with the API key (sent as a
// header, so it's never part of a logged address).
static constexpr const char* kUrl =
    "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
static constexpr int kMicRate = 16000;    // what we send
static constexpr int kVoiceRate = 24000;  // what Gemini speaks
static constexpr uint32_t kConnectMs = 12000;
static constexpr uint32_t kReplyMs = 30000;
static constexpr uint32_t kIdleCloseMs = 300000;  // close the session after 5 quiet minutes
static constexpr size_t kHistory = 8;             // exchanges remembered across sessions

static AssistantConfig s_cfg;
static QueueHandle_t s_events = nullptr;
static QueueHandle_t s_inbox = nullptr;  // std::string*: messages; nullptr: disconnected; markers below
static StreamBufferHandle_t s_audio = nullptr;
static esp_websocket_client_handle_t s_ws = nullptr;
static std::string s_connectedMarker, s_wakeMarker, s_photoMarker;
// "What am I holding?": Gemini calls look, the main loop takes a photo and
// hands it back here, and it goes to Gemini with the answer to the call.
static std::atomic<bool> s_lookPending{false};
static uint8_t* s_photo = nullptr;  // main loop -> gemini task (with s_photoMarker)
static size_t s_photoLen = 0;
static std::string s_lookId;        // gemini task only: the look call waiting for its photo

static std::atomic<bool> s_started{false};
static std::atomic<bool> s_keyRejected{false};
static std::atomic<bool> s_turnActive{false};   // from "Hey Buddy" (or a say) until he's done
static std::atomic<bool> s_turnPending{false};  // a turn is waiting for the session
static std::atomic<bool> s_followUpTurn{false};
static std::atomic<bool> s_accepting{false};
static std::atomic<bool> s_audioEnded{false};
static std::atomic<bool> s_heardSpeech{false};
static std::atomic<bool> s_cancelled{false};
static std::atomic<bool> s_sayPending{false};
static std::atomic<const char*> s_status{"off"};
// If Gemini turns the session down as it starts, the next try asks for less:
// 0 everything, 1 without Google Search, 2 without any tools.
static std::atomic<int> s_setupLevel{1};  // Google Search isn't in the free tier ("exceeded your quota")
static std::atomic<bool> s_setupDone{false};
static std::string s_sayText;

// Gemini task only.
enum class Stage { Idle, Streaming, WaitReply };
static Stage s_stage = Stage::Idle;
static bool s_connecting = false, s_sessionReady = false, s_closeAfterTurn = false;
static bool s_discard = false;      // a turn with nothing said: ignore its answer
static bool s_fromVoice = false;    // the turn came from the microphones (they wait for it)
static bool s_spoke = false;        // some of this answer was played
// A follow-up (no "Hey Buddy") may be a TV or someone else talking: its
// answer is held back until its first few words are in, and dropped if he's
// answering in another language (Gemini heard noise or a TV, not you).
static bool s_gate = false, s_drop = false;
static bool s_usedTool = false, s_nudged = false;  // it remembered something, then said nothing: ask it to say so
static std::vector<int16_t> s_held;
static std::atomic<int> s_followUps{0};  // in a row, since "Hey Buddy"
static constexpr int kMaxFollowUps = 3;
static std::string s_heard, s_said, s_emotion;
static uint32_t s_deadline = 0, s_lastUse = 0;
static std::deque<std::pair<std::string, std::string>> s_history;  // (what you said, what he said)
static StreamResampler s_resampler(kVoiceRate, kMicRate);

// Websocket task only.
static std::string s_rx;

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }
static void* psramMalloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static void post(AssistEvent::Type type, const char* text = "", const char* emotion = "") {
    AssistEvent ev{};
    ev.type = type;
    ev.followUp = s_followUpTurn.load();
    strncpy(ev.emotion, emotion, sizeof ev.emotion - 1);
    strncpy(ev.text, text, sizeof ev.text - 1);
    xQueueSend(s_events, &ev, pdMS_TO_TICKS(100));
}

static bool sendText(const std::string& text) {
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) return false;
    const int sent = esp_websocket_client_send_text(s_ws, text.data(), (int)text.size(), pdMS_TO_TICKS(3000));
    if (sent != (int)text.size()) ESP_LOGW(TAG, "send failed (%d of %u bytes)", sent, (unsigned)text.size());
    return sent == (int)text.size();
}

static bool sendJson(cJSON* msg) {
    char* text = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    if (!text) return false;
    const bool ok = sendText(text);
    cJSON_free(text);
    return ok;
}

// ---------------------------------------------------------------------------
// Websocket task: collect each message (it may come in pieces) and hand it over.

static void onWsEvent(void*, esp_event_base_t, int32_t id, void* data) {
    const auto* d = (esp_websocket_event_data_t*)data;
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED: {
            std::string* m = &s_connectedMarker;
            xQueueSend(s_inbox, &m, pdMS_TO_TICKS(500));
            break;
        }
        case WEBSOCKET_EVENT_CLOSED:
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_ERROR: {
            if (id == WEBSOCKET_EVENT_CLOSED) {
                // The close frame says why (a bad key, an unknown model...).
                std::string why;
                if (d->data_ptr && d->data_len > 2) why.assign(d->data_ptr + 2, d->data_len - 2);
                ESP_LOGW(TAG, "Gemini closed the session (code %d): %s", d->close_status_code, why.c_str());
                std::string low = why;
                for (auto& c : low) c = (char)tolower((unsigned char)c);
                if (low.find("api key") != std::string::npos || low.find("permission") != std::string::npos ||
                    low.find("unauthenticated") != std::string::npos) {
                    s_keyRejected.store(true);
                    s_status.store("Gemini key rejected");
                }
            } else if (id == WEBSOCKET_EVENT_DISCONNECTED) {
                ESP_LOGW(TAG, "lost the connection to Gemini (HTTP %d, errno %d)",
                         d->error_handle.esp_ws_handshake_status_code, d->error_handle.esp_transport_sock_errno);
                if (d->error_handle.esp_ws_handshake_status_code == 401 ||
                    d->error_handle.esp_ws_handshake_status_code == 403) {
                    s_keyRejected.store(true);
                    s_status.store("Gemini key rejected");
                }
            }
            std::string* none = nullptr;
            xQueueSend(s_inbox, &none, 0);
            break;
        }
        case WEBSOCKET_EVENT_DATA:
            if (d->op_code == 0x8) {  // close: a 2-byte code, then why
                std::string why;
                if (d->data_ptr && d->data_len > 2) why.assign(d->data_ptr + 2, d->data_len - 2);
                const int code = (d->data_ptr && d->data_len >= 2)
                                     ? (((uint8_t)d->data_ptr[0] << 8) | (uint8_t)d->data_ptr[1]) : 0;
                ESP_LOGW(TAG, "Gemini is closing the session (code %d): %s", code, why.c_str());
                std::string low = why;
                for (auto& c : low) c = (char)tolower((unsigned char)c);
                if (low.find("api key") != std::string::npos || low.find("unauthenticated") != std::string::npos) {
                    s_keyRejected.store(true);
                    s_status.store("Gemini key rejected");
                }
                break;
            }
            // Text or binary frames, both JSON (and their continuations).
            if (d->op_code != 0x1 && d->op_code != 0x2 && d->op_code != 0x0) break;
            if (d->payload_offset == 0) s_rx.clear();
            s_rx.append(d->data_ptr, d->data_len);
            if (d->payload_offset + d->data_len >= d->payload_len) {
                auto* msg = new std::string(std::move(s_rx));
                s_rx.clear();
                // Speech can come faster than it plays: wait rather than drop it.
                if (xQueueSend(s_inbox, &msg, pdMS_TO_TICKS(8000)) != pdTRUE) {
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
// The session.

static std::string systemPrompt() {
    const std::string me = s_cfg.buddyName.empty() ? "Buddy" : s_cfg.buddyName;
    const std::string them = s_cfg.ownerName.empty() ? "your owner" : s_cfg.ownerName;
    std::string p = "You are " + me + ", a small talking robot companion with a computer screen for a face, living on " +
                    them + "'s desk. You talk out loud through a little speaker, so keep answers short and natural: "
                    "usually one to three sentences, like a friend chatting, never lists or markdown. ";
    p += s_cfg.personality.empty()
             ? "You're playful, curious and quick-witted, with a dry, friendly sense of humor; you tease a little "
               "but you're never mean. "
             : "About you: " + s_cfg.personality + " ";
    if (!s_cfg.ownerName.empty()) p += "The person you live with is " + s_cfg.ownerName + ". ";
    p += "You have a camera in your face: when asked what you see, what they're holding or showing you, or how "
         "something looks, call look, then answer from the photo. You can wear sunglasses: when asked to put on "
         "(or take off) your shades or sunglasses, call sunglasses. ";
    p += "If you're asked to remember something, call the remember tool; to forget something, call forget. "
         "Always speak English, even if you hear another language. If what you hear isn't clearly someone "
         "talking to you (a TV, a video, people talking to each other, noise), say nothing at all. ";
    if (s_setupLevel.load() == 0) p += "Use Google Search for anything current (news, scores, weather). ";
    if (s_cfg.memory) {
        const std::string notes = memoryPromptBlock();
        if (!notes.empty()) p += "\n" + notes;
    }
    if (!s_history.empty()) {
        p += "\nWhat you talked about recently (for context; don't repeat it):\n";
        for (const auto& [heard, said] : s_history) p += "- They said: " + heard + "\n  You said: " + said + "\n";
    }
    return p;
}

static cJSON* fnDecl(const char* name, const char* desc, const char* arg, const char* argDesc) {
    cJSON* f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "name", name);
    cJSON_AddStringToObject(f, "description", desc);
    cJSON* params = cJSON_AddObjectToObject(f, "parameters");
    cJSON_AddStringToObject(params, "type", "OBJECT");
    cJSON* props = cJSON_AddObjectToObject(params, "properties");
    cJSON* a = cJSON_AddObjectToObject(props, arg);
    cJSON_AddStringToObject(a, "type", "STRING");
    cJSON_AddStringToObject(a, "description", argDesc);
    cJSON* req = cJSON_AddArrayToObject(params, "required");
    cJSON_AddItemToArray(req, cJSON_CreateString(arg));
    return f;
}

static void sendSetup() {
    cJSON* root = cJSON_CreateObject();
    cJSON* setup = cJSON_AddObjectToObject(root, "setup");
    cJSON_AddStringToObject(setup, "model", ("models/" + s_cfg.geminiModel).c_str());
    cJSON* gen = cJSON_AddObjectToObject(setup, "generationConfig");
    cJSON* mods = cJSON_AddArrayToObject(gen, "responseModalities");
    cJSON_AddItemToArray(mods, cJSON_CreateString("AUDIO"));
    cJSON* voice = cJSON_AddObjectToObject(cJSON_AddObjectToObject(cJSON_AddObjectToObject(gen, "speechConfig"),
                                                                   "voiceConfig"),
                                           "prebuiltVoiceConfig");
    cJSON_AddStringToObject(voice, "voiceName", s_cfg.geminiVoice.c_str());
    cJSON* sys = cJSON_AddObjectToObject(setup, "systemInstruction");
    cJSON* part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", systemPrompt().c_str());
    cJSON_AddItemToArray(cJSON_AddArrayToObject(sys, "parts"), part);
    // We decide when you've finished talking (the same as with Home Assistant).
    cJSON* rt = cJSON_AddObjectToObject(setup, "realtimeInputConfig");
    cJSON_AddBoolToObject(cJSON_AddObjectToObject(rt, "automaticActivityDetection"), "disabled", true);
    cJSON_AddObjectToObject(setup, "inputAudioTranscription");
    cJSON_AddObjectToObject(setup, "outputAudioTranscription");
    const int level = s_setupLevel.load();
    if (level >= 2) {
        ESP_LOGW(TAG, "starting the session without tools (no remembering or searching)");
        if (!sendJson(root)) ESP_LOGW(TAG, "couldn't send the session setup");
        return;
    }
    cJSON* tools = cJSON_AddArrayToObject(setup, "tools");
    if (level == 0) {
        cJSON* search = cJSON_CreateObject();
        cJSON_AddObjectToObject(search, "googleSearch");
        cJSON_AddItemToArray(tools, search);
    } else {
        ESP_LOGW(TAG, "starting the session without Google Search");
    }
    cJSON* fns = cJSON_CreateObject();
    cJSON* decls = cJSON_AddArrayToObject(fns, "functionDeclarations");
    cJSON_AddItemToArray(decls, fnDecl("remember", "Write down a lasting fact about your owner or their life, to "
                                       "remember from now on.", "fact",
                                       "The fact, in the third person, e.g. 'his dog is named King'"));
    cJSON_AddItemToArray(decls, fnDecl("forget", "Cross out a fact you'd written down.", "what",
                                       "What to forget, e.g. 'the dog's name'"));
    cJSON_AddItemToArray(decls, fnDecl("look", "Take a photo with your camera, to see what's in front of you: "
                                       "what they're holding or showing you, or how they look.", "question",
                                       "What you want to find out from the photo"));
    cJSON_AddItemToArray(decls, fnDecl("sunglasses", "Put your sunglasses on, or take them off.", "wear",
                                       "'on' or 'off'"));
    cJSON_AddItemToArray(tools, fns);
    if (!sendJson(root)) ESP_LOGW(TAG, "couldn't send the session setup");
}

static void closeSession() {
    if (s_ws) {
        esp_websocket_client_destroy(s_ws);  // stops it first
        s_ws = nullptr;
    }
    s_connecting = false;
    s_sessionReady = false;
    s_closeAfterTurn = false;
    s_rx.clear();
}

static void connect() {
    if (s_ws || s_keyRejected.load()) return;
    s_setupDone.store(false);
    static std::string headers;
    headers = "x-goog-api-key: " + s_cfg.geminiKey + "\r\n";
    esp_websocket_client_config_t wc = {};
    wc.uri = kUrl;
    wc.headers = headers.c_str();
    wc.crt_bundle_attach = esp_crt_bundle_attach;
    wc.buffer_size = 4096;
    wc.task_stack = 6144;
    wc.task_prio = 4;
    wc.task_core_id_set = true;
    wc.task_core_id = 1;
    wc.network_timeout_ms = 10000;
    wc.disable_auto_reconnect = true;
    s_ws = esp_websocket_client_init(&wc);
    if (!s_ws) {
        ESP_LOGE(TAG, "out of memory connecting to Gemini");
        return;
    }
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, onWsEvent, nullptr);
    s_connecting = true;
    s_status.store("connecting");
    s_deadline = nowMs() + kConnectMs;
    ESP_LOGI(TAG, "connecting to Gemini (%s, voice %s)", s_cfg.geminiModel.c_str(), s_cfg.geminiVoice.c_str());
    if (esp_websocket_client_start(s_ws) != ESP_OK) closeSession();
}

// ---------------------------------------------------------------------------
// Turns.

static void endTurn() {
    s_lookPending.store(false);  // (a photo arriving now isn't wanted)
    s_lookId.clear();
    s_accepting.store(false);
    s_stage = Stage::Idle;
    s_turnPending.store(false);
    s_turnActive.store(false);
    s_lastUse = nowMs();
}

static void failTurn(const char* what) {
    ESP_LOGW(TAG, "%s", what);
    if (s_spoke) soundStopSpeech();
    s_spoke = false;
    const bool fromVoice = s_fromVoice;
    s_discard = s_stage == Stage::WaitReply;  // an answer may still come: not now
    endTurn();
    post(AssistEvent::Error, what);
    if (fromVoice) voiceResume();
}

static void startTurn() {
    s_turnPending.store(false);
    s_fromVoice = true;
    s_spoke = false;
    s_gate = s_followUpTurn.load();
    s_drop = false;
    s_usedTool = s_nudged = false;
    s_held.clear();
    s_heard.clear();
    s_said.clear();
    s_emotion.clear();
    s_resampler.reset();
    s_stage = Stage::Streaming;
    sendText("{\"realtimeInput\":{\"activityStart\":{}}}");
}

static void startSay() {
    s_sayPending.store(false);
    s_fromVoice = false;
    s_spoke = false;
    s_gate = s_drop = false;
    s_usedTool = s_nudged = false;
    s_held.clear();
    s_heard.clear();
    s_said.clear();
    s_emotion.clear();
    s_resampler.reset();
    const std::string speech = splitReply(s_sayText).speech;
    cJSON* root = cJSON_CreateObject();
    cJSON* cc = cJSON_AddObjectToObject(root, "clientContent");
    cJSON* turn = cJSON_CreateObject();
    cJSON_AddStringToObject(turn, "role", "user");
    cJSON* part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text",
                            ("(Nobody asked anything: say this out loud, in your own voice and words, briefly: " +
                             speech + ")").c_str());
    cJSON_AddItemToArray(cJSON_AddArrayToObject(turn, "parts"), part);
    cJSON_AddItemToArray(cJSON_AddArrayToObject(cc, "turns"), turn);
    cJSON_AddBoolToObject(cc, "turnComplete", true);
    s_stage = Stage::WaitReply;
    s_deadline = nowMs() + kReplyMs;
    if (!sendJson(root)) failTurn("couldn't send to Gemini");
}

// Microphone audio to Gemini, 0.1 s at a time.
static void pumpAudio() {
    static uint8_t pcm[3200];
    static unsigned char b64[4400];
    for (int i = 0; i < 6; ++i) {
        const size_t n = xStreamBufferReceive(s_audio, pcm, sizeof pcm, 0);
        if (n == 0) break;
        size_t olen = 0;
        if (mbedtls_base64_encode(b64, sizeof b64, &olen, pcm, n) != 0) break;
        std::string msg = "{\"realtimeInput\":{\"audio\":{\"data\":\"";
        msg.append((const char*)b64, olen);
        msg += "\",\"mimeType\":\"audio/pcm;rate=16000\"}}}";
        if (!sendText(msg)) break;
    }
    if (!s_audioEnded.load() || !xStreamBufferIsEmpty(s_audio)) return;
    s_accepting.store(false);
    sendText("{\"realtimeInput\":{\"activityEnd\":{}}}");
    if (!s_heardSpeech.load()) {
        // Nothing was said (voice.cpp already went back to the wake word): any
        // answer to the silence isn't wanted.
        s_discard = true;
        endTurn();
        return;
    }
    s_stage = Stage::WaitReply;
    s_deadline = nowMs() + kReplyMs;
}

static void playPcm(const int16_t* pcm, size_t n) {
    if (!s_spoke) {
        s_spoke = true;
        soundSpeechBegin();
        s_emotion = emotionFromSpeech(s_said);
        post(AssistEvent::Reply, "", s_emotion.c_str());
    }
    if (!s_cancelled.load()) soundSpeechWrite(pcm, n);
}

// Was a follow-up really you? Gemini often hears nothing it can write down
// when it's you (an empty transcript), but when there was only a fan or a TV it
// "hears" a made-up Spanish sentence ("¿Qué es el número de serie?") and may
// answer it in English. So: not if all it heard was another language, and not
// if it's answering in another language.
static bool followUpWasYou() {
    if (looksForeign(s_heard) && englishWords(s_heard) < 2) return false;
    return !looksForeign(s_said);
}

// A follow-up's answer, held until we know what was heard: let it out, or drop it.
static void openGate(bool play) {
    if (!s_gate) return;
    s_gate = false;
    if (play && !s_held.empty()) playPcm(s_held.data(), s_held.size());
    if (!play) {
        s_drop = true;
        if (s_said.empty())
            ESP_LOGI(TAG, "follow-up: no answer came; going back to waiting for \"Hey Buddy\"");
        else
            ESP_LOGI(TAG, "follow-up: heard \"%s\", his answer would be \"%s\": not you (noise or a TV); "
                     "not answering", s_heard.c_str(), s_said.c_str());
    }
    s_held.clear();
    s_held.shrink_to_fit();
}

// Gemini's voice: 24 kHz PCM in base64 -> 16 kHz, shaped for the small speaker.
static void playAudio(const char* b64) {
    static SpeechShaper shaper(kMicRate);
    const size_t len = strlen(b64);
    size_t bytes = 0;
    uint8_t* raw = (uint8_t*)psramMalloc(len * 3 / 4 + 4);
    if (!raw) return;
    if (mbedtls_base64_decode(raw, len * 3 / 4 + 4, &bytes, (const unsigned char*)b64, len) != 0 || bytes < 2) {
        heap_caps_free(raw);
        return;
    }
    const size_t in = bytes / 2;
    const size_t cap = s_resampler.maxOut(in);
    int16_t* out = (int16_t*)psramMalloc(cap * sizeof(int16_t));
    if (out) {
        const size_t n = s_resampler.process((const int16_t*)raw, in, out, cap);
        shaper.process(out, n);
        if (s_gate) s_held.insert(s_held.end(), out, out + n);  // wait: is it really for him?
        else if (!s_drop) playPcm(out, n);
        heap_caps_free(out);
    }
    heap_caps_free(raw);
}

static void finishReply() {
    // After a tool (remember, forget) Gemini's voice models sometimes say
    // nothing: ask it, once, to say what it did.
    if (s_usedTool && !s_nudged && !s_spoke && s_held.empty() && !s_cancelled.load()) {
        s_nudged = true;
        ESP_LOGI(TAG, "it used a tool and said nothing; asking it to say what it did");
        sendText("{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\"(Say briefly, out loud, "
                 "what you just did.)\"}]}],\"turnComplete\":true}}");
        s_deadline = nowMs() + kReplyMs;
        return;
    }
    if (s_gate && s_usedTool && s_held.empty()) openGate(false);
    else if (s_gate) openGate(!s_held.empty() && followUpWasYou());
    if (s_drop) {  // a follow-up that wasn't for him: the conversation ends quietly
        s_drop = false;
        s_cancelled.store(false);
        endTurn();
        post(AssistEvent::NotHeard);
        voiceResume();
        return;
    }
    const bool cancelled = s_cancelled.exchange(false);
    if (s_spoke) {
        soundSpeechEnd();
        while (soundSpeaking() && !s_cancelled.load()) vTaskDelay(pdMS_TO_TICKS(20));
        vTaskDelay(pdMS_TO_TICKS(250));  // let the speaker's echo die away before listening
    }
    ESP_LOGI(TAG, "heard \"%s\" -> said [%s] \"%s\"", s_heard.c_str(), s_emotion.c_str(), s_said.c_str());
    if (!s_heard.empty() || !s_said.empty()) {
        s_history.push_back({s_heard, s_said});
        while (s_history.size() > kHistory) s_history.pop_front();
    }
    const bool spoke = s_spoke, fromVoice = s_fromVoice;
    if (spoke || cancelled) post(AssistEvent::Done);
    else if (!cancelled) post(AssistEvent::Error, "Gemini didn't say anything");
    const std::string said = s_said;
    s_spoke = false;
    endTurn();
    if (s_closeAfterTurn) closeSession();
    if (!fromVoice) return;
    bool again = spoke && s_cfg.followUp && !cancelled && !isClarifyRequest(said) && s_followUps.load() < kMaxFollowUps;
    if (again && geminiBeginTurn(true)) {
        s_followUps.fetch_add(1);
        voiceFollowUp();
    } else {
        if (spoke && s_followUps.load() >= kMaxFollowUps) ESP_LOGI(TAG, "a few follow-ups in a row; say \"Hey Buddy\" to carry on");
        voiceResume();
    }
}

static void handleToolCall(const cJSON* call) {
    cJSON* resp = cJSON_CreateObject();
    cJSON* fr = cJSON_AddArrayToObject(cJSON_AddObjectToObject(resp, "toolResponse"), "functionResponses");
    const cJSON* fns = cJSON_GetObjectItem(call, "functionCalls");
    const cJSON* fn;
    int answered = 0;
    cJSON_ArrayForEach(fn, fns) {
        const cJSON* fname = cJSON_GetObjectItem(fn, "name");
        if (cJSON_IsString(fname) && strcmp(fname->valuestring, "look") == 0) {
            // Answered once the photo is taken (geminiLook).
            const cJSON* id = cJSON_GetObjectItem(fn, "id");
            const cJSON* q = cJSON_GetObjectItem(cJSON_GetObjectItem(fn, "args"), "question");
            s_lookId = cJSON_IsString(id) ? id->valuestring : "";
            s_lookPending.store(true);
            ESP_LOGI(TAG, "look: \"%s\"; taking a photo", cJSON_IsString(q) ? q->valuestring : "");
            post(AssistEvent::Look, cJSON_IsString(q) ? q->valuestring : "what do you see?");
            continue;
        }
        ++answered;
        const cJSON* name = cJSON_GetObjectItem(fn, "name");
        const cJSON* args = cJSON_GetObjectItem(fn, "args");
        const char* nm = cJSON_IsString(name) ? name->valuestring : "";
        std::string result = "unknown tool";
        std::string removed;
        if (strcmp(nm, "remember") == 0) {
            const cJSON* f = cJSON_GetObjectItem(args, "fact");
            const std::string fact = cJSON_IsString(f) ? f->valuestring : "";
            result = fact.empty() ? "nothing to remember"
                     : memoryRemember(fact) == MemoryResult::Added ? "written down" : "already known";
            ESP_LOGI(TAG, "remember \"%s\": %s", fact.c_str(), result.c_str());
        } else if (strcmp(nm, "sunglasses") == 0) {
            const cJSON* w = cJSON_GetObjectItem(args, "wear");
            const bool on = !(cJSON_IsString(w) && strcmp(w->valuestring, "off") == 0);
            AssistEvent ev{};
            ev.type = AssistEvent::Shades;
            ev.command = on ? 1 : 0;
            xQueueSend(s_events, &ev, pdMS_TO_TICKS(100));
            result = on ? "they're on" : "they're off";
        } else if (strcmp(nm, "forget") == 0) {
            const cJSON* w = cJSON_GetObjectItem(args, "what");
            const std::string what = cJSON_IsString(w) ? w->valuestring : "";
            result = memoryForget(what, &removed) == MemoryResult::Removed ? "crossed out: " + removed
                                                                            : "nothing like that was written down";
            ESP_LOGI(TAG, "forget \"%s\": %s", what.c_str(), result.c_str());
        }
        cJSON* r = cJSON_CreateObject();
        const cJSON* id = cJSON_GetObjectItem(fn, "id");
        if (cJSON_IsString(id)) cJSON_AddStringToObject(r, "id", id->valuestring);
        cJSON_AddStringToObject(r, "name", nm);
        cJSON_AddStringToObject(cJSON_AddObjectToObject(r, "response"), "result", result.c_str());
        cJSON_AddItemToArray(fr, r);
    }
    if (answered) sendJson(resp);
    else cJSON_Delete(resp);
    s_usedTool = true;
    s_deadline = nowMs() + kReplyMs;
}

static void handleMessage(const std::string& text) {
    cJSON* j = cJSON_ParseWithLength(text.data(), text.size());
    if (!j) {
        ESP_LOGW(TAG, "unreadable message (%u bytes)", (unsigned)text.size());
        return;
    }
    if (cJSON_GetObjectItem(j, "setupComplete")) {
        s_setupDone.store(true);
        s_connecting = false;
        s_sessionReady = true;
        s_status.store("ready");
        ESP_LOGI(TAG, "Gemini session ready");
    }
    if (const cJSON* call = cJSON_GetObjectItem(j, "toolCall")) handleToolCall(call);
    if (cJSON_GetObjectItem(j, "goAway")) s_closeAfterTurn = true;  // the session ends soon: start a new one next time
    if (const cJSON* sc = cJSON_GetObjectItem(j, "serverContent")) {
        const bool done = cJSON_IsTrue(cJSON_GetObjectItem(sc, "turnComplete"));
        // What you said can come back while you're still talking.
        if (!s_discard && s_stage != Stage::Idle)
            if (const cJSON* t = cJSON_GetObjectItem(cJSON_GetObjectItem(sc, "inputTranscription"), "text"))
                if (cJSON_IsString(t)) {
                    s_heard += t->valuestring;
                    // (Not used to decide anything: Gemini's transcript of what you
                    // said is often empty, or a phantom "¿Qué es el número de serie?",
                    // even when it understood you perfectly.)
                }
        if (s_discard || s_stage != Stage::WaitReply) {
            if (done) s_discard = false;
        } else {
            s_deadline = nowMs() + kReplyMs;
            if (const cJSON* t = cJSON_GetObjectItem(cJSON_GetObjectItem(sc, "outputTranscription"), "text")) {
                if (cJSON_IsString(t)) {
                    s_said += t->valuestring;
                    // A follow-up's answer, once a few words of it are in: in English it
                    // was you; answering in another language means it heard a TV or noise.
                    if (s_gate && s_said.size() >= 40) openGate(followUpWasYou());
                    // The face follows his words once there are a few of them.
                    const std::string e = emotionFromSpeech(s_said);
                    if (s_spoke && s_said.size() > 12 && e != s_emotion) {
                        s_emotion = e;
                        post(AssistEvent::Reply, "", e.c_str());
                    }
                }
            }
            const cJSON* parts = cJSON_GetObjectItem(cJSON_GetObjectItem(sc, "modelTurn"), "parts");
            const cJSON* part;
            cJSON_ArrayForEach(part, parts) {
                const cJSON* data = cJSON_GetObjectItem(cJSON_GetObjectItem(part, "inlineData"), "data");
                if (cJSON_IsString(data)) playAudio(data->valuestring);
            }
            if (cJSON_IsTrue(cJSON_GetObjectItem(sc, "interrupted"))) soundStopSpeech();
            if (done) finishReply();
        }
    }
    cJSON_Delete(j);
}

static void geminiTask(void*) {
    while (true) {
        std::string* msg = nullptr;
        if (xQueueReceive(s_inbox, &msg, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (msg == &s_connectedMarker) {
                ESP_LOGI(TAG, "connected; starting the session");
                sendSetup();
            } else if (msg == nullptr) {  // disconnected
                const bool busy = s_stage != Stage::Idle || s_turnPending.load() || s_sayPending.load();
                const bool duringSetup = s_connecting && !s_setupDone.load();
                closeSession();
                // Turned down while starting (not a bad key): try again asking for less,
                // keeping your question (the microphones' audio is still buffered).
                if (duringSetup && !s_keyRejected.load() && s_setupLevel.load() < 2 && busy && s_stage == Stage::Idle) {
                    s_setupLevel.fetch_add(1);
                    ESP_LOGW(TAG, "Gemini turned the session down; trying a simpler one");
                    connect();
                    continue;
                }
                if (!s_keyRejected.load()) s_status.store("idle");
                if (busy) {
                    s_turnPending.store(false);
                    s_sayPending.store(false);
                    failTurn(s_keyRejected.load() ? "Gemini didn't accept the key" : "lost the connection to Gemini");
                }
            } else if (msg == &s_photoMarker) {
                uint8_t* jpeg = s_photo;
                const size_t len = s_photoLen;
                s_photo = nullptr;
                if (s_lookPending.load() && s_sessionReady) sendPhoto(jpeg, len);
                else heap_caps_free(jpeg);
            } else if (msg != &s_wakeMarker) {
                handleMessage(*msg);
                delete msg;
            }
        }
        const uint32_t now = nowMs();
        const bool wanted = s_turnPending.load() || s_sayPending.load();
        if (wanted && !s_sessionReady && !s_connecting) connect();
        if (s_connecting && (int32_t)(now - s_deadline) > 0) {
            closeSession();
            if (wanted) {
                s_turnPending.store(false);
                s_sayPending.store(false);
                failTurn("couldn't reach Gemini");
            }
        }
        if (s_sessionReady && s_stage == Stage::Idle && !s_discard) {
            if (s_turnPending.load()) startTurn();
            else if (s_sayPending.load()) startSay();
        }
        if (s_stage == Stage::Streaming) pumpAudio();
        if (s_stage == Stage::WaitReply && (int32_t)(now - s_deadline) > 0) failTurn("Gemini took too long to answer");
        if (s_sessionReady && s_stage == Stage::Idle && !wanted && (int32_t)(now - s_lastUse) > (int32_t)kIdleCloseMs) {
            ESP_LOGI(TAG, "quiet for a while; closing the Gemini session");
            closeSession();
            s_status.store("idle");
        }
    }
}

// The photo for a look call: first the picture itself (as part of the
// conversation), then the answer to the call.
static void sendPhoto(uint8_t* jpeg, size_t len) {
    std::string result = "The camera couldn't take a photo just now; say so.";
    if (jpeg && len) {
        size_t olen = 0;
        mbedtls_base64_encode(nullptr, 0, &olen, jpeg, len);
        unsigned char* b64 = (unsigned char*)psramMalloc(olen + 1);
        if (b64 && mbedtls_base64_encode(b64, olen + 1, &olen, jpeg, len) == 0) {
            std::string msg = "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"inlineData\":"
                              "{\"mimeType\":\"image/jpeg\",\"data\":\"";
            msg.append((const char*)b64, olen);
            msg += "\"}},{\"text\":\"(The photo your camera just took.)\"}]}],\"turnComplete\":false}}";
            if (sendText(msg)) result = "Photo taken: it's the image just sent. Answer from what it shows; if it's "
                                        "unclear, say you can't quite tell.";
            ESP_LOGI(TAG, "sent the photo (%u KB)", (unsigned)(len / 1024));
        }
        heap_caps_free(b64);
    }
    heap_caps_free(jpeg);
    cJSON* resp = cJSON_CreateObject();
    cJSON* fr = cJSON_AddArrayToObject(cJSON_AddObjectToObject(resp, "toolResponse"), "functionResponses");
    cJSON* r = cJSON_CreateObject();
    if (!s_lookId.empty()) cJSON_AddStringToObject(r, "id", s_lookId.c_str());
    cJSON_AddStringToObject(r, "name", "look");
    cJSON_AddStringToObject(cJSON_AddObjectToObject(r, "response"), "result", result.c_str());
    cJSON_AddItemToArray(fr, r);
    sendJson(resp);
    s_lookId.clear();
    s_lookPending.store(false);
    s_deadline = nowMs() + kReplyMs;
}

// ---------------------------------------------------------------------------
// Called from assistant.cpp (and so from voice.cpp and the main loop).

bool geminiStart(const AssistantConfig& cfg, QueueHandle_t events) {
    if (cfg.geminiKey.empty()) return false;
    s_cfg = cfg;
    s_events = events;
    cJSON_Hooks hooks = {psramMalloc, heap_caps_free};
    cJSON_InitHooks(&hooks);
    s_inbox = xQueueCreate(48, sizeof(std::string*));
    s_audio = xStreamBufferCreateWithCaps(kMicRate * 2 * 6, 1, MALLOC_CAP_SPIRAM);  // 6 s while it connects
    if (!s_inbox || !s_audio) return false;
    if (xTaskCreatePinnedToCoreWithCaps(geminiTask, "gemini", 8192, nullptr, 5, nullptr, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "out of memory starting Gemini");
        return false;
    }
    s_status.store("idle");
    s_started.store(true);
    ESP_LOGI(TAG, "brain: Gemini (%s), name %s", cfg.geminiModel.c_str(), cfg.buddyName.c_str());
    return true;
}

bool geminiAvailable() {
    return s_started.load() && !s_keyRejected.load() && wifiConnected() && !s_turnActive.load();
}

const char* geminiStatus() {
    if (s_started.load() && !wifiConnected() && !s_keyRejected.load()) return "offline";
    return s_status.load();
}

bool geminiBeginTurn(bool followUp) {
    if (!followUp) s_cancelled.store(false);  // a fresh "Hey Buddy"
    if (!followUp) s_followUps.store(0);
    if (!s_started.load() || s_keyRejected.load() || !wifiConnected()) return false;
    bool expected = false;
    if (!s_turnActive.compare_exchange_strong(expected, true)) return false;
    s_followUpTurn.store(followUp);
    s_audioEnded.store(false);
    s_heardSpeech.store(false);
    xStreamBufferReset(s_audio);
    s_accepting.store(true);
    s_turnPending.store(true);
    std::string* m = &s_wakeMarker;  // wake the task up now
    xQueueSend(s_inbox, &m, 0);
    return true;
}

void geminiAudio(const int16_t* pcm, int samples) {
    if (!s_accepting.load()) return;
    xStreamBufferSend(s_audio, pcm, samples * sizeof(int16_t), 0);  // drops if it's far behind
}

void geminiEndAudio(bool heardSpeech) {
    s_heardSpeech.store(heardSpeech);
    s_audioEnded.store(true);
    if (heardSpeech) post(AssistEvent::Thinking);
}

bool geminiSay(const std::string& text) {
    if (!s_started.load() || s_keyRejected.load() || !wifiConnected()) return false;
    bool expected = false;
    if (!s_turnActive.compare_exchange_strong(expected, true)) return false;
    s_cancelled.store(false);
    s_sayText = text;
    s_sayPending.store(true);
    std::string* m = &s_wakeMarker;
    xQueueSend(s_inbox, &m, 0);
    return true;
}

bool geminiLookPending() { return s_lookPending.load(); }

void geminiLook(uint8_t* jpeg, size_t len) {
    if (!s_lookPending.load() || s_photo) {  // nobody asked (or still busy with the last one)
        heap_caps_free(jpeg);
        return;
    }
    s_photo = jpeg;  // (nullptr: the camera failed; Gemini is told so)
    s_photoLen = len;
    std::string* m = &s_photoMarker;
    if (xQueueSend(s_inbox, &m, pdMS_TO_TICKS(200)) != pdTRUE) {
        heap_caps_free(jpeg);
        s_photo = nullptr;
    }
}

bool geminiCancel() {
    if (!s_turnActive.load() && !soundSpeaking()) return false;
    s_cancelled.store(true);
    soundStopSpeech();
    voiceCancel();
    return true;
}
