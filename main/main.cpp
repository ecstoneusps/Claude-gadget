// Desk companion for the M5Stack CoreS3 (ESP-IDF).
//
// Pieces:
//   components/face       draws and animates the face (portable)
//   components/companion  decides how to react to events (portable, unit-tested)
//   main/voice.cpp        offline speech: "Hey Buddy" wake word + commands
//   main/audio_io.cpp     full-duplex audio: both mics + speaker at once
//   main/sound.cpp        sound effects through the speaker
//   main/proximity.cpp    proximity sensor: notices you sitting down
//   main/radar.cpp        LD2410 presence radar on Port C (optional; better at it)
//   main/camera.cpp       camera + face detection: looks at you
//   main/settings.cpp     settings kept across restarts (mute)
//   main/sdcard.cpp       microSD card; settings.txt is read from it at startup
//   components/config     parses settings.txt (portable, unit-tested)
//   main/wifi.cpp         Wi-Fi
//   main/assistant.cpp    talking with an AI through Home Assistant's voice assistant
//   components/assist     reply text and audio helpers for that (portable, unit-tested)
//   this file             hardware glue: display, touch, IMU, battery, main loop

#include <math.h>
#include <atomic>

#include <M5Unified.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "BuddyConfig.h"
#include "Companion.h"
#include "Face.h"
#include "Presence.h"
#include "assistant.h"
#include "audio_io.h"
#include "camera.h"
#include "cpustats.h"
#include "i2c_bus.h"
#include "proximity.h"
#include "radar.h"
#include "sdcard.h"
#include "settings.h"
#include "sound.h"
#include "voice.h"
#include "wifi.h"

static const char* TAG = "main";

constexpr uint32_t kFrameMs = 33;   // ~30 fps
constexpr float kShakeG = 1.0f;     // deviation from 1 g that counts as a shake
constexpr float kTiltG = 0.25f;     // tilt (vs. resting pose) before the eyes follow

// Everything adjustable lives in settings.txt on the SD card (see
// components/config/BuddyConfig.h for the defaults used without a card).
static BuddyConfig config;
static constexpr const char* kSettingsFile = "settings.txt";

static uint8_t percentTo255(int pct) { return (uint8_t)(pct * 255 / 100); }

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// Lets the portable face engine draw into an M5Canvas sprite.
class M5CanvasAdapter : public Canvas {
public:
    explicit M5CanvasAdapter(M5Canvas& c) : c_(c) {}
    int width() const override { return c_.width(); }
    int height() const override { return c_.height(); }
    void fillScreen(uint16_t col) override { c_.fillScreen(col); }
    void fillRect(int x, int y, int w, int h, uint16_t col) override { c_.fillRect(x, y, w, h, col); }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t col) override {
        c_.fillRoundRect(x, y, w, h, r, col);
    }
    void fillCircle(int x, int y, int r, uint16_t col) override { c_.fillCircle(x, y, r, col); }
    void fillEllipse(int x, int y, int rx, int ry, uint16_t col) override { c_.fillEllipse(x, y, rx, ry, col); }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col) override {
        c_.fillTriangle(x0, y0, x1, y1, x2, y2, col);
    }

private:
    M5Canvas& c_;
};

static M5Canvas canvas(&M5.Display);
static M5CanvasAdapter canvasAdapter(canvas);
static Face face;
static Companion buddy(face);
static QueueHandle_t voiceEvents;
static QueueHandle_t assistEvents;
static std::atomic<bool> aiStarted{false};
static std::atomic<bool> voiceOk{false};
static bool audioOk = false, cameraOkFlag = false, proxOkFlag = false;

static void handleTouch(uint32_t now) {
    if (M5.Touch.getCount() == 0) return;
    const auto t = M5.Touch.getDetail();
    if (t.isHolding()) buddy.onHold(now);
    else if (t.wasClicked()) buddy.onTap(now);
}

