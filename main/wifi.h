#pragma once
#include <string>
#include <utility>
#include <vector>

// Wi-Fi station: connects at startup and keeps reconnecting in the background.
// Given several networks, it joins the first one in the list that's in range
// (the first is home), and looks again whenever the connection is lost.
// Needs NVS (settingsBegin()) first. Returns false if Wi-Fi couldn't start.
// `setupHotspot`: if no saved network works for 2 minutes (or none is saved),
// open the "Buddy Setup" network (settings page at http://192.168.71.1/).
bool wifiStart(const std::vector<std::pair<std::string, std::string>>& networks, bool setupHotspot = true);
bool wifiSetupHotspotOn();
bool wifiConnected();
bool wifiConnectedOrStarting();  // wifiStart() succeeded (it may still be connecting)
const char* wifiStatus();  // "off", "connecting", "connected", "wrong password", "no saved network in range"
int wifiNetworkIndex();    // which network it's on: 0 = the first (home), -1 = none
const char* wifiNetworkName();  // its name ("" if none)
