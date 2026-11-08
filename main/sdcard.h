#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

// microSD card on the CoreS3 (FAT32), mounted at /sdcard. It shares the SPI bus
// with the display; ESP-IDF's bus lock keeps the two from colliding.

bool sdcardMount();                      // false if no card or it can't be read
bool sdcardMounted();
bool sdcardReadFile(const char* path, std::string& out);           // path under /sdcard
bool sdcardWriteFile(const char* path, const std::string& data);
#include <vector>
// Whole file into `out` (up to maxBytes). False if missing, unreadable or too big.
bool sdcardReadBinary(const char* path, std::vector<uint8_t>& out, size_t maxBytes);

// Saves a photo as photos/IMG_0001.jpg, IMG_0002.jpg... (the next free
// number). Returns the file name ("" if it couldn't be saved).
std::string sdcardSavePhoto(const uint8_t* jpeg, size_t len);
