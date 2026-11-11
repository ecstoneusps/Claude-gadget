// Native unit tests for the AI-conversation text and audio helpers. Run: tests/run_tests.sh
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdlib.h>

#include "AssistText.h"
#include "Memory.h"
#include "Wav.h"

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                 \
        }                                                               \
    } while (0)
#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const std::string a_ = (a), b_ = (b);                                           \
        if (a_ != b_) {                                                                 \
            printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_.c_str(), b_.c_str()); \
            ++failures;                                                                 \
        }                                                                               \
    } while (0)

static void test_split_leading_tag() {
    ReplyParts p = splitReply("[laughing] Ha! That's a good one.");
    CHECK_EQ(p.emotion, "laughing");
    CHECK_EQ(p.speech, "Ha! That's a good one.");
}

static void test_split_no_tag() {
    ReplyParts p = splitReply("Turned on the desk lamp.");
    CHECK_EQ(p.emotion, "");
    CHECK_EQ(p.speech, "Turned on the desk lamp.");
}

static void test_split_several_tags_first_wins() {
    ReplyParts p = splitReply("[Happy] Sure! [thinking] Hmm, let me see... [winking] Got it.");
    CHECK_EQ(p.emotion, "happy");
    CHECK_EQ(p.speech, "Sure! Hmm, let me see... Got it.");
}

static void test_split_keeps_numeric_brackets() {
    ReplyParts p = splitReply("[cool] Mars has two moons [1].");
    CHECK_EQ(p.emotion, "cool");
    CHECK_EQ(p.speech, "Mars has two moons [1].");
}

static void test_split_tag_with_colon() {
    ReplyParts p = splitReply("[surprised]: Whoa, really?");
    CHECK_EQ(p.emotion, "surprised");
    CHECK_EQ(p.speech, "Whoa, really?");
}

static void test_think_block_removed() {
    ReplyParts p = splitReply("<think>The user wants a joke.</think>\n[silly] Why did the robot cross the road?");
    CHECK_EQ(p.emotion, "silly");
    CHECK_EQ(p.speech, "Why did the robot cross the road?");
}

static void test_clean_markdown_and_emoji() {
    CHECK_EQ(cleanForSpeech("**Wow**, that's *really* cool! \xF0\x9F\x98\x8A"), "Wow, that's really cool!");
    CHECK_EQ(cleanForSpeech("*giggles* Okay, okay."), "Okay, okay.");
    CHECK_EQ(cleanForSpeech("Ha! *beeps happily* Hi."), "Ha! Hi.");
    CHECK_EQ(cleanForSpeech("# Title\nSome `code` here"), "Title Some code here");
    CHECK_EQ(cleanForSpeech("It\xE2\x80\x99s fine \xE2\x80\x94 really"), "It's fine , really");
    CHECK_EQ(cleanForSpeech("Caf\xC3\xA9 time"), "Caf\xC3\xA9 time");
    CHECK_EQ(cleanForSpeech("5 * 3 = 15"), "5 3 = 15");
}

static void test_normalize() {
    CHECK_EQ(normalizeSpeech(" Wink at me."), "wink at me");
    CHECK_EQ(normalizeSpeech("Don't  STOP, it!"), "dont stop it");
    CHECK_EQ(normalizeSpeech("..."), "");
}

static void test_phrase_match() {
    CHECK(speechMatchesPhrase(" Wink at me.", "wink at me"));
    CHECK(speechMatchesPhrase("Hey Buddy, look left please.", "look left"));
    CHECK(speechMatchesPhrase("Buddy, go to sleep!", "go to sleep"));
    CHECK(!speechMatchesPhrase("Can you look left and tell me a story?", "look left"));
    CHECK(!speechMatchesPhrase("", "look left"));
}

static std::vector<uint8_t> makeWav(int rate, int channels, const std::vector<int16_t>& samples,
                                    bool streamed = false, bool extraChunk = false) {
    std::vector<uint8_t> w;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back((uint8_t)(v >> (8 * i))); };
    auto put16 = [&](uint16_t v) { w.push_back((uint8_t)v); w.push_back((uint8_t)(v >> 8)); };
    auto tag = [&](const char* t) { w.insert(w.end(), t, t + 4); };
    const uint32_t dataBytes = (uint32_t)samples.size() * 2;
    tag("RIFF"); put32(streamed ? 0xFFFFFFFFu : 36 + dataBytes + (extraChunk ? 12 : 0)); tag("WAVE");
    tag("fmt "); put32(16); put16(1); put16((uint16_t)channels); put32(rate);
    put32(rate * channels * 2); put16((uint16_t)(channels * 2)); put16(16);
    if (extraChunk) { tag("LIST"); put32(3); w.push_back(1); w.push_back(2); w.push_back(3); w.push_back(0); }
    tag("data"); put32(streamed ? 0xFFFFFFFFu : dataBytes);
    for (int16_t s : samples) put16((uint16_t)s);
    return w;
}

