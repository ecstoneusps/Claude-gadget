#pragma once
#include <string>
#include <vector>

// Things the buddy remembers ("remember that my dog's name is King"), kept as
// plain lines in memories.txt on the SD card and handed to the AI at the
// start of each conversation. Portable, so it is unit-tested on a PC.

struct MemoryCommand {
    enum Type { None, Remember, ForgetLast, ForgetAll, Forget } type = None;
    std::string fact;  // Remember: what to remember; Forget: what to forget ("my dog's name")
};

// Recognizes "remember that ...", "don't forget ...", "make a note that ...",
// "forget that", "forget everything", "forget that my dog's name is King".
// Questions ("do you remember ...?", "what do you remember?") are not commands:
// the AI answers those from the notes.
MemoryCommand parseMemoryCommand(const std::string& heard);

// "my dog's name is King" -> "your dog's name is King", for saying it back.
std::string toSecondPerson(const std::string& fact);

class MemoryStore {
public:
    // memories.txt: one note per line; blank lines and lines starting with #
    // (comments you add yourself) are skipped.
    void load(const std::string& text);
    std::string toText() const;

    // False if it's already remembered (same words, ignoring case and punctuation).
    bool add(const std::string& fact);
    bool removeLast(std::string* removed = nullptr);
    // Removes the note that best matches `what` (most shared words); false if none shares enough.
    bool remove(const std::string& what, std::string* removed = nullptr);
    void clear() { notes_.clear(); }

    const std::vector<std::string>& notes() const { return notes_; }

    // What the AI is told at the start of a conversation ("" if nothing),
    // the newest notes first that fit in `maxChars`.
    std::string promptBlock(size_t maxChars = 3000) const;

private:
    std::vector<std::string> notes_;
};
