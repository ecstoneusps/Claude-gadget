#include "photonotes.h"

#include <dirent.h>
#include <stdio.h>

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include "esp_log.h"
#include "sdcard.h"

static const char* TAG = "photonotes";
static constexpr size_t kKeep = 5;  // the newest this many go to the AI

static std::mutex s_lock;
static std::vector<std::pair<int, std::string>> s_notes;  // (photo number, description), newest first
static std::vector<std::pair<std::string, std::string>> s_toWrite;  // (txt path, description)
static int s_latest = 0;  // highest photo number seen

static int photoNumber(const std::string& name) {
    int n = 0;
    return (sscanf(name.c_str(), "IMG_%d.", &n) == 1 || sscanf(name.c_str(), "img_%d.", &n) == 1) ? n : 0;
}

static std::string photoName(int n) {
    char buf[24];
    snprintf(buf, sizeof buf, "IMG_%04d.jpg", n);
    return buf;
}

static void addLocked(int n, const std::string& desc) {
    s_notes.erase(std::remove_if(s_notes.begin(), s_notes.end(), [&](const auto& p) { return p.first == n; }),
                  s_notes.end());
    s_notes.push_back({n, desc});
    std::sort(s_notes.begin(), s_notes.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (s_notes.size() > kKeep) s_notes.resize(kKeep);
    s_latest = std::max(s_latest, n);
}

void photoNotesLoad() {
    if (!sdcardMounted()) return;
    // The card is mounted at /sdcard; find the photos and their descriptions.
    std::vector<int> jpgs, txts;
    if (DIR* d = opendir("/sdcard/photos")) {
        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            const int n = photoNumber(name);
            if (n <= 0) continue;
            const std::string ext = name.size() > 4 ? name.substr(name.size() - 4) : "";
            if (ext == ".jpg" || ext == ".JPG") jpgs.push_back(n);
            else if (ext == ".txt" || ext == ".TXT") txts.push_back(n);
        }
        closedir(d);
    }
    std::sort(txts.rbegin(), txts.rend());
    std::lock_guard<std::mutex> g(s_lock);
    for (int n : jpgs) s_latest = std::max(s_latest, n);
    for (size_t i = 0; i < txts.size() && i < kKeep; ++i) {
        char path[40];
        snprintf(path, sizeof path, "photos/IMG_%04d.txt", txts[i]);
        std::string desc;
        if (sdcardReadFile(path, desc)) {
            while (!desc.empty() && (desc.back() == '\n' || desc.back() == '\r' || desc.back() == ' ')) desc.pop_back();
            if (!desc.empty()) addLocked(txts[i], desc);
        }
    }
    if (s_latest) ESP_LOGI(TAG, "%u photo description(s); newest photo %s", (unsigned)s_notes.size(), photoName(s_latest).c_str());
}

void photoNotesService() {
    std::vector<std::pair<std::string, std::string>> todo;
    {
        std::lock_guard<std::mutex> g(s_lock);
        todo.swap(s_toWrite);
    }
    for (const auto& [path, desc] : todo)
        if (!sdcardWriteFile(path.c_str(), desc + "\n")) ESP_LOGW(TAG, "couldn't save %s", path.c_str());
}

void photoNotesSaved(const std::string& photo) {
    std::lock_guard<std::mutex> g(s_lock);
    s_latest = std::max(s_latest, photoNumber(photo));
}

void photoNotesAdd(const std::string& photo, const std::string& description) {
    const int n = photoNumber(photo);
    if (n <= 0 || description.empty()) return;
    char path[40];
    snprintf(path, sizeof path, "photos/IMG_%04d.txt", n);
    std::lock_guard<std::mutex> g(s_lock);
    addLocked(n, description);
    s_toWrite.push_back({path, description});
    ESP_LOGI(TAG, "%s: %s", photo.c_str(), description.c_str());
}

std::string photoNotesPromptBlock() {
    std::lock_guard<std::mutex> g(s_lock);
    if (s_notes.empty()) return "";
    std::string b = "[Photos you took recently, newest first (from their descriptions): ";
    for (size_t i = 0; i < s_notes.size(); ++i)
        b += (i ? "; " : "") + photoName(s_notes[i].first) + ": " + s_notes[i].second;
    b += ".]\n";
    return b;
}

std::string photoNotesLatest() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_latest ? photoName(s_latest) : "";
}
