#include "Memory.h"

#include <ctype.h>
#include <string.h>

#include <algorithm>
#include <set>
#include <sstream>


namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Drops leading "hey buddy," / "buddy," / "okay," / "please" and trailing
// punctuation, keeping the rest as said (capitals and apostrophes intact).
std::string stripFillers(std::string s) {
    s = trim(s);
    static const char* const kLead[] = {"hey buddy", "hi buddy", "okay buddy", "ok buddy", "buddy", "okay", "ok",
                                        "so", "and", "please", "hey"};
    for (bool again = true; again;) {
        again = false;
        const std::string l = lower(s);
        for (const char* w : kLead) {
            const size_t n = strlen(w);
            if (l.compare(0, n, w) == 0 && (l.size() == n || !isalpha((unsigned char)l[n]))) {
                s = s.substr(n);
                while (!s.empty() && (s[0] == ',' || s[0] == ' ' || s[0] == '.')) s.erase(0, 1);
                again = true;
                break;
            }
        }
    }
    while (!s.empty() && (s.back() == '.' || s.back() == '!' || s.back() == ',' || s.back() == ' ')) s.pop_back();
    return s;
}

// Lowercase letters and digits, single spaces: for comparing notes. (Unlike
// normalizeSpeech, numbers count: "my code is 1234" isn't "my code is 5678".)
std::string key(const std::string& s) {
    std::string out;
    for (char ch : s) {
        const unsigned char c = (unsigned char)ch;
        if (isalnum(c)) out += (char)tolower(c);
        else if (c == '\'') continue;
        else if (!out.empty() && out.back() != ' ') out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool startsWith(const std::string& lowerText, const char* prefix, size_t* rest) {
    const size_t n = strlen(prefix);
    if (lowerText.compare(0, n, prefix) != 0) return false;
    *rest = n;
    return true;
}

std::set<std::string> words(const std::string& s) {
    std::set<std::string> out;
    std::istringstream in(key(s));
    std::string w;
    static const std::set<std::string> kSkip = {"my", "the", "a", "an", "is", "are", "i", "that", "of", "to", "and"};
    while (in >> w)
        if (!kSkip.count(w)) out.insert(w);
    return out;
}

}  // namespace

MemoryCommand parseMemoryCommand(const std::string& heard) {
    MemoryCommand cmd;
    const std::string s = stripFillers(heard);
    const std::string l = lower(s);
    if (l.empty() || trim(heard).back() == '?') return cmd;  // questions go to the AI

    size_t rest = 0;
    // Forgetting.
    if (l == "forget everything" || l == "forget everything you know" || l == "forget everything you remember" ||
        l == "forget all of it" || l == "erase your memory" || l == "clear your memory") {
        cmd.type = MemoryCommand::ForgetAll;
        return cmd;
    }
    if (l == "forget that" || l == "forget it" || l == "never mind that" || l == "forget what i just said" ||
        l == "forget the last thing" || l == "forget what i just told you") {
        cmd.type = MemoryCommand::ForgetLast;
        return cmd;
    }
    for (const char* p : {"forget that ", "forget about ", "forget ", "please forget "}) {
        if (startsWith(l, p, &rest)) {
            cmd.type = MemoryCommand::Forget;
            cmd.fact = trim(s.substr(rest));
            return cmd;
        }
    }
    // Remembering.
    static const char* const kRemember[] = {"remember that ", "don't forget that ", "dont forget that ", "do not forget that ",
                                            "make a note that ", "take a note that ", "note that ", "remember this ",
                                            "remember ", "don't forget ", "dont forget ", "make a note ", "take a note "};
    for (const char* p : kRemember) {
        if (startsWith(l, p, &rest)) {
            std::string fact = trim(s.substr(rest));
            while (!fact.empty() && (fact[0] == ',' || fact[0] == ':')) fact = trim(fact.substr(1));
            // "remember when we...", "remember to ..." (a reminder, not a fact) aren't notes.
            const std::string lf = lower(fact);
            if (fact.size() < 3 || lf.rfind("when ", 0) == 0 || lf.rfind("to ", 0) == 0) return cmd;
            cmd.type = MemoryCommand::Remember;
            cmd.fact = fact;
            return cmd;
        }
    }
    return cmd;
}

std::string toSecondPerson(const std::string& fact) {
    static const std::pair<const char*, const char*> kSwap[] = {
        {"i'm", "you're"}, {"i am", "you are"}, {"i've", "you've"}, {"i'll", "you'll"}, {"i'd", "you'd"},
        {"myself", "yourself"}, {"mine", "yours"}, {"my", "your"}, {"me", "you"}, {"i", "you"},
        {"i was", "you were"},
    };
    std::string out;
    size_t i = 0;
    while (i < fact.size()) {
        if (!isalpha((unsigned char)fact[i]) && fact[i] != '\'') {
            out += fact[i++];
            continue;
        }
        size_t j = i;
        while (j < fact.size() && (isalpha((unsigned char)fact[j]) || fact[j] == '\'')) ++j;
        // Try two-word phrases first ("i am", "i was").
        bool done = false;
        if (j < fact.size() && fact[j] == ' ') {
            size_t k = j + 1;
            while (k < fact.size() && (isalpha((unsigned char)fact[k]) || fact[k] == '\'')) ++k;
            const std::string two = lower(fact.substr(i, k - i));
            for (const auto& [from, to] : kSwap) {
                if (two == from) {
                    out += to;
                    i = k;
                    done = true;
                    break;
                }
            }
        }
        if (done) continue;
        const std::string word = fact.substr(i, j - i);
        const std::string lw = lower(word);
        std::string repl = word;
        for (const auto& [from, to] : kSwap) {
            if (lw == from && !strchr(from, ' ')) {
                repl = to;
                break;
            }
        }
        // "am" only right after "i" was swapped (handled as "i am"); leave other "am"s alone.
        if (lw == "am") repl = word;
        out += repl;
        i = j;
    }
    return out;
}

void MemoryStore::load(const std::string& text) {
    notes_.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        notes_.push_back(line);
    }
}

