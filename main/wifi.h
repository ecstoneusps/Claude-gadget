#pragma once
#include <string>

// Wi-Fi station: connects at startup and keeps reconnecting in the background.
// Needs NVS (settingsBegin()) first. Returns false if Wi-Fi couldn't start.
bool wifiStart(const std::string& ssid, const std::string& password);
bool wifiConnected();
bool wifiConnectedOrStarting();  // wifiStart() succeeded (it may still be connecting)
const char* wifiStatus();  // "off", "connecting", "connected", "wrong password", "network not found"