static void handleImu(uint32_t now) {
    static bool haveRest = false, tilted = false;
    static float restX = 0, restY = 0;

    float ax, ay, az;
    M5.Imu.update();
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    // Learn the resting pose and slowly re-learn it (~10 s) after it's moved.
    if (!haveRest) {
        restX = ax; restY = ay; haveRest = true;
    } else {
        restX += (ax - restX) * 0.003f;
        restY += (ay - restY) * 0.003f;
    }

    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (fabsf(mag - 1.0f) > kShakeG) buddy.onShake(now);

    // If the eyes look the wrong way on your unit, flip_tilt_x / flip_tilt_y in settings.txt.
    const float tx = (config.flipTiltX ? 1 : -1) * (ax - restX);
    const float ty = (config.flipTiltY ? -1 : 1) * (ay - restY);
    if (fabsf(tx) > kTiltG || fabsf(ty) > kTiltG) {
        buddy.onTilt(fminf(fmaxf(tx * 1.5f, -1), 1), fminf(fmaxf(ty * 1.5f, -1), 1), now);
        tilted = true;
    } else if (tilted) {
        buddy.onTiltEnd();
        tilted = false;
    }
}

// Proximity: greet you when you sit down, doze sooner when you leave.
// Logs the raw reading every 10 s so the thresholds can be tuned.
static PresenceDetector presence;

static RadarPresence radarPresence;

// Presence radar (when one is plugged in): takes over from the proximity
// sensor, since it notices you sitting still and further away.
static bool handleRadar(uint32_t now) {
    if (!config.radar) return false;
    radarPoll(now);
    if (!radarFound()) return false;
    static uint32_t lastLog = 0;
    const RadarReading& r = radarLatest();
    const bool fresh = radarFresh(now);
    const int cm = r.nearestCm();
    switch (radarPresence.update(fresh && r.someone(), cm, now)) {
        case RadarPresence::Event::Arrived:
            ESP_LOGI(TAG, "someone's here (radar: %d cm)", cm);
            buddy.onArrived(now);
            break;
        case RadarPresence::Event::Left:
            ESP_LOGI(TAG, "they left (radar)");
            buddy.onLeft(now);
            break;
        default:
            break;
    }
    if (now - lastLog > 10000) {
        lastLog = now;
        static const char* const kStates[] = {"nobody", "moving", "still", "moving + still"};
        if (!fresh) {
            ESP_LOGW(TAG, "radar: no readings for a while (unplugged?)");
        } else {
            ESP_LOGI(TAG, "radar: %s, nearest %d cm (moving %u cm/%u%%, still %u cm/%u%%), %s", kStates[r.state & 3],
                     cm, r.movingCm, r.movingEnergy, r.stillCm, r.stillEnergy,
                     radarPresence.present() ? "at the desk" : "not at the desk");
        }
    }
    return true;
}

static void handleProximity(uint32_t now, bool sensorOk) {
    static uint32_t lastRead = 0, lastLog = 0;
    if (!sensorOk || now - lastRead < 50) return;
    lastRead = now;

    uint16_t raw;
    if (!proximityRead(&raw)) return;
    switch (presence.update(raw, now)) {
        case PresenceDetector::Event::Arrived:
            ESP_LOGI(TAG, "someone's here (prox %u, baseline %.0f)", raw, presence.baseline());
            buddy.onArrived(now);
            break;
        case PresenceDetector::Event::Left:
            ESP_LOGI(TAG, "they left (prox %u, baseline %.0f)", raw, presence.baseline());
            buddy.onLeft(now);
            break;
        default:
            break;
    }
    if (now - lastLog > 10000) {
        lastLog = now;
        ESP_LOGI(TAG, "prox %u, baseline %.0f, %s", raw, presence.baseline(),
                 presence.present() ? "present" : "away");
    }
}

// Camera: steer the eyes toward your face.
static void handleFace(uint32_t now) {
    // Look often while awake, now and then while dozing (seeing you wakes it),
    // and not at all after "good night".
    uint32_t interval = buddy.isAsleep() ? 0 : buddy.wantsDim() ? 1000 : 200;
    // With a working radar, nobody at the desk means nothing to look at: no
    // face detection (it uses ~40% of core 0) until the radar sees someone.
    const bool nobodyHere = config.radar && radarFresh(now) && !radarPresence.present();
    if (nobodyHere) interval = 0;
    static bool paused = false;
    if (nobodyHere != paused) {
        paused = nobodyHere;
        ESP_LOGI(TAG, "face detection %s", paused ? "paused: nobody at the desk (radar)" : "resumed");
    }
    faceTrackingSetInterval(interval);

    static uint32_t lastSeq = 0;
    const FaceSighting f = faceTrackingLatest();
    if (f.seq == lastSeq) return;
    lastSeq = f.seq;
    if (!f.found) return;
    // With the camera facing you, stepping to your right moves you to the left of
    // the image, and the eyes should turn to *your* right (the screen's right).
    const float gx = (config.cameraMirrored ? f.x : -f.x) * config.faceGaze;
    const float gy = f.y * config.faceGaze;
    buddy.onFace(fminf(fmaxf(gx, -1), 1), fminf(fmaxf(gy, -1), 1), now);
}

