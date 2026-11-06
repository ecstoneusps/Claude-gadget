#include "AssistText.h"

#include <ctype.h>
#include <string.h>

namespace {

bool isTagChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ' ' || c == '-' || c == '_';
}

std::string collapseSpaces(const std::string& s) {
    std::string out;
    for (char c : s) {
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (space) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Removes <think>...</think> (some models reason out loud first).
std::string stripThink(const std::string& s) {
    std::string out = s;
    for (;;) {
        const size_t a = out.find("<think>");
        if (a == std::string::npos) break;
        const size_t b = out.find("</think>", a);
        out.erase(a, b == std::string::npos ? std::string::npos : b + 8 - a);
    }
    return out;
}

bool startsWithWord(const std::string& s, const std::string& w) {
    return s.compare(0, w.size(), w) == 0 && (s.size() == w.size() || s[w.size()] == ' ');
}

bool endsWithWord(const std::string& s, const std::string& w) {
    return s.size() >= w.size() && s.compare(s.size() - w.size(), w.size(), w) == 0 &&
           (s.size() == w.size() || s[s.size() - w.size() - 1] == ' ');
}

}  // namespace

std::string cleanForSpeech(const std::string& text) {
    std::string s = stripThink(text);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '*') {
            // **bold**: drop the stars, keep the words.
            if (i + 1 < s.size() && s[i + 1] == '*') {
                ++i;
                continue;
            }
            // *giggles* at the start of a sentence is a stage direction: drop it.
            // Mid-sentence it's emphasis (*really*): keep the word. A lone *: drop.
            size_t last = out.find_last_not_of(' ');
            const bool sentenceStart = last == std::string::npos || out[last] == '.' || out[last] == '!' ||
                                       out[last] == '?' || out[last] == '\n';
            size_t j = s.find('*', i + 1);
            if (sentenceStart && j != std::string::npos && j > i + 1 && j - i < 40 &&
                s.find('\n', i) > j && s[i + 1] != ' ') {
                const std::string inner = s.substr(i + 1, j - i - 1);
                bool action = true;  // only words, no sentence punctuation
                for (char ch : inner) {
                    if (!(isalpha((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '\'')) action = false;
                }
                if (action) {
                    i = j;
                    out += ' ';
                    continue;
                }
            }
            continue;
        }
        if (c == '#' || c == '`' || c == '_' || c == '~' || c == '|' || c == '>' || c == '<') {
            out += ' ';
            continue;
        }
        if (c >= 0x80) {
            // Keep accented letters (2-byte sequences); drop emoji and symbols (3-4 bytes).
            int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
            if (len == 2 && i + 1 < s.size()) {
                out += s.substr(i, 2);
            } else if (len == 3 && i + 2 < s.size()) {
                // Typographic punctuation the synthesizer understands as plain text.
                const std::string seq = s.substr(i, 3);
                if (seq == "\xE2\x80\x99" || seq == "\xE2\x80\x98") out += '\'';
                else if (seq == "\xE2\x80\x9C" || seq == "\xE2\x80\x9D") out += '"';
                else if (seq == "\xE2\x80\x94" || seq == "\xE2\x80\x93") out += ", ";
                else if (seq == "\xE2\x80\xA6") out += "...";
                else out += ' ';
            } else {
                out += ' ';
            }
            i += len - 1;
            continue;
        }
        out += (char)c;
    }
    // Leftover leading punctuation from removed tags, e.g. ": hello".
    std::string r = collapseSpaces(out);
    size_t k = 0;
    while (k < r.size() && (r[k] == ':' || r[k] == ',' || r[k] == ' ' || r[k] == '-')) ++k;
    return r.substr(k);
}

ReplyParts splitReply(const std::string& reply) {
    ReplyParts parts;
    std::string rest;
    const std::string s = stripThink(reply);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '[') {
            const size_t j = s.find(']', i + 1);
            if (j != std::string::npos && j > i + 1 && j - i <= 24) {
                bool tag = true;
                for (size_t k = i + 1; k < j; ++k) tag = tag && isTagChar(s[k]);
                if (tag) {
                    if (parts.emotion.empty()) {
                        for (size_t k = i + 1; k < j; ++k) {
                            const char c = s[k];
                            if (c != ' ') parts.emotion += (char)tolower((unsigned char)c);
                        }
                    }
                    rest += ' ';
                    i = j;
                    continue;
                }
            }
        }
        rest += s[i];
    }
    parts.speech = cleanForSpeech(rest);
    return parts;
}

std::string normalizeSpeech(const std::string& text) {
    std::string out;
    for (char ch : text) {
        const unsigned char c = (unsigned char)ch;
        if (isalpha(c)) {
            out += (char)tolower(c);
        } else if (c == '\'') {
            continue;  // don't -> dont
        } else if (!out.empty() && out.back() != ' ') {
            out += ' ';
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool speechMatchesPhrase(const std::string& heard, const std::string& phrase) {
    std::string h = normalizeSpeech(heard);
    const std::string p = normalizeSpeech(phrase);
    if (p.empty()) return false;
    static const char* const kLead[] = {"hey buddy", "hi buddy", "okay buddy", "ok buddy", "buddy", "hey", "please"};
    static const char* const kTrail[] = {"please", "buddy", "now"};
    for (bool changed = true; changed;) {
        changed = false;
        if (h == p) return true;
        for (const char* w : kLead) {
            if (startsWithWord(h, w) && h.size() > strlen(w)) {
                h = h.substr(strlen(w) + 1);
                changed = true;
                break;
            }
        }
        if (changed) continue;
        for (const char* w : kTrail) {
            if (endsWithWord(h, w) && h.size() > strlen(w)) {
                h = h.substr(0, h.size() - strlen(w) - 1);
                changed = true;
                break;
            }
        }
    }
    return h == p;
}
