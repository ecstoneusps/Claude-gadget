#pragma once
#include <stdint.h>

// CoreS3 camera (GC0308) + Espressif's on-device face detection model.
// A background task on core 1 grabs frames and looks for the largest face.

// Set up the camera. Call from app_main after M5.begin() and before any other
// task uses the internal I2C bus (the camera shares it with M5Unified).
bool cameraBegin();

// Start the face-detection task (loads the model; takes a moment).
void faceTrackingStart();

// How often to look: 0 pauses detection (e.g. while asleep).
void faceTrackingSetInterval(uint32_t ms);

struct FaceSighting {
    uint32_t seq;   // increments with every processed frame
    bool found;     // a face was in that frame
    float x, y;     // face center, -1..1 in camera image coordinates (x right, y down)
    float size;     // face height as a fraction of the frame height
};

// Latest result. Safe to call from any task.
FaceSighting faceTrackingLatest();
