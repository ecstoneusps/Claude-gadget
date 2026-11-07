#include "memory.h"

#include <mutex>

#include "esp_log.h"

#include "Memory.h"
#include "sdcard.h"

static const char* TAG = "memory";
static constexpr const char* kFile = "memories.txt";
static constexpr const char* kBackup = "memories-backup.txt";

static std::mutex s_lock;
static MemoryStore s_store;
static bool s_dirty = false;
static std::string s_backup;  // text to write to the backup file, if any

void memoryLoad() {
    std::string text;
    std::lock_guard<std::mutex> g(s_lock);
    if (sdcardReadFile(kFile, text)) {
        s_store.load(text);
        ESP_LOGI(TAG, "%u note(s) in %s", (unsigned)s_store.notes().size(), kFile);
    } else if (sdcardMounted()) {
        ESP_LOGI(TAG, "no %s yet; it's created when you ask Buddy to remember something", kFile);
    }
}

void memoryService() {
    std::string text, backup;
    {
        std::lock_guard<std::mutex> g(s_lock);
        if (!s_dirty) return;
        s_dirty = false;
        text = s_store.toText();
        backup.swap(s_backup);
    }
    if (!sdcardMounted()) {
        ESP_LOGW(TAG, "no SD card: notes last until restart");
        return;
    }
    if (!backup.empty() && !sdcardWriteFile(kBackup, backup)) ESP_LOGW(TAG, "couldn't write %s", kBackup);
    if (sdcardWriteFile(kFile, text)) ESP_LOGI(TAG, "saved %s", kFile);
    else ESP_LOGW(TAG, "couldn't write %s", kFile);
}

MemoryResult memoryRemember(const std::string& fact) {
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_store.add(fact)) return MemoryResult::AlreadyKnown;
    s_dirty = true;
    ESP_LOGI(TAG, "remembered \"%s\" (%u notes)", fact.c_str(), (unsigned)s_store.notes().size());
    return MemoryResult::Added;
}

MemoryResult memoryForgetLast(std::string* removed) {
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_store.removeLast(removed)) return MemoryResult::Empty;
    s_dirty = true;
    ESP_LOGI(TAG, "forgot \"%s\"", removed ? removed->c_str() : "");
    return MemoryResult::Removed;
}

MemoryResult memoryForget(const std::string& what, std::string* removed) {
    std::lock_guard<std::mutex> g(s_lock);
    if (s_store.notes().empty()) return MemoryResult::Empty;
    if (!s_store.remove(what, removed)) return MemoryResult::NotFound;
    s_dirty = true;
    ESP_LOGI(TAG, "forgot \"%s\"", removed ? removed->c_str() : "");
    return MemoryResult::Removed;
}

MemoryResult memoryForgetAll() {
    std::lock_guard<std::mutex> g(s_lock);
    if (s_store.notes().empty()) return MemoryResult::Empty;
    s_backup = s_store.toText();
    s_store.clear();
    s_dirty = true;
    ESP_LOGI(TAG, "forgot everything (backup in %s)", kBackup);
    return MemoryResult::Cleared;
}

std::string memoryPromptBlock() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_store.promptBlock();
}

size_t memoryCount() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_store.notes().size();
}
