#pragma once
#include <string>

// Buddy's notes ("remember that my dog's name is King"): kept in RAM, saved to
// memories.txt on the SD card. Changes can come from any task; the file is
// written by memoryService(), called from the UI task (the card shares the
// display's SPI bus).
void memoryLoad(bool edited = false);  // at startup, after the card is mounted (edited: changed on the settings page)
void memoryService();                  // UI task: writes the file if something changed

enum class MemoryResult { Added, AlreadyKnown, Removed, NotFound, Cleared, Empty };
MemoryResult memoryRemember(const std::string& fact);
MemoryResult memoryForgetLast(std::string* removed);
MemoryResult memoryForget(const std::string& what, std::string* removed);
MemoryResult memoryForgetAll();        // the old file is kept as memories-backup.txt

std::string memoryPromptBlock();       // for the AI ("" if nothing remembered)
size_t memoryCount();
#include <vector>
std::vector<std::string> memoryNotes();  // a copy of all notes, oldest first

// Shared with Buddy on the PC through Home Assistant's MQTT broker (a
// retained message): each change gets a higher version, and the highest wins.
uint32_t memoryVersion();
std::string memoryText();                 // memories.txt as it is now
bool memoryTakeChanged();                 // true once after a change made here (to share it)
void memoryMarkChanged(bool bumpVersion); // share the notes again (bumpVersion: as a newer version)
bool memoryApply(uint32_t version, const std::string& text);  // a newer shared copy: use it (false if not newer)