static void handleVoice(uint32_t now) {
    if (!voiceOk.load()) return;
    // The mics stay on while the buddy makes sounds; don't let it react to its
    // own chirps as if someone clapped or spoke.
    if (!soundHeardRecently()) buddy.onAudio(voiceLevelDb(), voiceSpeech(), now);

    VoiceEvent ev;
    while (xQueueReceive(voiceEvents, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case VoiceEvent::WakeWord:
                buddy.onWakeWord(now);
                break;
            case VoiceEvent::Command:
                if (ev.prob >= config.commandConfidence) {
                    ESP_LOGI(TAG, "-> %s", commandName((VoiceCommand)ev.commandId));
                    buddy.onCommand((VoiceCommand)ev.commandId, now);
                } else {
                    buddy.onCommandTimeout(now);
                }
                break;
            case VoiceEvent::Timeout:
                buddy.onCommandTimeout(now);
                break;
            case VoiceEvent::FollowUp:
                buddy.onFollowUp(now);
                break;
            case VoiceEvent::FollowUpEnd:
                buddy.onFollowUpEnd(now);
                break;
        }
    }
}

static void startPhoto(bool look, const std::string& question, bool fromConversation, uint32_t now);

// The AI conversation: thinking, the answer's mood, the mouth while it speaks.
static void handleAssistant(uint32_t now) {
    if (!aiStarted.load()) return;
    AssistEvent ev;
    while (xQueueReceive(assistEvents, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case AssistEvent::Thinking: buddy.onHeard(now); break;
            case AssistEvent::Command:
                ESP_LOGI(TAG, "-> %s", commandName((VoiceCommand)ev.command));
                buddy.onCommand((VoiceCommand)ev.command, now);
                break;
            case AssistEvent::Reply: buddy.onReply(ev.emotion, now); break;
            case AssistEvent::Done: buddy.onReplyDone(now); break;
            case AssistEvent::NotHeard:
                if (ev.followUp) buddy.onFollowUpEnd(now);
                else buddy.onCommandTimeout(now);
                break;
            case AssistEvent::Error: buddy.onAssistantError(now); break;
            case AssistEvent::WakeAgain: buddy.onWakeWord(now); break;
            case AssistEvent::Look: startPhoto(true, ev.text, true, now); break;
        }
    }
    buddy.setSpeaking(soundSpeaking(), soundSpeechLevel());
}

// Loading the speech models takes a few seconds, so do it off the UI task
// and let the face animate meanwhile.
static void voiceInitTask(void*) {
    // Two-mic mode: core 0 when face tracking is off (core 1 is busy drawing).
    const int detectCore = config.faceTracking ? 1 : 0;
    if (voiceStart(voiceEvents, config.extraPhrases, config.twoMicrophones, detectCore)) voiceOk.store(true);
    else ESP_LOGW(TAG, "voice disabled; touch and motion still work");

    // The AI connection (Wi-Fi itself started at boot, see app_main).
    if (voiceOk.load() && config.aiConfigured() && wifiConnectedOrStarting()) {
        AssistantConfig ac;
        ac.url = config.haUrl;
        ac.token = config.haToken;
        ac.pipeline = config.haPipeline;
        ac.followUp = config.followUp;
        ac.extraPhrases = config.extraPhrases;
        ac.visionUrl = config.visionUrl;
        ac.visionModel = config.visionModel;
        aiStarted.store(assistantStart(ac, assistEvents));
    }
    vTaskDelete(nullptr);
}

