#pragma once
#include <string>

// What Buddy's photos show: when he takes a picture, the vision model writes
// a short description; it's saved next to the photo (photos/IMG_0007.txt) and
// the newest few are given to the AI, so "what was in the picture you took?"
// can be answered. Descriptions can come from any task; files are written by
// photoNotesService() in the UI task (the card shares the display's SPI bus).

void photoNotesLoad();     // at startup, after the card is mounted: the newest descriptions
void photoNotesService();  // UI task: writes new descriptions to the card

void photoNotesSaved(const std::string& photoName);  // a new photo was saved (before it's described)
void photoNotesAdd(const std::string& photoName, const std::string& description);
std::string photoNotesPromptBlock();  // for the AI ("" if none)
std::string photoNotesLatest();       // the newest photo's file name, e.g. "IMG_0007.jpg" ("" if none)
