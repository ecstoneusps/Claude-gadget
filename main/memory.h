#pragma once
#include <string>

// Buddy's notes ("remember that my dog's name is King"): kept in RAM, saved to
// memories.txt on the SD card. Changes can come from any task; the file is
// written by memoryService(), called from the UI task (the card shares the
// display's SPI bus).
void memoryLoad();                     // at startup, after the card is mounted
void memoryService();                  // UI task: writes the file if something changed

enum class MemoryResult { Added, AlreadyKnown, Removed, NotFound, Cleared, Empty };
MemoryResult memoryRemember(const std::string& fact);
MemoryResult memoryForgetLast(std::string* removed);
MemoryResult memoryForget(const std::string& what, std::string* removed);
MemoryResult memoryForgetAll();        // the old file is kept as memories-backup.txt

std::string memoryPromptBlock();       // for the AI ("" if nothing remembered)
size_t memoryCount();
