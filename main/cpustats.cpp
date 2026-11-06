#include "cpustats.h"

#include <algorithm>
#include <string.h>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "cpu";


static std::vector<TaskStatus_t> snapshot(configRUN_TIME_COUNTER_TYPE* total) {
    std::vector<TaskStatus_t> tasks(uxTaskGetNumberOfTasks() + 4);
    const UBaseType_t n = uxTaskGetSystemState(tasks.data(), tasks.size(), total);
    tasks.resize(n);
    return tasks;
}

void CpuWindow::mark() {
    configRUN_TIME_COUNTER_TYPE total;
    mark_.clear();
    for (const auto& t : snapshot(&total)) mark_.push_back({t.xHandle, t.ulRunTimeCounter});
    total_ = total;
}

void CpuWindow::log(const char* label) {
    configRUN_TIME_COUNTER_TYPE total;
    auto tasks = snapshot(&total);
    const double elapsed = (double)(total - total_);  // microseconds, per core
    if (elapsed <= 0) return;
    struct Use {
        const char* name;
        int core;
        double pct;
    };
    std::vector<Use> use;
    for (const auto& t : tasks) {
        configRUN_TIME_COUNTER_TYPE before = 0;
        for (const auto& m : mark_)
            if (m.handle == t.xHandle) before = m.runtime;
        const double pct = 100.0 * (double)(t.ulRunTimeCounter - before) / elapsed;
        if (pct >= 1.0) use.push_back({t.pcTaskName, t.xCoreID > 1 ? -1 : (int)t.xCoreID, pct});
    }
    std::sort(use.begin(), use.end(), [](const Use& a, const Use& b) { return a.pct > b.pct; });
    char line[256];
    int len = snprintf(line, sizeof line, "%s (%.1f s):", label, elapsed / 1e6);
    for (size_t i = 0; i < use.size() && i < 9 && len < (int)sizeof line - 24; ++i)
        len += snprintf(line + len, sizeof line - len, " %s/%c %.0f%%", use[i].name,
                        use[i].core < 0 ? '*' : '0' + use[i].core, use[i].pct);
    ESP_LOGI(TAG, "%s", line);
}