static void test_wav_16k_mono_passthrough() {
    std::vector<int16_t> s = {0, 100, -100, 32767, -32768};
    auto w = makeWav(16000, 1, s);
    WavInfo info;
    CHECK(parseWav(w.data(), w.size(), info));
    CHECK(info.sampleRate == 16000 && info.channels == 1 && info.frames == 5);
    std::vector<int16_t> out(wavResampledLength(info, 16000));
    CHECK(out.size() == 5);
    CHECK(wavToMono(w.data(), info, 16000, out.data(), out.size()) == 5);
    CHECK(out == s);
}

static void test_wav_22k_resampled() {
    std::vector<int16_t> s(22050, 1000);  // one second of a constant level
    auto w = makeWav(22050, 1, s, /*streamed=*/true, /*extraChunk=*/true);
    WavInfo info;
    CHECK(parseWav(w.data(), w.size(), info));
    CHECK(info.frames == 22050);
    const size_t n = wavResampledLength(info, 16000);
    CHECK(n == 16000);
    std::vector<int16_t> out(n);
    CHECK(wavToMono(w.data(), info, 16000, out.data(), n) == n);
    CHECK(out[0] == 1000 && out[n / 2] == 1000 && out[n - 1] == 1000);
}

static void test_wav_stereo_mixed() {
    std::vector<int16_t> s = {1000, 3000, -2000, 0};  // two stereo frames
    auto w = makeWav(16000, 2, s);
    WavInfo info;
    CHECK(parseWav(w.data(), w.size(), info));
    CHECK(info.frames == 2);
    int16_t out[2];
    CHECK(wavToMono(w.data(), info, 16000, out, 2) == 2);
    CHECK(out[0] == 2000 && out[1] == -1000);
}

static void test_wav_rejects_junk() {
    WavInfo info;
    const uint8_t mp3[] = {0xFF, 0xFB, 0x90, 0x64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!parseWav(mp3, sizeof mp3, info));
    auto w = makeWav(16000, 1, {1, 2, 3});
    w[34] = 8;  // 8-bit samples: not supported
    CHECK(!parseWav(w.data(), w.size(), info));
    auto t = makeWav(16000, 1, {1, 2, 3});
    CHECK(!parseWav(t.data(), 20, info));  // truncated before the data chunk
}

static void test_speech_shaper() {
    // 1 kHz "speech" with a strong 60 Hz hum, then silence.
    const int rate = 16000;
    std::vector<int16_t> pcm(rate * 2, 0);
    for (int i = 0; i < rate; ++i)
        pcm[i] = (int16_t)(3000 * sin(2 * M_PI * 1000 * i / rate) + 8000 * sin(2 * M_PI * 60 * i / rate));
    std::vector<int16_t> whole = pcm;
    SpeechShaper(rate).process(whole.data(), whole.size());

    // Processing in odd-sized pieces, as it streams in, gives the same result.
    SpeechShaper pieces(rate);
    for (size_t at = 0; at < pcm.size();) {
        const size_t n = std::min<size_t>(777, pcm.size() - at);
        pieces.process(pcm.data() + at, n);
        at += n;
    }
    CHECK(pcm == whole);

    int peak = 0;
    double hum = 0, tone = 0;
    for (int i = rate / 4; i < rate; ++i) {
        peak = std::max(peak, abs((int)pcm[i]));
        hum += pcm[i] * sin(2 * M_PI * 60 * i / rate);
        tone += pcm[i] * sin(2 * M_PI * 1000 * i / rate);
    }
    CHECK(peak <= 32767 * 0.98 + 1);
    CHECK(fabs(tone) > 2 * fabs(hum));  // the hum was 2.7x louder than the tone before
    for (int i = rate + rate / 4; i < 2 * rate; ++i) CHECK(abs((int)pcm[i]) < 50);  // silence stays silent

    // +5 dB on a moderate tone (below the limiter's knee).
    std::vector<int16_t> mid(rate);
    for (int i = 0; i < rate; ++i) mid[i] = (int16_t)(6000 * sin(2 * M_PI * 1000 * i / rate));
    SpeechShaper(rate).process(mid.data(), mid.size());
    int midPeak = 0;
    for (int i = rate / 2; i < rate; ++i) midPeak = std::max(midPeak, abs((int)mid[i]));
    CHECK(midPeak > 10000 && midPeak < 11500);  // 6000 * 1.78 = 10670

    // A full-scale tone is limited, not clipped.
    std::vector<int16_t> loud(rate);
    for (int i = 0; i < rate; ++i) loud[i] = (int16_t)(32000 * sin(2 * M_PI * 1000 * i / rate));
    SpeechShaper(rate).process(loud.data(), loud.size());
    for (int i = 0; i < rate; ++i) CHECK(abs((int)loud[i]) <= 32767 * 0.98 + 1);
}