// Reads settings.txt from the SD card, writing a commented default file the
// first time so every option is easy to find. Without a card: built-in defaults.
static void loadSettings() {
    if (sdcardMount()) {
        std::string text;
        if (sdcardReadFile(kSettingsFile, text)) {
            // Written by an older version: add the settings it doesn't have yet
            // (at their defaults, at the end of the file).
            const std::string extra = configUpgradeText(text);
            if (!extra.empty() && sdcardWriteFile(kSettingsFile, text + extra)) {
                int added = 0;
                for (size_t p = 0; (p = extra.find(" = ", p)) != std::string::npos; ++p) ++added;
                ESP_LOGI(TAG, "added %d new setting%s to the end of settings.txt", added, added == 1 ? "" : "s");
                text += extra;
            } else if (!extra.empty()) {
                ESP_LOGW(TAG, "couldn't add the new settings to settings.txt (card full or read-only?)");
            }
            std::vector<std::string> warnings;
            parseConfig(text, config, warnings);
            for (const auto& w : warnings) ESP_LOGW(TAG, "settings.txt %s", w.c_str());
            ESP_LOGI(TAG, "settings.txt loaded%s", warnings.empty() ? "" : " (some lines ignored)");
        } else if (sdcardWriteFile(kSettingsFile, defaultConfigText())) {
            ESP_LOGI(TAG, "wrote a default settings.txt to the SD card");
        } else {
            ESP_LOGW(TAG, "could not read or create settings.txt");
        }
    }
    buddy.sleepAfterMs = config.sleepAfterSeconds * 1000u;
    buddy.dimAfterMs = config.dimAfterSeconds * 1000u;
    buddy.sleepAfterLeftMs = config.sleepAfterLeavingSeconds * 1000u;
    presence.minRise = (float)config.proximitySensitivity;
    radarPresence.maxDistanceCm = config.radarDistanceCm;
    ESP_LOGI(TAG, "volume %d%%, brightness %d%%, sleep after %ds, face tracking %s, proximity %s",
             config.volume, config.brightness, config.sleepAfterSeconds,
             config.faceTracking ? "on" : "off", config.proximity ? "on" : "off");
    // Never log the token or password themselves.
    if (config.aiConfigured()) {
        ESP_LOGI(TAG, "AI conversation: Wi-Fi \"%s\", Home Assistant %s, voice assistant \"%s\", token set (%u chars)",
                 config.wifiName.c_str(), config.haUrl.c_str(), config.haPipeline.c_str(),
                 (unsigned)config.haToken.size());
    } else if (!config.conversation) {
        ESP_LOGI(TAG, "AI conversation: off (conversation = off)");
    } else {
        ESP_LOGI(TAG, "AI conversation: not set up (missing%s%s%s in settings.txt)",
                 config.wifiName.empty() ? " wifi_name" : "", config.haUrl.empty() ? " ha_url" : "",
                 config.haToken.empty() ? " ha_token" : "");
    }
}

