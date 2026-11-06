// Native unit tests for the AI-conversation text and audio helpers. Run: tests/run_tests.sh
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "AssistText.h"
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
    if (failures) {
        printf("assist tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("assist tests: all passed\n");
    return 0;
}
