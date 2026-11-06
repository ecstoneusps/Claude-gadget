#pragma once
#include <string>

// Text handling for conversations with the AI (through Home Assistant).
// Portable, so it is unit-tested on a PC.

// The AI is asked to start each reply with an emotion tag, e.g.
// "[laughing] Ha! Good one." splitReply() takes the first [word] tag as the
// emotion and removes every such tag from what gets spoken. Tags are letters
// only (plus spaces, - and _), so "[1]" style references are left alone.
struct ReplyParts {
    std::string emotion;  // lowercase, "" if the reply had no tag
    std::string speech;   // the rest, cleaned up for the speech synthesizer
};
ReplyParts splitReply(const std::string& reply);

// Removes what a speech synthesizer would read out badly: <think> blocks,
// markdown (*bold*, # headings, `code`), stage directions like *giggles*,
// emoji and other symbols. Collapses whitespace.
std::string cleanForSpeech(const std::string& text);

// Lowercase letters and single spaces ("Wink at me!" -> "wink at me"), with
// apostrophes dropped ("don't" -> "dont"). For comparing recognized speech
// with command phrases.
std::string normalizeSpeech(const std::string& text);

// Whether recognized speech is exactly a command phrase, ignoring
// punctuation, case, and a "hey buddy" / "buddy" / "please" around it.
bool speechMatchesPhrase(const std::string& heard, const std::string& phrase);