// Diagnostic view: the camera image (pixel-doubled to fill the screen) with
// a box around the face it found.
static void drawCameraPreview() {
    static uint32_t seq = 0;
    static uint16_t* frame = (uint16_t*)heap_caps_malloc(kCamW * kCamH * 2, MALLOC_CAP_SPIRAM);
    static uint16_t* big = (uint16_t*)heap_caps_malloc(kCamW * kCamH * 8, MALLOC_CAP_SPIRAM);
    if (!frame || !big || !cameraCopyPreview(frame, &seq)) return;
    for (int y = 0; y < kCamH; ++y) {
        const uint16_t* src = frame + y * kCamW;
        uint16_t* d0 = big + (2 * y) * (2 * kCamW);
        uint16_t* d1 = d0 + 2 * kCamW;
        for (int x = 0; x < kCamW; ++x) d0[2 * x] = d0[2 * x + 1] = d1[2 * x] = d1[2 * x + 1] = src[x];
    }
    canvas.pushImage(0, 0, 2 * kCamW, 2 * kCamH, big);  // uint16_t* = big-endian RGB565 in M5GFX
    const FaceSighting f = faceTrackingLatest();
    if (f.found) {
        const int x = 2 * f.box[0], y = 2 * f.box[1], w = 2 * (f.box[2] - f.box[0]), h = 2 * (f.box[3] - f.box[1]);
        canvas.drawRect(x, y, w, h, TFT_GREEN);
        canvas.drawRect(x + 1, y + 1, w - 2, h - 2, TFT_GREEN);
    }
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setCursor(4, 4);
    canvas.printf("camera preview  %s", f.found ? "FACE" : "no face");
    canvas.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// "Take a picture" / "what do you see?": the camera takes over the screen for
// a few seconds (live view, countdown, flash, the photo), then the face is back.

static struct {
    enum Stage { Idle, Live, Show } stage = Idle;
    bool look = false;              // describe it (vision) rather than save it
    bool fromConversation = false;  // an AI turn is waiting for the answer
    std::string question;
    std::string caption;
    uint32_t since = 0;
    uint16_t* frame = nullptr;      // kPhotoW x kPhotoH, big-endian RGB565
} photo;

static void startPhoto(bool look, const std::string& question, bool fromConversation, uint32_t now) {
    if (photo.stage != photo.Idle || (!photo.frame && !(photo.frame = (uint16_t*)heap_caps_malloc(
                                                           kPhotoW * kPhotoH * 2, MALLOC_CAP_SPIRAM))) ||
        !photoBegin()) {
        ESP_LOGW(TAG, "couldn't start the camera for a photo");
        if (look && fromConversation) assistantLook(nullptr, 0, question, true);
        buddy.onAssistantError(now);
        return;
    }
    photo.stage = photo.Live;
    photo.look = look;
    photo.fromConversation = fromConversation;
    photo.question = question;
    photo.since = now;
    ESP_LOGI(TAG, "camera on for a %s", look ? "look" : "photo");
}

static void drawCaption(const char* text, int size, int y) {
    canvas.setTextSize(size);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString(text, kPhotoW / 2 + 2, y + 2);  // shadow, readable on any photo
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(text, kPhotoW / 2, y);
}

// Runs the photo screens; true while it owns the display (the face isn't drawn).
static bool runPhoto(uint32_t now) {
    if (photo.stage == photo.Idle) return false;
    const uint32_t t = now - photo.since;
    if (photo.stage == photo.Live) {
        // Live view: time to aim (and for the camera's exposure to settle).
        const uint32_t liveMs = photo.look ? 1800 : 3000;
        static bool gotFrame = false;
        if (t == 0 || t < kFrameMs) gotFrame = false;
        if (photoPreview(photo.frame)) {
            gotFrame = true;
            canvas.pushImage(0, 0, kPhotoW, kPhotoH, photo.frame);
        } else if (!gotFrame) {
            canvas.fillScreen(TFT_BLACK);  // until the first frame
        }
        if (photo.look) {
            drawCaption("Let me see...", 2, 8);
        } else {
            drawCaption("Say cheese!", 2, 8);
            const int left = (int)((liveMs - (t < liveMs ? t : liveMs) + 999) / 1000);
            if (left > 0) drawCaption(std::to_string(left).c_str(), 6, 90);
        }
        canvas.pushSprite(0, 0);
        if (t < liveMs) return true;

        // Click. With photo_flash, the screen lights the scene at full
        // brightness while the photo is exposed; otherwise the white screen
        // afterwards is just the "click".
        uint8_t* jpeg = nullptr;
        size_t len = 0;
        if (config.photoFlash) {
            canvas.fillScreen(TFT_WHITE);
            canvas.pushSprite(0, 0);
            M5.Display.setBrightness(255);
        }
        const bool ok = photoCapture(&jpeg, &len, photo.frame, config.photoFlash);
        photoEnd();
        if (config.photoFlash) {
            M5.Display.setBrightness(percentTo255(buddy.wantsDim() ? config.dimBrightness : config.brightness));
        } else {
            canvas.fillScreen(TFT_WHITE);
            canvas.pushSprite(0, 0);
        }
        if (!buddy.isMuted()) soundPlay(Sfx::Boop);
        if (!ok) {
            photo.caption = "Oops, no photo";
            heap_caps_free(jpeg);
            if (photo.look && photo.fromConversation) assistantLook(nullptr, 0, photo.question, true);
            buddy.onAssistantError(now);
        } else if (!photo.look) {
            const std::string name = sdcardSavePhoto(jpeg, len);
            heap_caps_free(jpeg);
            photo.caption = !sdcardMounted() ? "No SD card to save it on" : name.empty() ? "Couldn't save it" : name;
            if (!name.empty()) ESP_LOGI(TAG, "saved photos/%s", name.c_str());
        } else if (aiStarted.load() && assistantCanSee()) {
            photo.caption = "Hmm...";
            assistantLook(jpeg, len, photo.question, photo.fromConversation);  // takes the JPEG
        } else {
            heap_caps_free(jpeg);
            photo.caption = "I need the AI to tell you";
            if (photo.fromConversation) assistantLook(nullptr, 0, photo.question, true);
        }
        photo.stage = photo.Show;
        photo.since = now;
        vTaskDelay(pdMS_TO_TICKS(80));  // let the flash be seen
        return true;
    }
    // The photo, briefly.
    canvas.pushImage(0, 0, kPhotoW, kPhotoH, photo.frame);
    drawCaption(photo.caption.c_str(), 2, kPhotoH - 28);
    canvas.pushSprite(0, 0);
    if (t >= (photo.look ? 1500u : 2500u)) photo.stage = photo.Idle;
    return true;
}

// "Louder" / "quieter": 10 steps at a time, saved into settings.txt a few
// seconds after the last change (so "louder... louder" writes the card once).
static void handleVolumeChange(uint32_t now) {
    static uint32_t saveAt = 0;
    const int steps = buddy.takeVolumeChange();
    if (steps != 0) {
        const int old = config.volume;
        config.volume = config.volume + steps * 10;
        if (config.volume > 100) config.volume = 100;
        if (config.volume < 10) config.volume = 10;  // "quieter" never goes silent; "be quiet" does that
        soundSetVolume(config.volume);
        ESP_LOGI(TAG, "volume %d%% -> %d%%%s", old, config.volume, config.volume == 100 ? " (maximum)" : "");
        saveAt = now + 3000;
        if (saveAt == 0) saveAt = 1;
    }
    if (saveAt != 0 && (int32_t)(now - saveAt) >= 0) {
        saveAt = 0;
        std::string text;
        if (!sdcardMounted()) {
            ESP_LOGW(TAG, "no SD card: the new volume lasts until restart");
        } else if (sdcardReadFile(kSettingsFile, text) &&
                   sdcardWriteFile(kSettingsFile, configSetValue(text, "volume", std::to_string(config.volume)))) {
            ESP_LOGI(TAG, "saved volume = %d to settings.txt", config.volume);
        } else {
            ESP_LOGW(TAG, "couldn't save the volume to settings.txt");
        }
    }
}

static void uiTask(void*) {
    const bool proxOk = config.proximity && proximityBegin();
    if (config.radar) radarBegin();
    proxOkFlag = proxOk;
    uint32_t lastStatus = 0, frames = 0;
    uint32_t lastFrame = nowMs(), lastBattery = 0, lastImu = 0;
    bool dimmed = false;
    while (true) {
        const uint32_t now = nowMs();
        M5.update();
        handleTouch(now);
        if (now - lastImu >= 20) {  // 50 Hz
            lastImu = now;
            handleImu(now);
        }
        handleVoice(now);
        handleAssistant(now);
        if (!handleRadar(now)) handleProximity(now, proxOk);
        handleFace(now);

        // One-line health summary every 30 s, so a log opened at any time
        // shows what started (the USB monitor disconnects on every reset).
        if (lastStatus == 0 || now - lastStatus > 30000) {
            ESP_LOGI(TAG, "status: %d fps, audio %s, voice %s (%s), wifi %s, ai %s, sd %s, camera %s, "
                          "proximity %s, radar %s, battery %d%%%s, usb %d mV, free PSRAM %u KB, free internal %u KB",
                     (int)(frames * 1000 / (now - lastStatus + 1)),
                     audioOk ? "ok" : "FAILED", voiceOk.load() ? "ready" : "not ready", voiceFrontEnd(),
                     wifiStatus(), aiStarted.load() ? assistantStatus() : "off",
                     sdcardMounted() ? "ok" : "none", cameraOkFlag ? "ok" : "off/FAILED",
                     proxOkFlag ? "ok" : "off/FAILED",
                     !config.radar ? "off" : radarFresh(now) ? "ok" : radarFound() ? "LOST" : "none",
                     (int)M5.Power.getBatteryLevel(),
                     M5.Power.isCharging() ? " charging" : "", (int)M5.Power.getVBUSVoltage(),
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
            static CpuWindow cpu;
            if (lastStatus != 0) cpu.log("cpu last 30 s");
            cpu.mark();
            lastStatus = now;
            frames = 0;
        }

        if (lastBattery == 0 || now - lastBattery > 10000) {
            lastBattery = now;
            // Low battery = actually running on a battery that's nearly empty.
            // On USB power, or with no battery fitted (it reads 0%), it isn't.
            const int level = (int)M5.Power.getBatteryLevel();
            const bool onUsb = M5.Power.getVBUSVoltage() > 4000;
            buddy.setLowBattery(!onUsb && level > 0 && level < 10);
        }

        buddy.update(now);
        static bool savedMuted = buddy.isMuted();
        if (buddy.isMuted() != savedMuted) {  // "be quiet" / "sound on" was said
            savedMuted = buddy.isMuted();
            settingsSetBool("muted", savedMuted);
        }
        handleVolumeChange(now);
        switch (buddy.takeCameraRequest()) {  // "take a picture" / offline "what is this"
            case Companion::CameraRequest::Photo: startPhoto(false, "", false, now); break;
            case Companion::CameraRequest::Look: startPhoto(true, "What do you see?", false, now); break;
            default: break;
        }
        soundPlay(buddy.takeSound());  // after the volume change, so "louder" chirps at the new level
        if (buddy.wantsDim() != dimmed) {
            dimmed = buddy.wantsDim();
            M5.Display.setBrightness(percentTo255(dimmed ? config.dimBrightness : config.brightness));
        }

        bool drew = false;
        if (now - lastFrame >= kFrameMs) {
            drew = true;
            face.update(now - lastFrame);
            lastFrame = now;
            ++frames;
            if (runPhoto(now)) {
                // the camera has the screen
            } else if (config.cameraPreview) {
                drawCameraPreview();
            } else {
                face.draw(canvasAdapter);
                canvas.pushSprite(0, 0);
            }
        }
        // Drawing a frame takes most of core 1; rest a little after each one so
        // everything else on this core (reply download, sound, camera driver,
        // idle task) gets a turn. Without it the task watchdog fired while a
        // reply was being fetched.
        vTaskDelay(pdMS_TO_TICKS(drew ? 6 : 2));
    }
}

extern "C" void app_main(void) {
    auto cfg = M5.config();
    cfg.internal_mic = false;  // audio_io.cpp drives the mics and speaker itself (full duplex)
    cfg.internal_spk = false;
    M5.begin(cfg);
    M5.Display.setRotation(1);  // landscape, 320x240
    loadSettings();
    M5.Display.setBrightness(percentTo255(config.brightness));

    canvas.setColorDepth(16);
    canvas.setPsram(true);
    if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
        M5.Display.println("Sprite alloc failed");
        ESP_LOGE(TAG, "sprite alloc failed");
        return;
    }

    face.setSeed(esp_random());
    settingsBegin();
    buddy.setMuted(settingsGetBool("muted", false));
    voiceEvents = xQueueCreate(8, sizeof(VoiceEvent));
    assistEvents = xQueueCreate(8, sizeof(AssistEvent));
    // Wi-Fi first, while internal RAM is still in one piece: it needs a few
    // contiguous blocks that weren't left once the camera and models loaded.
    if (config.aiConfigured()) wifiStart(config.wifiName, config.wifiPassword);
    sharedI2cBus();                       // before other tasks touch the I2C bus
    // The camera only runs if something uses it: it captures frames
    // continuously once started, even with nothing looking at them.
    const bool cameraOk = (config.faceTracking || config.cameraPreview) && cameraBegin();
    const bool micOk = audioBegin();
    audioOk = micOk;
    cameraOkFlag = cameraOk;
    soundInit();
    soundSetVolume(config.volume);

    // Speech runs on core 0; drawing and input get core 1.
    xTaskCreatePinnedToCore(uiTask, "ui", 8192, nullptr, 4, nullptr, 1);
    if (cameraOk && config.cameraPreview) cameraEnablePreview();
    if (cameraOk && (config.faceTracking || config.cameraPreview)) faceTrackingStart(config.accurateFaceModel);
    if (micOk) xTaskCreatePinnedToCore(voiceInitTask, "sr_init", 8192, nullptr, 3, nullptr, 0);
    ESP_LOGI(TAG, "desk companion started");
}