static void test_vision_requests() {
    CHECK(isVisionRequest("What do you see?"));
    CHECK(isVisionRequest(" Hey buddy, what am I holding in my hand?"));
    CHECK(isVisionRequest("What's this?"));
    CHECK(isVisionRequest("Can you read this for me?"));
    CHECK(isVisionRequest("Look at this!"));
    CHECK(isVisionRequest("How do I look today?"));
    CHECK(!isVisionRequest("What is the tallest mountain?"));
    CHECK(!isVisionRequest("Tell me a joke"));
    CHECK(!isVisionRequest("What is thistle?"));  // word boundaries
    CHECK(!isVisionRequest(""));
    CHECK(isPhotoRequest("Take a picture of me!"));
    CHECK(isPhotoRequest("Hey buddy, can you take a selfie?"));
    CHECK(!isPhotoRequest("What's a good camera to take pictures with?"));
    CHECK(!isVisionRequest("Take a picture of me!"));
}

static void test_memory_commands() {
    MemoryCommand c = parseMemoryCommand(" Remember that my dog's name is King.");
    CHECK(c.type == MemoryCommand::Remember);
    CHECK_EQ(c.fact, "my dog's name is King");
    c = parseMemoryCommand("Hey buddy, please remember my birthday is March 3rd!");
    CHECK(c.type == MemoryCommand::Remember);
    CHECK_EQ(c.fact, "my birthday is March 3rd");
    c = parseMemoryCommand("Don't forget that I like my coffee black.");
    CHECK(c.type == MemoryCommand::Remember);
    CHECK_EQ(c.fact, "I like my coffee black");
    c = parseMemoryCommand("Make a note that the car is due for an oil change.");
    CHECK(c.type == MemoryCommand::Remember);
    CHECK(parseMemoryCommand("Do you remember my dog's name?").type == MemoryCommand::None);
    CHECK(parseMemoryCommand("What do you remember about me?").type == MemoryCommand::None);
    CHECK(parseMemoryCommand("Remember when we went to the beach").type == MemoryCommand::None);
    CHECK(parseMemoryCommand("Remember to call mom").type == MemoryCommand::None);  // a reminder, not a fact
    CHECK(parseMemoryCommand("Tell me a joke").type == MemoryCommand::None);
    CHECK(parseMemoryCommand("Forget that.").type == MemoryCommand::ForgetLast);
    CHECK(parseMemoryCommand("Never mind that").type == MemoryCommand::ForgetLast);
    CHECK(parseMemoryCommand("Forget everything.").type == MemoryCommand::ForgetAll);
    c = parseMemoryCommand("Forget that my dog's name is King.");
    CHECK(c.type == MemoryCommand::Forget);
    CHECK_EQ(c.fact, "my dog's name is King");
}

static void test_second_person() {
    CHECK_EQ(toSecondPerson("my dog's name is King"), "your dog's name is King");
    CHECK_EQ(toSecondPerson("I like my coffee black"), "you like your coffee black");
    CHECK_EQ(toSecondPerson("I'm allergic to cats"), "you're allergic to cats");
    CHECK_EQ(toSecondPerson("I am a night owl"), "you are a night owl");
    CHECK_EQ(toSecondPerson("the car is mine"), "the car is yours");
    CHECK_EQ(toSecondPerson("Sam called me at 5 am"), "Sam called you at 5 am");
}

