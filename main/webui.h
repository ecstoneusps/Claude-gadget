#pragma once
#include <stdint.h>
#include <string>

// A settings page in your browser: http://<Buddy's address>/ edits
// settings.txt without taking the SD card out. Wi-Fi passwords and the Home
// Assistant token are never sent to the browser (they show as ********, and
// leaving them that way keeps them). A change only takes effect once someone
// taps Buddy's screen to accept it, so nobody else on the network can change
// him unnoticed; then he restarts with the new settings.

void webuiStart(const char* settingsFile);  // after Wi-Fi has started

// For the UI task: a change is waiting for a tap (shown on the screen).
bool webuiWaiting(uint32_t now);  // false again after ~60 s without a tap
void webuiAccept();               // tapped: saves settings.txt (then restart)
void webuiDecline();              // e.g. ignored too long

std::string webuiAddress();  // "http://192.168.4.134/" or "" while not connected
