// Desk companion for the M5Stack CoreS3 (ESP-IDF).
//
// Pieces:
//   components/face       draws and animates the face (portable)
//   components/companion  decides how to react to events (portable, unit-tested)
//   main/voice.cpp        offline speech: "Hey Buddy" wake word + commands
//   main/sound.cpp        sound effects through the speaker
//   main/proximity.cpp    proximity sensor: notices you sitting down
//   main/camera.cpp       camera + face detection: looks at you
//   main/settings.cpp     settings kept across restarts (mute)
//   main/sdcard.cpp       microSD card; settings.txt is read from it at startup
//   components/config     parses settings.txt (portable, unit-tested)
//   this file             hardware glue: display, touch, IMU, battery, main loop

#include <math.h>
#include <atomic>

#include <M5Unified.h>
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
#include "camera.h"
#include "proximity.h"
#include "sdcard.h"
#include "settings.h"
#include "sound.h"
#include "voice.h"

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
static std::atomic<bool> voiceOk{false};

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
    faceTrackingSetInterval(buddy.isAsleep() ? 0 : buddy.wantsDim() ? 1000 : 200);

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
    buddy.onAudio(voiceLevelDb(), voiceSpeech(), now);

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
        }
    }
}

// Loading the speech models takes a few seconds, so do it off the UI task
// and let the face animate meanwhile.
static void voiceInitTask(void*) {
    if (voiceStart(voiceEvents, config.extraPhrases)) voiceOk.store(true);
    else ESP_LOGW(TAG, "voice disabled; touch and motion still work");
    vTaskDelete(nullptr);
}

// Reads settings.txt from the SD card, writing a commented default file the
// first time so every option is easy to find. Without a card: built-in defaults.
static void loadSettings() {
    if (sdcardMount()) {
        std::string text;
        if (sdcardReadFile(kSettingsFile, text)) {
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
    ESP_LOGI(TAG, "volume %d%%, brightness %d%%, sleep after %ds, face tracking %s, proximity %s",
             config.volume, config.brightness, config.sleepAfterSeconds,
             config.faceTracking ? "on" : "off", config.proximity ? "on" : "off");
}

static void uiTask(void*) {
    const bool proxOk = config.proximity && proximityBegin();
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
        handleProximity(now, proxOk);
        handleFace(now);

        if (lastBattery == 0 || now - lastBattery > 10000) {
            lastBattery = now;
            buddy.setLowBattery(!M5.Power.isCharging() && M5.Power.getBatteryLevel() < 10);
        }

        buddy.update(now);
        static bool savedMuted = buddy.isMuted();
        if (buddy.isMuted() != savedMuted) {  // "be quiet" / "sound on" was said
            savedMuted = buddy.isMuted();
            settingsSetBool("muted", savedMuted);
        }
        soundPlay(buddy.takeSound());
        soundUpdate(now);
        if (buddy.wantsDim() != dimmed) {
            dimmed = buddy.wantsDim();
            M5.Display.setBrightness(percentTo255(dimmed ? config.dimBrightness : config.brightness));
        }

        if (now - lastFrame >= kFrameMs) {
            face.update(now - lastFrame);
            lastFrame = now;
            face.draw(canvasAdapter);
            canvas.pushSprite(0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

extern "C" void app_main(void) {
    auto cfg = M5.config();
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
    const bool cameraOk = cameraBegin();  // before other tasks touch the I2C bus
    const bool micOk = voiceMicBegin();
    soundInit();
    soundSetVolume(percentTo255(config.volume));

    // Speech runs on core 0; drawing and input get core 1.
    xTaskCreatePinnedToCore(uiTask, "ui", 8192, nullptr, 4, nullptr, 1);
    if (cameraOk && config.faceTracking) faceTrackingStart();
    if (micOk) xTaskCreatePinnedToCore(voiceInitTask, "sr_init", 8192, nullptr, 3, nullptr, 0);
    ESP_LOGI(TAG, "desk companion started");
}
