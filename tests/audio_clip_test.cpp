// audio_clip_test.cpp [file.ogg] - the clips a mod's sound files become (engine/audio_clip.hpp): WAV reading, pitch, gain and loop points.
// With an .ogg path as the argument it also decodes that file (no Ogg file is kept in the repository).
#include "../src/engine/audio/audio_clip.hpp"
#include <filesystem>
#include <iostream>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                   \
    do                                                             \
    {                                                              \
        if (!(c))                                                  \
        {                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                              \
        }                                                          \
    } while (0)

static void put32(std::vector<uint8_t> &v, uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back(uint8_t(x >> (8 * i)));
}
static void put16(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}
// A WAV file: format 1 (PCM) or 3 (float), `bits` per sample, `ch` channels, the given raw sample bytes.
static std::vector<uint8_t> wav(int fmt, int ch, int rate, int bits, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> v = {'R', 'I', 'F', 'F'};
    put32(v, uint32_t(36 + data.size()));
    for (char c : std::string("WAVEfmt "))
        v.push_back(uint8_t(c));
    put32(v, 16);
    put16(v, uint32_t(fmt));
    put16(v, uint32_t(ch));
    put32(v, uint32_t(rate));
    put32(v, uint32_t(rate * ch * bits / 8));
    put16(v, uint32_t(ch * bits / 8));
    put16(v, uint32_t(bits));
    for (char c : std::string("data"))
        v.push_back(uint8_t(c));
    put32(v, uint32_t(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

int main(int argc, char **argv)
{
    std::string why;
    { // 16-bit mono
        PcmData p;
        CHECK(parseWav(wav(1, 1, 22050, 16, {0x00, 0x10, 0x00, 0xf0}), p, why) && p.rate == 22050 && p.channels == 1 && p.pcm.size() == 2 && p.pcm[0] == 0x1000 && p.pcm[1] == -0x1000);
    }
    { // 8-bit: unsigned, centred on 128
        PcmData p;
        CHECK(parseWav(wav(1, 2, 8000, 8, {128, 255, 0, 128}), p, why) && p.channels == 2 && p.pcm.size() == 4 && p.pcm[0] == 0 && p.pcm[1] == 127 << 8 && p.pcm[2] == -128 * 256);
    }
    { // 24-bit: the top two bytes
        PcmData p;
        CHECK(parseWav(wav(1, 1, 44100, 24, {0x00, 0x34, 0x12}), p, why) && p.pcm.size() == 1 && p.pcm[0] == 0x1234);
    }
    { // 32-bit float
        PcmData p;
        float f[2] = {0.5f, -1.f};
        std::vector<uint8_t> d(8);
        std::memcpy(d.data(), f, 8);
        CHECK(parseWav(wav(3, 1, 44100, 32, d), p, why) && p.pcm.size() == 2 && std::abs(p.pcm[0] - 16383) <= 1 && p.pcm[1] == -32767);
    }
    { // not a WAV, an unsupported one, one with no data
        PcmData p;
        CHECK(!parseWav({1, 2, 3}, p, why) && why.find("not a WAV") != std::string::npos);
        CHECK(!parseWav(wav(2, 1, 44100, 4, {0, 0}), p, why) && why.find("only PCM") != std::string::npos);
        std::vector<uint8_t> nodata = wav(1, 1, 44100, 16, {});
        nodata.resize(nodata.size() - 8); // cut the data chunk off
        CHECK(!parseWav(nodata, p, why));
    }
    { // the clip: stereo at the mixer's rate
        PcmData p;
        p.rate = 22050;
        p.channels = 1;
        p.pcm = {0, 1000, 2000, 3000};
        const AudioClip c = makeClip(p, 44100);
        CHECK(c.frames() == 8 && c.pcm[0] == 0 && c.pcm[1] == 0 && c.pcm[2] == 500 && c.pcm[4] == 1000 && c.pcm[6] == 1500); // doubled up, blended between
        const AudioClip g = makeClip(p, 22050, 0.5f);
        CHECK(g.frames() == 4 && g.pcm[2] == 500 && g.pcm[3] == 500 && g.pcm[6] == 1500); // half volume, both channels
        const AudioClip up = makeClip(p, 22050, 1.f, 12); // an octave up: twice as fast, half as long
        CHECK(up.frames() == 2 && up.pcm[2] == 2000);
        const AudioClip down = makeClip(p, 22050, 1.f, -12);
        CHECK(down.frames() == 8);
        PcmData st;
        st.rate = 44100;
        st.channels = 3; // more than two: the first two
        st.pcm = {10, 20, 30, 40, 50, 60};
        const AudioClip s = makeClip(st, 44100);
        CHECK(s.frames() == 2 && s.pcm[0] == 10 && s.pcm[1] == 20 && s.pcm[2] == 40 && s.pcm[3] == 50);
        CHECK(makeClip(PcmData{}, 44100).frames() == 0);
    }
    { // loop points, in seconds of the file
        PcmData p;
        p.rate = 100;
        p.channels = 1;
        p.pcm.assign(100, 1000); // one second
        AudioClip c = makeClip(p, 100, 1.f, 0, 0.25, 0.75);
        CHECK(c.loopStart == 25 && c.loopEnd == 75);
        c = makeClip(p, 100, 1.f, 12, 0.5, 1.0); // an octave up: the points move with the sound
        CHECK(c.loopEnd == std::min<size_t>(c.frames(), 50) && c.loopStart == 25);
        CHECK(makeClip(p, 100).loopEnd == 0);
    }
    { // mixing with a loop region: an intro, then round and round the middle
        AudioClip c;
        for (int i = 0; i < 6; ++i)
        {
            c.pcm.push_back(int16_t(i + 1));
            c.pcm.push_back(int16_t(i + 1));
        }
        c.loopStart = 2;
        c.loopEnd = 5; // frames 3, 4, 5 (values) repeat
        int32_t out[20] = {};
        const size_t pos = mixClip(c, 0, true, 1.f, out, 10);
        const int want[10] = {1, 2, 3, 4, 5, 3, 4, 5, 3, 4};
        for (int i = 0; i < 10; ++i)
            CHECK(out[i * 2] == want[i]);
        CHECK(pos == 4);
        int32_t one[12] = {};
        CHECK(mixClip(c, 0, false, 1.f, one, 6) == 6 && one[10] == 6); // not looping: the whole clip, the region is ignored
        AudioClip whole = c;
        whole.loopStart = whole.loopEnd = 0;
        int32_t w[16] = {};
        mixClip(whole, 0, true, 1.f, w, 8);
        CHECK(w[12] == 1 && w[14] == 2); // the whole clip repeats
    }
    { // files: a wav on disk, a bad extension, a garbage .ogg
        const std::string dir = (std::filesystem::temp_directory_path() / "gp_audioclip_").string();
        {
            std::ofstream f(dir + "a.wav", std::ios::binary);
            const auto w = wav(1, 1, 8000, 16, {0, 1, 0, 2});
            f.write(reinterpret_cast<const char *>(w.data()), std::streamsize(w.size()));
            std::ofstream g(dir + "b.ogg", std::ios::binary);
            g << "this is not an ogg file at all";
            std::ofstream h(dir + "c.mp3", std::ios::binary);
            h << "x";
        }
        PcmData p;
        CHECK(loadSoundFile(dir + "a.wav", p, why) && p.rate == 8000 && checkSoundFile(dir + "a.wav", why));
        CHECK(!loadSoundFile(dir + "b.ogg", p, why) && why.find("Ogg") != std::string::npos);
        CHECK(!checkSoundFile(dir + "b.ogg", why));
        CHECK(!checkSoundFile(dir + "c.mp3", why) && why.find(".wav or .ogg") != std::string::npos);
        CHECK(!checkSoundFile(dir + "missing.wav", why) && why.find("cannot read") != std::string::npos);
        for (const char *n : {"a.wav", "b.ogg", "c.mp3"})
            std::filesystem::remove(dir + n);
    }
    if (argc > 1) // a real Ogg file
    {
        PcmData p;
        const bool ok = loadSoundFile(argv[1], p, why);
        if (!ok)
            std::cerr << why << "\n";
        CHECK(ok && p.rate > 0 && p.channels >= 1 && p.frames() > 100 && checkSoundFile(argv[1], why));
        if (ok)
        {
            const AudioClip c = makeClip(p, 44100);
            int peak = 0;
            for (int16_t s : c.pcm)
                peak = std::max(peak, std::abs(int(s)));
            std::cout << argv[1] << ": " << p.rate << " Hz, " << p.channels << " ch, " << p.frames() << " frames, peak " << peak << "\n";
            CHECK(peak > 100);
        }
    }

    if (g_fail)
        return 1;
    std::cout << "audio_clip_test: all passed\n";
    return 0;
}
