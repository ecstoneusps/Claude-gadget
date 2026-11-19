#include "memory.h"

#include <mutex>

#include "esp_log.h"

#include "Memory.h"
#include "sdcard.h"
#include "settings.h"

static const char* TAG = "memory";
static constexpr const char* kFile = "memories.txt";
static constexpr const char* kBackup = "memories-backup.txt";

static std::mutex s_lock;
static MemoryStore s_store;
static bool s_dirty = false;
static std::string s_backup;  // text to write to the backup file, if any
// Shared notes (with Buddy on the PC, through Home Assistant's MQTT broker):
// every change here gets a higher version number; the highest one wins.
static uint32_t s_version = 0;
static bool s_versionDirty = false;
static bool s_changed = false;  // changed here: share it

static void changedHere() {
    s_dirty = true;
    s_changed = true;
    ++s_version;
    s_versionDirty = true;
}

void memoryLoad(bool edited) {
    std::string text;
    std::lock_guard<std::mutex> g(s_lock);
    if (!edited) s_version = settingsGetU32("mem_ver", 0);
    if (edited) {  // changed on the settings page: share the new version
        s_changed = true;
        ++s_version;
        s_versionDirty = true;
    }
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
        if (s_versionDirty) {
            s_versionDirty = false;
            settingsSetU32("mem_ver", s_version);
        }
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
    changedHere();
    ESP_LOGI(TAG, "remembered \"%s\" (%u notes)", fact.c_str(), (unsigned)s_store.notes().size());
    return MemoryResult::Added;
}

MemoryResult memoryForgetLast(std::string* removed) {
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_store.removeLast(removed)) return MemoryResult::Empty;
    changedHere();
    ESP_LOGI(TAG, "forgot \"%s\"", removed ? removed->c_str() : "");
    return MemoryResult::Removed;
}

MemoryResult memoryForget(const std::string& what, std::string* removed) {
    std::lock_guard<std::mutex> g(s_lock);
    if (s_store.notes().empty()) return MemoryResult::Empty;
    if (!s_store.remove(what, removed)) return MemoryResult::NotFound;
    changedHere();
    ESP_LOGI(TAG, "forgot \"%s\"", removed ? removed->c_str() : "");
    return MemoryResult::Removed;
}

MemoryResult memoryForgetAll() {
    std::lock_guard<std::mutex> g(s_lock);
    if (s_store.notes().empty()) return MemoryResult::Empty;
    s_backup = s_store.toText();
    s_store.clear();
    changedHere();
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

std::vector<std::string> memoryNotes() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_store.notes();
}

uint32_t memoryVersion() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_version;
}

std::string memoryText() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_store.toText();
}

bool memoryTakeChanged() {
    std::lock_guard<std::mutex> g(s_lock);
    const bool c = s_changed;
    s_changed = false;
    return c;
}

void memoryMarkChanged(bool bumpVersion) {
    std::lock_guard<std::mutex> g(s_lock);
    s_changed = true;
    if (bumpVersion || s_version == 0) {
        ++s_version;
        s_versionDirty = true;
    }
}

bool memoryApply(uint32_t version, const std::string& text) {
    std::lock_guard<std::mutex> g(s_lock);
    if (version <= s_version) return false;
    s_store.load(text);
    s_version = version;
    s_versionDirty = true;
    s_dirty = true;  // save it to the card
    ESP_LOGI(TAG, "notes updated from the shared copy (version %u, %u notes)", (unsigned)version,
             (unsigned)s_store.notes().size());
    return true;
}