static void test_memory_store() {
    MemoryStore m;
    m.load("# comment\n\nmy dog's name is King\nI like my coffee black\n");
    CHECK(m.notes().size() == 2);
    CHECK(!m.add("My dog's name is King."));  // already known
    CHECK(m.add("my birthday is March 3rd"));
    CHECK(m.add("my locker code is 1234"));
    CHECK(m.add("my locker code is 5678"));  // numbers make it a different note
    CHECK(m.remove("locker code 1234"));
    CHECK(m.remove("locker code 5678"));
    std::string gone;
    CHECK(m.remove("my dog's name", &gone));
    CHECK_EQ(gone, "my dog's name is King");
    CHECK(!m.remove("the weather in Paris"));
    CHECK(m.removeLast(&gone));
    CHECK_EQ(gone, "my birthday is March 3rd");
    CHECK(m.notes().size() == 1);
    MemoryStore again;
    again.load(m.toText());
    CHECK(again.notes() == m.notes());
    const std::string block = m.promptBlock();
    CHECK(block.find("I like my coffee black") != std::string::npos);
    CHECK(MemoryStore().promptBlock().empty());
    // Only the newest notes that fit.
    MemoryStore many;
    for (int i = 0; i < 200; ++i) many.add("note number " + std::to_string(i) + " about something");
    const std::string b = many.promptBlock(300);
    CHECK(b.size() < 300 + 400);  // the notes that fit, plus the instructions around them
    CHECK(b.find("note number 199") != std::string::npos);
    CHECK(b.find("note number 0 ") == std::string::npos);
}

static void test_sentence_end() {
    const std::string npos_ = "";
    const size_t NP = std::string::npos;
    // Still being written: no complete sentence yet.
    CHECK(sentenceEnd("[happy] Hello there", 0) == NP);
    CHECK(sentenceEnd("[happy] Hello there!", 0) == NP);  // no space after it yet: more may follow
    const std::string a = "[happy] Hello there! How are you today? I'm";
    const size_t e1 = sentenceEnd(a, 0);
    CHECK(e1 != NP && a.substr(0, e1) == "[happy] Hello there! ");
    const size_t e2 = sentenceEnd(a, e1);
    CHECK(e2 != NP && a.substr(e1, e2 - e1) == "How are you today? ");
    CHECK(sentenceEnd(a, e2) == NP);
    // Short bits wait for more; numbers and abbreviations inside words don't end a sentence.
    CHECK(sentenceEnd("Hi. ", 0) == NP);
    CHECK(sentenceEnd("It costs 3.5 dollars and more", 0) == NP);
    const std::string b = "Version 2.0 is out now. Yes";
    const size_t e3 = sentenceEnd(b, 0);
    CHECK(e3 != NP && b.substr(0, e3) == "Version 2.0 is out now. ");
    // A line break ends one too.
    const std::string c = "Here is a list of things:\n- apples\n";
    CHECK(sentenceEnd(c, 0) == c.find('\n') + 1);
    // A long run without punctuation is cut at a comma.
    std::string d;
    for (int i = 0; i < 12; ++i) d += "and then some more words, ";
    const size_t e4 = sentenceEnd(d, 0);
    CHECK(e4 != NP && e4 <= 182 && d[e4 - 2] == ',');
    (void)npos_;
}

static void test_learned_facts() {
    const auto facts = parseLearnedFacts(
        "[happy] Here's what I learned:\n- My dog's name is King.\n2. I like hiking on weekends\n"
        "* \"My sister Anna lives in Ohio\"\nNONE\nDid you know that?\nOk\n\u2022 I'm a mail carrier\n");
    CHECK(facts.size() == 4);
    CHECK(facts.size() == 4 && facts[0] == "My dog's name is King" && facts[1] == "I like hiking on weekends" &&
          facts[2] == "My sister Anna lives in Ohio" && facts[3] == "I'm a mail carrier");
    CHECK(parseLearnedFacts("NONE").empty());
    CHECK(parseLearnedFacts("[neutral] NONE.").empty());
    const std::string p = learnPrompt({{"owner", "I got a new puppy named Max"}, {"buddy", "Aww, I love Max!"}},
                                      {"My dog's name is King"});
    CHECK(p.find("Owner: I got a new puppy named Max") != std::string::npos);
    CHECK(p.find("Buddy: Aww, I love Max!") != std::string::npos);
    CHECK(p.find("- My dog's name is King") != std::string::npos);
}

int main() {
    test_split_leading_tag();
    test_split_no_tag();
    test_split_several_tags_first_wins();
    test_split_keeps_numeric_brackets();
    test_split_tag_with_colon();
    test_think_block_removed();
    test_clean_markdown_and_emoji();
    test_normalize();
    test_phrase_match();
    test_wav_16k_mono_passthrough();
    test_wav_22k_resampled();
    test_wav_stereo_mixed();
    test_wav_rejects_junk();
    test_speech_shaper();
    test_vision_requests();
    test_memory_commands();
    test_second_person();
    test_memory_store();
    test_sentence_end();
    test_learned_facts();
    if (failures) {
        printf("assist tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("assist tests: all passed\n");
    return 0;
}
