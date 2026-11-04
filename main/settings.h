#pragma once

// Small settings that survive restarts, kept in the ESP32's built-in flash
// key-value store (NVS). For bigger or user-editable settings, see the SD card
// plans in the README.

void settingsBegin();                                  // call once at startup
bool settingsGetBool(const char* key, bool fallback);  // fallback if never saved
void settingsSetBool(const char* key, bool value);     // only writes if it changed
