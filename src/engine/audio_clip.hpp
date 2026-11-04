// audio_clip.hpp - sound clips and the files a mod can bring them in (no SDL, so it can be tested).
//
//   AudioClip  : interleaved stereo 16-bit at the mixer's rate, with an optional loop region
//   mixClip    : adds a clip into a 32-bit buffer (loops, with the region if it has one)
//   loadSoundFile : .wav (8/16/24/32-bit integer or 32-bit float PCM, any rate, mono or more) or .ogg (Vorbis) -> PcmData
//   makeClip   : PcmData -> AudioClip at the mixer's rate, with a gain and a pitch change (semitones, speed and pitch together like a tape)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("O2") // rendering a song or decoding a file is heavy: a build with no optimization takes seven times as long
#endif
// Ogg Vorbis (public domain, third_party/stb/stb_vorbis.c). It leaks a few short macro names, which are put back after it.
#pragma push_macro("CHECK")
#undef CHECK
#define STB_VORBIS_NO_PUSHDATA_API
#include "../../third_party/stb/stb_vorbis.c"
#undef L
#undef C
#undef R
#undef CHECK
#pragma pop_macro("CHECK")

namespace gp
{

    // One decoded sound: interleaved stereo samples at the mixer's rate. A looping clip repeats [loopStart, loopEnd) (in frames) once it has
    // played up to loopEnd: the part before loopStart is an intro. loopEnd 0 = the whole clip.
    struct AudioClip
    {
        std::vector<int16_t> pcm;
        size_t loopStart = 0, loopEnd = 0;
        size_t frames() const { return pcm.size() / 2; }
    };

    // The mixing arithmetic, apart from SDL so it can be tested: adds `frames` frames of the clip from `pos` into `out`
    // (interleaved stereo, 32-bit accumulators) at `gain` (0..1), wrapping when `loop`. Returns the new position, or
    // the clip's end if it ran out.
    inline size_t mixClip(const AudioClip &c, size_t pos, bool loop, float gain, int32_t *out, size_t frames)
    {
        const size_t n = c.frames();
        if (n == 0)
            return 0;
        const size_t end = c.loopEnd > 0 && c.loopEnd <= n ? c.loopEnd : n;
        const size_t start = c.loopStart < end ? c.loopStart : 0;
        for (size_t i = 0; i < frames; ++i)
        {
            if (loop && pos >= end)
                pos = start;
            else if (pos >= n)
                return n;
            out[i * 2] += int32_t(float(c.pcm[pos * 2]) * gain);
            out[i * 2 + 1] += int32_t(float(c.pcm[pos * 2 + 1]) * gain);
            ++pos;
        }
        return pos;
    }

    // Sound as it comes out of a file: interleaved 16-bit samples, any rate, any channel count.
    struct PcmData
    {
        std::vector<int16_t> pcm;
        int rate = 0, channels = 0;
        size_t frames() const { return channels > 0 ? pcm.size() / size_t(channels) : 0; }
    };

    inline uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
    inline uint32_t le16(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }

