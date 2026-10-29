// rom_audio.hpp - the game's own sounds, decoded from the ROM (no SDL; pure so it can be tested).
//
// Port of the sample finder, song parser, instrument picker and envelope from GigaEditr's audio browser / music player.
//   samples: "SPF2ALP" / "wav2drm" headers (40 bytes) + 8-bit unsigned mono PCM
//   sfx    : table at word 0xf3aa, music: table at word 0xf46a, 2 words per entry (lo, hi) added to the bank base
//            held in ROM words 0xfa5f (lo) / 0xfa60 (hi); the result is a word address
// Everything is rendered offline into stereo float buffers at the mixer's rate; the Audio class turns them into clips.
#pragma once
#include "../loaders/rom.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace gp
{
    namespace rau
    {

        struct Sample
        {
            bool spf = false; // SPF2ALP (else wav2drm)
            uint32_t off = 0; // header byte offset
            uint32_t rate = 11025;
            uint32_t len = 0, pl = 0, ie = 0, ls = 0;
            uint32_t env = 0;
            int root = 0, zoneTop = 0;
            bool hasLoop = false;
        };

        // Linear amplitude 0..1 with the envelope breakpoints: [seconds, level 0..127].
        struct Env
        {
            std::vector<std::pair<double, int>> pts;
            double stopT = -1;
            bool has = false;
        };

        struct Event
        {
            int t = 0;
            char k = 0; // n note, p program, b bend, f flag
            int ch = 0, dur = 0, pb = 0, pan = 0, vol = 0, vel = 0, v = 0, gap = 1;
        };

        struct Song
        {
            std::vector<Event> ev;
            int end = 0;
        };

        // RemapZoneIndexToAudioId (0x03c811): the song an area plays on entry, or -1 to leave the current one running.
        // The front end (menus, file select) plays song 4.
        constexpr int kMenuSong = 4;
        constexpr int zoneSong(int zone)
        {
            switch (zone)
            {
            case 0: return 4;
            case 1: return 6;
            case 2: case 4: return 7;
            case 3: return 5;
            case 5: return 3;
            case 6: return 1;
            case 7: return 2;
            case 8: return 8;
            case 9: return 0;
            default: return -1;
            }
        }

        constexpr double kEnvTick = 32 / 70312.5, kEnvRamp = 832 / 70312.5;
        constexpr double kTicksPerSecond = 200; // fitted in the editor, not from the ROM

        class RomAudio
        {
        public:
            explicit RomAudio(const Rom &rom) : r_(rom.bytes())
            {
                bl_ = w(0xfa5f);
                bh_ = w(0xfa60);
                find();
                loadSongs();
                loadInstruments();
            }

            // Sound effect id (0..0x5f) -> stereo float buffer at `rate`, or empty.
            std::vector<float> sfx(int id, int rate) const
            {
                std::vector<float> out;
                if (id < 0 || id >= 0x60)
                    return out;
                const Sample *s = byOff(resolve(0xf3aa, id));
                if (!s)
                    return out;
                const uint32_t n = s->spf ? s->pl : s->len;
                const double step = double(std::max<uint32_t>(s->rate, 8000)) / rate;
                const size_t frames = size_t(n / step);
                out.resize(frames * 2);
                for (size_t i = 0; i < frames; ++i)
                {
                    const float v = interp(s->off + 40, n, i * step) * 0.5f;
                    out[i * 2] = out[i * 2 + 1] = v;
                }
                return out;
            }

            size_t songCount() const { return songs_.size(); }

            // Song n rendered to stereo float at `rate`; the tail is folded back onto the start so it loops cleanly.
            std::vector<float> music(size_t n, int rate) const
            {
                std::vector<float> out;
                if (n >= songs_.size())
                    return out;
                const Song &S = songs_[n];
                const size_t body = size_t(S.end / kTicksPerSecond * rate);
                if (!body)
                    return out;
                const size_t total = body + size_t(4.0 * rate);
                std::vector<float> mix(total * 2, 0.f);
                std::array<int, 16> prog{};
                std::array<std::vector<std::pair<double, int>>, 16> bends; // time, phase value
                for (const Event &q : S.ev)
                    if (q.k == 'b' && q.ch < 16)
                        bends[q.ch].push_back({q.t / kTicksPerSecond, q.v});
                for (const Event &q : S.ev)
                {
                    const double when = q.t / kTicksPerSecond;
                    if (q.k == 'p')
                    {
                        if (q.ch < 16)
                            prog[q.ch] = q.v;
                        continue;
                    }
                    if (q.k != 'n')
                        continue;
                    const float gain = float((q.vol * q.vel) >> 7) / 127.f;
                    if (q.ch == 9)
                    {
                        if (q.pb < int(drums_.size()))
                            voice(mix, rate, *drums_[q.pb], when, 1.0, gain, q.pan, 0, nullptr);
                    }
                    else if (const Sample *e = pick(prog[q.ch & 15], q.pb))
                        voice(mix, rate, *e, when, std::pow(2.0, (q.pb - e->root) / 12.0), gain, q.pan, q.dur / kTicksPerSecond,
                              &bends[q.ch & 15]);
                }
                // fold the tail over the start and normalise only if it would clip
                out.assign(body * 2, 0.f);
                for (size_t i = 0; i < total * 2; ++i)
                    out[i % (body * 2)] += mix[i];
                float pk = 0;
                for (float f : out)
                    pk = std::max(pk, std::fabs(f));
                if (pk > 0.98f)
                    for (float &f : out)
                        f *= 0.98f / pk;
                return out;
            }

            const Sample *drum(size_t i) const { return i < drums_.size() ? drums_[i] : nullptr; }
            const std::vector<Song> &songs() const { return songs_; }
            size_t sampleCount() const { return all_.size(); }

            // First entry of the program's group with pb <= its zone top (0x04b41e).
            const Sample *pick(int prog, int pb) const
            {
                if (prog < 0 || prog >= int(offs_.size()))
                    return nullptr;
                const size_t s = offs_[prog], en = prog + 1 < int(offs_.size()) ? offs_[prog + 1] : list_.size();
                size_t i = s;
                while (i < en && i < list_.size() && pb > list_[i]->zoneTop)
                    ++i;
                if (list_.empty())
                    return nullptr;
                return list_[std::min(i, list_.size() - 1)];
            }

        private:
            const std::vector<uint8_t> &r_;
            uint32_t bl_ = 0, bh_ = 0;
            std::vector<Sample> all_;
            std::map<uint32_t, size_t> byOff_;
            std::vector<Song> songs_;
            std::vector<uint32_t> offs_;
            std::vector<const Sample *> list_, drums_;
            mutable std::map<uint32_t, Env> envs_;

            uint32_t b(uint32_t o) const { return o < r_.size() ? r_[o] : 0; }
            uint32_t w(uint32_t word) const { return b(word * 2) | (b(word * 2 + 1) << 8); }
            uint32_t u32(uint32_t o) const { return b(o) | (b(o + 1) << 8) | (b(o + 2) << 16) | (b(o + 3) << 24); }

            // Table entry -> byte offset.
            uint32_t resolve(uint32_t table, int i) const
            {
                const uint32_t l = bl_ + w(table + i * 2);
                const uint32_t h = (bh_ + w(table + i * 2 + 1) + (l >> 16)) & 0xffff;
                return (h * 65536 + (l & 0xffff)) * 2;
            }

            const Sample *byOff(uint32_t off) const
            {
                auto it = byOff_.find(off);
                return it == byOff_.end() ? nullptr : &all_[it->second];
            }

            void find()
            {
                static const char kS[] = "SPF2ALP";
                for (uint32_t o = 0; o + 40 < r_.size(); ++o)
                {
                    bool spf = true, wav = true;
                    for (int k = 0; k < 7; ++k)
                    {
                        spf = spf && r_[o + k] == uint8_t(kS[k]);
                        wav = wav && (k == 3 ? r_[o + k] == '2' : (r_[o + k] | 32) == "wav2drm"[k]);
                    }
                    if (!spf && !wav)
                        continue;
                    Sample s;
                    s.spf = spf;
                    s.off = o;
                    s.rate = u32(o + 16);
                    s.len = u32(o + 20);
                    if (!s.len || s.len > 0x800000)
                        continue;
                    const bool trunc = o + 40 + s.len > r_.size();
                    if (trunc)
                        s.len = uint32_t(r_.size() - o - 40);
                    const uint32_t f2 = spf ? u32(o + 24) : 0;
                    s.env = spf ? u32(o + 28) : 0;
                    const int64_t ls = 2 * int64_t(f2) - 40;
                    s.pl = s.len;
                    s.ie = s.len;
                    if (spf)
                    {
                        if (f2 > 0 && ls >= 0 && ls < int64_t(s.len))
                        {
                            s.ls = uint32_t(ls);
                            uint32_t pl = s.ls;
                            while (pl < s.len && b(o + 40 + pl) != 0xff)
                                ++pl;
                            uint32_t ie = s.ls;
                            while (ie > 0 && b(o + 39 + ie) == 0xff)
                                --ie;
                            s.pl = pl;
                            s.ie = ie;
                        }
                        else
                        {
                            uint32_t pl = 0;
                            while (pl < s.len && b(o + 40 + pl) != 0xff)
                                ++pl;
                            s.pl = s.ie = pl;
                        }
                    }
                    s.root = int(b(o + 33));
                    s.zoneTop = int(b(o + 34));
                    s.hasLoop = f2 > 0 && ls >= 0 && ls < int64_t(s.pl) && !trunc;
                    byOff_[o] = all_.size();
                    all_.push_back(s);
                    o += 39;
                }
            }

            void loadSongs()
            {
                for (int i = 0; i < 64; ++i)
                {
                    const uint32_t o = resolve(0xf46a, i);
                    if (o < 0x100000 || o + 8 > r_.size())
                        break;
                    Song S;
                    if (!parse(o, S))
                        break;
                    songs_.push_back(std::move(S));
                }
            }

            bool parse(uint32_t p, Song &S) const
            {
                auto u = [&](uint32_t o) { return b(o) | (b(o + 1) << 8); };
                int t = 0;
                for (int n = 0; n < 30000 && p + 2 <= r_.size(); ++n)
                {
                    const int wd = int(u(p));
                    p += 2;
                    Event e;
                    e.t = t;
                    if (wd & 0x8000)
                    {
                        const int w1 = int(u(p)), w2 = int(u(p + 2));
                        p += 4;
                        e.k = 'n';
                        e.ch = wd & 15;
                        e.dur = (wd & 0x7ff0) >> 4;
                        e.pb = w1 & 127;
                        e.pan = (w1 >> 8) & 127;
                        e.vol = (w2 >> 8) & 127;
                        e.vel = w2 & 127;
                        S.ev.push_back(e);
                        continue;
                    }
                    switch (wd & 0xf000)
                    {
                    case 0:
                        e.k = 'p';
                        e.ch = (wd >> 8) & 127;
                        e.v = wd & 127;
                        S.ev.push_back(e);
                        break;
                    case 0x1000:
                        e.k = 'b';
                        e.ch = (wd >> 4) & 15;
                        e.v = int(u(p));
                        p += 2;
                        S.ev.push_back(e);
                        break;
                    case 0x2000:
                        if (wd & 0x800)
                        {
                            e.k = 'f';
                            e.v = wd ^ 0x2800;
                            S.ev.push_back(e);
                        }
                        else
                        {
                            S.end = t;
                            return true;
                        }
                        break;
                    case 0x4000:
                        t += wd & 0x3fff;
                        break;
                    default:
                        S.end = t;
                        return false;
                    }
                }
                S.end = t;
                return false;
            }

            void loadInstruments()
            {
                for (int i = 0; i < 15; ++i)
                    offs_.push_back(w(0x3f22de + i));
                for (uint32_t p = 0x3f230f; list_.size() < 200; p += 2)
                {
                    const Sample *e = byOff((w(p) * 65536u + w(p + 1)) * 2);
                    if (!e || !e->spf)
                        break;
                    list_.push_back(e);
                }
                for (size_t i = offs_.back(); i < list_.size(); ++i)
                    drums_.push_back(list_[i]);
            }

            // Sample value at a fractional position (0..1 scale, data starts at byte `base`).
            float interp(uint32_t base, uint32_t n, double pos) const
            {
                const uint32_t i = uint32_t(pos);
                if (i >= n)
                    return 0.f;
                const float a = (float(b(base + i)) - 128.f) / 128.f;
                const float c = i + 1 < n ? (float(b(base + i + 1)) - 128.f) / 128.f : a;
                return a + (c - a) * float(pos - i);
            }

            const Env &envelope(const Sample &e) const
            {
                auto it = envs_.find(e.off);
                if (it != envs_.end())
                    return it->second;
                Env out;
                out.has = e.spf && e.env > 0;
                if (out.has)
                {
                    const uint32_t p = e.off + 2 * e.env;
                    auto rd = [&](int k, int &a, int &c) {
                        const uint32_t q = p + 4 * k;
                        if (q + 4 <= r_.size())
                        {
                            a = b(q) | (b(q + 1) << 8);
                            c = b(q + 2) | (b(q + 3) << 8);
                        }
                        else
                            a = c = 0;
                    };
                    struct St
                    {
                        int inc, sign, tgt, load, rep;
                    };
                    auto dec = [](int a, int c) { return St{a & 127, (a >> 7) & 1, (a >> 8) & 127, c & 255, (c >> 8) & 1}; };
                    int a, c;
                    rd(0, a, c);
                    St st = dec(a, c);
                    int edd = 0, cnt = st.load, ptr = 0, fetched = 0;
                    for (int tk = 1; tk <= 66000 && out.pts.size() < 1500; ++tk)
                    {
                        if (cnt > 0)
                            --cnt;
                        if (cnt != 0)
                            continue;
                        if (edd != st.tgt)
                        {
                            const int old = edd;
                            if (st.sign)
                            {
                                edd = std::max(st.tgt, edd - st.inc);
                                if (edd <= 0)
                                {
                                    out.pts.push_back({tk * kEnvTick, 0});
                                    out.stopT = tk * kEnvTick;
                                    break;
                                }
                            }
                            else
                                edd = std::min(st.tgt, edd + st.inc);
                            if (edd != old)
                                out.pts.push_back({tk * kEnvTick, edd});
                        }
                        if (edd == st.tgt && !st.rep)
                        {
                            rd(ptr++, a, c);
                            if (a == 0x5053 || a == 0x6177 || ++fetched > 256)
                                break; // ran into the next header: hold
                            st = dec(a, c);
                        }
                        cnt = st.load;
                    }
                }
                return envs_[e.off] = out;
            }

            // Mixes one note into `mix` (stereo float). dur <= 0: let the sample/envelope end it.
            void voice(std::vector<float> &mix, int rate, const Sample &e, double when, double pitch, float gain, int pan, double dur,
                       const std::vector<std::pair<double, int>> *bends) const
            {
                const uint32_t base = e.off + 40;
                const double hdr = double(std::max<uint32_t>(e.rate ? e.rate : 11025, 8000));
                const float gl = pan < 64 ? 1.f : std::min(1.f, float(127 - pan) * 2.f / 128.f);
                const float gr = pan < 64 ? std::min(1.f, float(pan) * 2.f / 128.f) : 1.f;
                const Env &env = envelope(e);

                // envelope timeline: (time since start, level 0..127); level 128 = no envelope (constant)
                std::vector<std::pair<double, int>> tl;
                double tEnd = 1e9;
                if (env.has)
                {
                    tl.push_back({0.0, 0});
                    tl.insert(tl.end(), env.pts.begin(), env.pts.end());
                    if (env.stopT >= 0)
                        tEnd = env.stopT;
                    if (dur > 0 && dur < tEnd)
                    {
                        int L = 0;
                        for (auto &pt : env.pts)
                            if (pt.first <= dur)
                                L = pt.second;
                            else
                                break;
                        while (!tl.empty() && tl.back().first > dur)
                            tl.pop_back();
                        double tt = dur;
                        while (L > 0)
                        {
                            tl.push_back({tt, L});
                            L -= 32;
                            tt += kEnvRamp;
                        }
                        tl.push_back({tt, 0});
                        tEnd = tt;
                    }
                }
                else
                {
                    tl.push_back({0.0, 128});
                    if (dur > 0)
                        tEnd = dur + 0.01;
                }

                // sample layout: intro then loop body
                const uint32_t introLen = e.hasLoop ? e.ie : e.pl, bodyLen = e.hasLoop ? e.pl - e.ls : 0;
                auto sampleAt = [&](double pos) -> float {
                    if (e.hasLoop)
                    {
                        if (pos >= introLen && bodyLen)
                            pos = introLen + std::fmod(pos - introLen, double(bodyLen));
                        if (pos < introLen)
                            return interp(base, introLen, pos);
                        return interp(base + e.ls, bodyLen, pos - introLen);
                    }
                    return interp(base, introLen, pos);
                };

                size_t i0 = size_t(std::max(0.0, when) * rate);
                double step = pitch * hdr / rate, pos = 0;
                size_t bi = 0;
                const size_t maxF = mix.size() / 2;
                size_t ti = 0;
                for (size_t f = i0; f < maxF; ++f)
                {
                    const double t = double(f - i0) / rate;
                    if (t >= tEnd)
                        break;
                    if (bends)
                        while (bi < bends->size() && (*bends)[bi].first <= when + t)
                        {
                            if ((*bends)[bi].first >= when - 0.001)
                                step = (*bends)[bi].second * 281250.0 / 524288.0 / rate;
                            ++bi;
                        }
                    if (!e.hasLoop && pos >= introLen)
                        break;
                    while (ti + 1 < tl.size() && tl[ti + 1].first <= t)
                        ++ti;
                    const float lvl = tl[ti].second == 128 ? 1.f : float(tl[ti].second) / 128.f;
                    const float v = sampleAt(pos) * gain * lvl;
                    mix[f * 2] += v * gl;
                    mix[f * 2 + 1] += v * gr;
                    pos += step;
                }
            }
        };

    } // namespace rau
} // namespace gp