std::string MemoryStore::toText() const {
    std::string t =
        "# Things Buddy remembers, one per line. Edit or delete lines freely;\n"
        "# lines starting with # are ignored.\n";
    for (const auto& n : notes_) t += n + "\n";
    return t;
}

bool MemoryStore::add(const std::string& fact) {
    const std::string f = trim(fact);
    if (f.empty()) return false;
    const std::string k = key(f);
    for (const auto& n : notes_)
        if (key(n) == k) return false;
    notes_.push_back(f);
    return true;
}

bool MemoryStore::removeLast(std::string* removed) {
    if (notes_.empty()) return false;
    if (removed) *removed = notes_.back();
    notes_.pop_back();
    return true;
}

bool MemoryStore::remove(const std::string& what, std::string* removed) {
    const auto want = words(what);
    if (want.empty()) return false;
    int best = -1;
    size_t bestShared = 0;
    for (size_t i = 0; i < notes_.size(); ++i) {
        const auto have = words(notes_[i]);
        size_t shared = 0;
        for (const auto& w : want) shared += have.count(w);
        // Newer notes win ties (">="): you probably mean the latest one.
        if (shared > 0 && shared >= bestShared) {
            bestShared = shared;
            best = (int)i;
        }
    }
    // At least half of the words you said must match.
    if (best < 0 || bestShared * 2 < want.size()) return false;
    if (removed) *removed = notes_[best];
    notes_.erase(notes_.begin() + best);
    return true;
}

std::string MemoryStore::promptBlock(size_t maxChars) const {
    if (notes_.empty()) return "";
    std::vector<std::string> picked;
    size_t used = 0;
    for (auto it = notes_.rbegin(); it != notes_.rend(); ++it) {
        if (used + it->size() + 4 > maxChars) break;
        picked.push_back(*it);
        used += it->size() + 4;
    }
    std::reverse(picked.begin(), picked.end());
    std::string block = "[Things your owner asked you to remember (\"my\" means your owner's): ";
    for (size_t i = 0; i < picked.size(); ++i) block += (i ? "; " : "") + picked[i];
    block += ". Use them when they help; don't recite them unprompted.]\n";
    return block;
}

std::string learnPrompt(const std::vector<std::pair<std::string, std::string>>& transcript,
                        const std::vector<std::string>& notes) {
    std::string p =
        "[Note-taking task, not a chat. Reply only with the list, no emotion tag, no other words.]\n"
        "Below is a conversation between you (Buddy) and your owner. Write down any lasting facts about your "
        "owner, or the people and pets in their life, that would help in future conversations: names, "
        "birthdays, likes and dislikes, hobbies, plans with dates, important events. One fact per line, "
        "written the way your owner would say it, starting with \"My\" or \"I\" (for example: My dog's name is "
        "King). Leave out anything temporary (the weather, the time, what they're doing right now), anything "
        "only you said, and anything already in your notes. If nothing is worth keeping, reply with just: NONE\n";
    if (!notes.empty()) {
        p += "\nAlready in your notes:\n";
        for (const auto& n : notes) p += "- " + n + "\n";
    }
    p += "\nConversation:\n";
    for (const auto& [who, said] : transcript) p += (who == "owner" ? "Owner: " : "Buddy: ") + said + "\n";
    return p;
}

std::vector<std::string> parseLearnedFacts(const std::string& reply) {
    std::vector<std::string> out;
    std::istringstream in(reply);
    std::string line;
    while (std::getline(in, line)) {
        std::string f = trim(line);
        // Emotion tags and list markers: "[happy]", "- ", "* ", "• ", "1. ", "2) ".
        while (!f.empty() && f[0] == '[') {
            const size_t close = f.find(']');
            if (close == std::string::npos) break;
            f = trim(f.substr(close + 1));
        }
        if (f.rfind("\xE2\x80\xA2", 0) == 0) f = trim(f.substr(3));  // bullet
        while (!f.empty() && (f[0] == '-' || f[0] == '*')) f = trim(f.substr(1));
        size_t d = 0;
        while (d < f.size() && isdigit((unsigned char)f[d])) ++d;
        if (d > 0 && d < f.size() && (f[d] == '.' || f[d] == ')')) f = trim(f.substr(d + 1));
        if (f.size() >= 2 && (f[0] == '"' || f[0] == '\'') && f.back() == f[0]) f = trim(f.substr(1, f.size() - 2));
        while (!f.empty() && (f.back() == ' ' || f.back() == '.')) f.pop_back();
        if (f.size() < 8 || f.size() > 160) continue;
        if (f.back() == '?' || f.back() == ':') continue;
        // Facts about the owner only: they start with "my" or "I" (I'm, I've, I have, I like...).
        const std::string l = lower(f);
        const bool mine = l.rfind("my ", 0) == 0 || l.rfind("i ", 0) == 0 || l.rfind("i'm ", 0) == 0 ||
                          l.rfind("i've ", 0) == 0 || l.rfind("i'll ", 0) == 0 || l.rfind("i'd ", 0) == 0;
        if (!mine || l.find("none") == 0) continue;
        out.push_back(f);
    }
    return out;
}