    // A RIFF WAVE file in memory. False with `why` set when it is not one this can read.
    inline bool parseWav(const std::vector<uint8_t> &f, PcmData &out, std::string &why)
    {
        if (f.size() < 12 || std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0)
            return why = "not a WAV file", false;
        int fmt = 0, ch = 0, bits = 0;
        uint32_t rate = 0;
        const uint8_t *data = nullptr;
        size_t dataLen = 0;
        for (size_t p = 12; p + 8 <= f.size();)
        {
            const uint32_t len = le32(&f[p + 4]);
            const size_t body = p + 8;
            const size_t avail = std::min<size_t>(len, f.size() - body);
            if (std::memcmp(&f[p], "fmt ", 4) == 0 && avail >= 16)
            {
                fmt = int(le16(&f[body]));
                ch = int(le16(&f[body + 2]));
                rate = le32(&f[body + 4]);
                bits = int(le16(&f[body + 14]));
                if (fmt == 0xfffe && avail >= 26) // WAVE_FORMAT_EXTENSIBLE: the real format is the first two bytes of the sub-format
                    fmt = int(le16(&f[body + 24]));
            }
            else if (std::memcmp(&f[p], "data", 4) == 0)
            {
                data = &f[body];
                dataLen = avail;
            }
            p = body + len + (len & 1);
        }
        if (!data || ch < 1 || ch > 16 || rate < 1000 || rate > 400000)
            return why = "a WAV file with no usable format or data", false;
        const int bytes = bits / 8;
        const bool pcmInt = fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32), flt = fmt == 3 && bits == 32;
        if (!pcmInt && !flt)
            return why = "only PCM (8, 16, 24, 32 bit) and 32-bit float WAV files are supported", false;
        const size_t count = dataLen / size_t(bytes) / size_t(ch) * size_t(ch);
        out.pcm.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            const uint8_t *s = data + i * size_t(bytes);
            int v = 0;
            if (flt)
            {
                float x;
                std::memcpy(&x, s, 4);
                v = int(std::clamp(x, -1.f, 1.f) * 32767.f);
            }
            else if (bits == 8)
                v = (int(s[0]) - 128) << 8;
            else if (bits == 16)
                v = int16_t(le16(s));
            else if (bits == 24)
                v = int16_t(le16(s + 1)); // the top two bytes
            else
                v = int16_t(le16(s + 2));
            out.pcm[i] = int16_t(v);
        }
        out.rate = int(rate);
        out.channels = ch;
        return true;
    }

    inline bool readFileBytes(const std::string &path, std::vector<uint8_t> &out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }

    inline std::string lowerExt(const std::string &path)
    {
        const size_t d = path.find_last_of('.');
        std::string e = d == std::string::npos ? "" : path.substr(d);
        for (char &c : e)
            c = char(std::tolower((unsigned char)c));
        return e;
    }

    // Reads an Ogg Vorbis file (the whole of it).
    inline bool decodeOgg(const std::vector<uint8_t> &f, PcmData &out, std::string &why)
    {
        int err = 0;
        stb_vorbis *v = stb_vorbis_open_memory(f.data(), int(f.size()), &err, nullptr);
        if (!v)
            return why = "not a readable Ogg Vorbis file (error " + std::to_string(err) + ")", false;
        const stb_vorbis_info info = stb_vorbis_get_info(v);
        const unsigned total = stb_vorbis_stream_length_in_samples(v);
        if (info.channels < 1 || info.channels > 16 || total == 0)
        {
            stb_vorbis_close(v);
            return why = "an Ogg file with no sound in it", false;
        }
        out.channels = info.channels;
        out.rate = int(info.sample_rate);
        out.pcm.assign(size_t(total) * size_t(info.channels), 0);
        size_t got = 0;
        while (got < total)
        {
            const int n = stb_vorbis_get_samples_short_interleaved(v, info.channels, out.pcm.data() + got * size_t(info.channels),
                                                                   int((total - got) * size_t(info.channels)));
            if (n <= 0)
                break;
            got += size_t(n);
        }
        stb_vorbis_close(v);
        out.pcm.resize(got * size_t(info.channels));
        return got > 0 ? true : (why = "an Ogg file with no sound in it", false);
    }

    // Header only (rate and channels), to check a file when the mod is loaded without decoding all of it.
    inline bool checkSoundFile(const std::string &path, std::string &why)
    {
        std::vector<uint8_t> f;
        if (!readFileBytes(path, f))
            return why = "cannot read " + path, false;
        const std::string e = lowerExt(path);
        if (e == ".wav")
        {
            PcmData p;
            return parseWav(f, p, why);
        }
        if (e == ".ogg")
        {
            int err = 0;
            stb_vorbis *v = stb_vorbis_open_memory(f.data(), int(f.size()), &err, nullptr);
            if (!v)
                return why = "not a readable Ogg Vorbis file (error " + std::to_string(err) + ")", false;
            stb_vorbis_close(v);
            return true;
        }
        return why = "a sound file must be .wav or .ogg", false;
    }

    inline bool loadSoundFile(const std::string &path, PcmData &out, std::string &why)
    {
        std::vector<uint8_t> f;
        if (!readFileBytes(path, f))
            return why = "cannot read " + path, false;
        const std::string e = lowerExt(path);
        if (e == ".wav")
            return parseWav(f, out, why);
        if (e == ".ogg")
            return decodeOgg(f, out, why);
        return why = "a sound file must be .wav or .ogg", false;
    }

    // A 16-bit PCM WAV file of interleaved samples.
    inline bool writeWav16(const std::string &path, const std::vector<int16_t> &pcm, int rate, int channels = 2)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f)
            return false;
        auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put(char(v >> (8 * i))); };
        auto p16 = [&](uint32_t v) { f.put(char(v)); f.put(char(v >> 8)); };
        const uint32_t bytes = uint32_t(pcm.size() * 2);
        f.write("RIFF", 4);
        p32(36 + bytes);
        f.write("WAVEfmt ", 8);
        p32(16);
        p16(1);
        p16(uint32_t(channels));
        p32(uint32_t(rate));
        p32(uint32_t(rate * channels * 2));
        p16(uint32_t(channels * 2));
        p16(16);
        f.write("data", 4);
        p32(bytes);
        f.write(reinterpret_cast<const char *>(pcm.data()), std::streamsize(bytes));
        return bool(f);
    }

    // The clip at the mixer's rate, stereo, times `gain`, played `semitones` higher (and so shorter) or lower. More than two channels: the
    // first two. The loop points (seconds of the file, 0 = none / the end) are carried over.
    inline AudioClip makeClip(const PcmData &in, int outRate, float gain = 1.f, double semitones = 0, double loopStartSec = 0, double loopEndSec = 0)
    {
        AudioClip c;
        const size_t n = in.frames();
        if (n == 0 || in.rate <= 0 || outRate <= 0)
            return c;
        const double step = double(in.rate) / double(outRate) * std::pow(2.0, semitones / 12.0); // input frames per output frame
        const size_t outN = size_t(double(n) / step);
        c.pcm.resize(outN * 2);
        const size_t ch = size_t(in.channels);
        auto at = [&](size_t frame, size_t channel) -> float
        { return float(in.pcm[std::min(frame, n - 1) * ch + std::min(channel, ch - 1)]); };
        for (size_t i = 0; i < outN; ++i)
        {
            const double pos = double(i) * step;
            const size_t a = size_t(pos);
            const float t = float(pos - double(a));
            for (size_t k = 0; k < 2; ++k)
            {
                const float v = (at(a, k) * (1.f - t) + at(a + 1, k) * t) * gain;
                c.pcm[i * 2 + k] = int16_t(std::clamp(v, -32768.f, 32767.f));
            }
        }
        const double perFrame = 1.0 / (double(in.rate) * std::pow(2.0, semitones / 12.0)); // seconds of the file -> output frames
        (void)perFrame;
        const double toFrames = double(outRate) / std::pow(2.0, semitones / 12.0);
        if (loopEndSec > 0)
        {
            c.loopEnd = std::min(outN, size_t(loopEndSec * toFrames));
            c.loopStart = loopStartSec > 0 ? std::min(c.loopEnd, size_t(loopStartSec * toFrames)) : 0;
        }
        else if (loopStartSec > 0)
            c.loopStart = std::min(outN, size_t(loopStartSec * toFrames));
        return c;
    }

} // namespace gp
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
