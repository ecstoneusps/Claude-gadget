#pragma once
#include <stddef.h>
#include <stdint.h>

// CoreS3 camera (GC0308) + Espressif's on-device face detection model.
// A background task on core 1 grabs frames and looks for the largest face.

// Set up the camera. Call from app_main after M5.begin() and before any other
// task uses the internal I2C bus (the camera shares it with M5Unified).
bool cameraBegin();

// Start the face-detection task (loads the model; takes a moment).
// `accurate`: ESPDet-Pico (finds faces far more reliably, ~3x slower per
// frame) instead of the fast two-stage MSR+MNP model.
void faceTrackingStart(bool accurate = true);

// How often to look: 0 pauses detection (e.g. while asleep).
void faceTrackingSetInterval(uint32_t ms);

struct FaceSighting {
    uint32_t seq;   // increments with every processed frame
    bool found;     // a face was in that frame
    float x, y;     // face center, -1..1 in camera image coordinates (x right, y down)
    float size;     // face height as a fraction of the frame height
    int box[4];     // best face in pixels: x1, y1, x2, y2
};

// Latest result. Safe to call from any task.
FaceSighting faceTrackingLatest();

// Camera frame size. The detection model works at 160x120 anyway, and a
// smaller frame means a quarter of the memory traffic.
constexpr int kCamW = 160, kCamH = 120;

// Camera preview (a diagnostic, `camera_preview = on` in settings.txt): keeps
// a copy of the latest kCamW x kCamH frame. Copies it into `dst` (big-endian
// RGB565, as the camera delivers it) if a newer one arrived since the last call.
void cameraEnablePreview();
bool cameraCopyPreview(uint16_t* dst, uint32_t* seq);

// Photos ("take a picture", "what do you see"): the camera runs at 320x240
// only between photoBegin() and photoEnd() (face tracking, if on, pauses
// meanwhile). Call these from one task.
constexpr int kPhotoW = 320, kPhotoH = 240;  // live view / still size: the screen
bool photoBegin();
// Live view: the newest frame at half size into `dst` (kPhotoW x kPhotoH,
// big-endian RGB565). False if no frame was ready.
bool photoPreview(uint16_t* dst);
// Takes the photo: a JPEG (heap_caps_malloc'd in PSRAM; caller frees) and,
// if `still` isn't null, a half-size copy for showing it.
// `fresh`: skip frames already taken and wait for the next (for a flash,
// which has to be lit while the frame is exposed).
bool photoCapture(uint8_t** jpeg, size_t* len, uint16_t* still, bool fresh = false);
void photoEnd();
