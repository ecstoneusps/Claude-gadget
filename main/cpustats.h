#pragma once
#include <stdint.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Which tasks used the CPU over a stretch of time, for finding what starves
// a core. mark() starts the window; log() prints the busiest tasks since.
class CpuWindow {
public:
    void mark();
    void log(const char* label);

private:
    struct Sample {
        TaskHandle_t handle;
        configRUN_TIME_COUNTER_TYPE runtime;
    };
    std::vector<Sample> mark_;
    configRUN_TIME_COUNTER_TYPE total_ = 0;
};
