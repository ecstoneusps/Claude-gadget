#pragma once
#include <string>

// microSD card on the CoreS3 (FAT32), mounted at /sdcard. It shares the SPI bus
// with the display; ESP-IDF's bus lock keeps the two from colliding.

bool sdcardMount();                      // false if no card or it can't be read
bool sdcardMounted();
bool sdcardReadFile(const char* path, std::string& out);           // path under /sdcard
bool sdcardWriteFile(const char* path, const std::string& data);
