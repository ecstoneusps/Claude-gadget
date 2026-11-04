#pragma once

// Small settings that survive restarts, kept in the ESP32's built-in flash
// key-value store (NVS). User-editable settings live in settings.txt on the SD
// card instead (components/config, main/sdcard.cpp).

void settingsBegin();                                  // call once at startup
bool settingsGetBool(const char* key, bool fallback);  // fallback if never saved
void settingsSetBool(const char* key, bool value);     // only writes if it changed
