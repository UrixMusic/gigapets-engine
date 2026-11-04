// midi.hpp - a Standard MIDI File (.mid) as a song the ROM's player can render (no SDL, no ROM: it only builds the song).
//
// What is read: format 0 and 1 files with a ticks-per-quarter-note division; tempo changes; note on / off per channel; program change;
// control changes 7 (volume), 11 (expression), 10 (pan), 64 (sustain pedal) and 6 / 38 with RPN 0 (pitch bend range); pitch bend.
// Everything else (other controllers, aftertouch, lyrics, sysex, SMPTE time) is skipped.
//
// How it maps onto the ROM's song events (rom_audio.hpp):
//   channel      the same 0..15; channel 10 (index 9) is the drum channel, as in the ROM's songs
//   note         ch, the note number, velocity, pan (CC10, 64 = centre), volume (CC7 x CC11, default 127), length in the player's 200 ticks a second
//   General MIDI   program changes and drum notes are read as General MIDI by default (gm_map.hpp: each of the ROM's 14 instruments and 12 drums is the
//                closest GM sound to it, and any other GM program takes the nearest instrument of its family), so a song written for any GM player plays
//                sensibly. `gmPrograms` / `gmDrums` off read them as the ROM's own numbers instead (programs 0..13, drum notes 0..11).
//   program      the ROM has 14 instruments (0..13; its group 14 is the drum kit): the MIDI program number is taken as it is, or as the `programs` map says (program -> ROM program).
//                Many editors (FL Studio) write no program changes at all, so a channel's instrument can also come from the track's name, written like
//                "Lead [3]" (the number in square brackets; "[i3]" works too), or from the `channels` map (MIDI channel -> instrument). A program change
//                in the file wins over both, from where it is. A channel with none of them plays instrument 0.
//   drums        the ROM has 12 drum sounds (0..11): the MIDI drum note is taken as the ROM's drum number, or as the `drums` map says
//   pitch bend   a bend event in cents above the note, so a slide keeps its note's own pitch
//   slides       FL Studio writes a slide as a second note laid over the main one (the slide note: it starts where the glide starts and ends where it ends,
//                and pitched at the note it slides to). That looks like an overlapping note, so it only counts on the channels the song names in
//                `slideChannels`: there a note that lies inside another note on the same channel is a slide of that note, which glides in a straight
//                line (in semitones) from where it was to the slide note's pitch during the slide note's time, then stays there until the note ends.
//                Two notes with exactly the same start and end: the one listed first is the slide.
// Sustain holds notes released while the pedal is down until it comes up. A song ends with the last track's end (or its last note), unless a marker or
// text event reading "loop end" says where the song loops: notes still sounding then ring on into the start of the next round, as in the ROM's songs.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "gm_map.hpp"
#include "rom_audio.hpp"

namespace gp
{
    namespace midi
    {

        struct Options
        {
            std::map<int, int> programs; // MIDI program -> the ROM's instrument 0..13
            std::map<int, int> drums;    // MIDI drum note -> the ROM's drum 0..11
            int programCount = 14, drumCount = 12; // instruments 0..13 (the ROM's group 14 is the drums)
            std::map<int, int> channels;    // MIDI channel 0..15 -> the ROM's instrument 0..13 it starts with
            bool gmPrograms = true, gmDrums = true; // read program changes / drum notes as General MIDI (else as the ROM's own numbers)
            std::vector<int> slideChannels; // MIDI channels 0..15 where a note inside another note is a slide
        };

        struct Result
        {
            rau::Song song;
            std::vector<std::string> warnings;
            int notes = 0;
            double seconds = 0;
        };

        namespace detail
        {
            struct Raw
            {
                uint64_t tick = 0;
                int order = 0; // at the same tick: note offs first, then the rest in the file's order
                size_t seq = 0;
                int kind = 0;  // 0x80 off, 0x90 on, 0xb0 controller, 0xc0 program, 0xe0 bend
                int ch = 0, a = 0, b = 0;
                int track = 0;
            };
            struct Tempo
            {
                uint64_t tick;
                uint32_t usPerQuarter;
            };
            inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
            inline uint32_t be16(const uint8_t *p) { return uint32_t(p[0]) << 8 | p[1]; }
        } // namespace detail

        // False with `why` set when the file cannot be used. Fills `out` otherwise (warnings are things that were left out).
        inline bool toSong(const std::vector<uint8_t> &f, const Options &opt, Result &out, std::string &why)
        {
            using namespace detail;
            out = Result{};
            if (f.size() < 14 || std::memcmp(f.data(), "MThd", 4) != 0)
                return why = "not a MIDI file", false;
            const uint32_t hlen = be32(&f[4]);
            if (hlen < 6 || 8 + size_t(hlen) > f.size())
                return why = "a damaged MIDI header", false;
            const int format = int(be16(&f[8])), ntrk = int(be16(&f[10])), division = int(be16(&f[12]));
            if (format > 1)
                return why = "MIDI format 2 (separate songs) is not supported; save it as format 0 or 1", false;
            if (division & 0x8000)
                return why = "MIDI files timed in SMPTE frames are not supported; use ticks per quarter note", false;
            if (division <= 0)
                return why = "a MIDI file with no time division", false;

            std::vector<Raw> raw;
            std::vector<Tempo> tempos;
            uint64_t endTick = 0;
            int64_t loopEndTick = -1; // a marker "loop end"
            size_t pos = 8 + size_t(hlen), seq = 0;
            int tracks = 0;
            std::map<int, int> trackProgram; // track -> the instrument its name asks for
            while (pos + 8 <= f.size() && tracks < ntrk)
            {
                const uint32_t len = be32(&f[pos + 4]);
                const bool isTrack = std::memcmp(&f[pos], "MTrk", 4) == 0;
                const size_t body = pos + 8, end = std::min<size_t>(f.size(), body + len);
                pos = body + len;
                if (!isTrack)
                    continue;
                ++tracks;
                uint64_t tick = 0;
                int running = 0;
                size_t p = body;
                auto varlen = [&](uint32_t &v)
                {
                    v = 0;
                    for (int i = 0; i < 4 && p < end; ++i)
                    {
                        const uint8_t c = f[p++];
                        v = v << 7 | (c & 0x7f);
                        if (!(c & 0x80))
                            return true;
                    }
                    return false;
                };
                while (p < end)
                {
                    uint32_t delta = 0;
                    if (!varlen(delta))
                        return why = "a damaged MIDI track", false;
                    tick += delta;
                    if (p >= end)
                        break;
                    int status = f[p];
                    if (status & 0x80)
                        ++p;
                    else if (running)
                        status = running;
                    else
                        return why = "a damaged MIDI track (data with no status)", false;
                    if (status == 0xff) // meta event
                    {
                        if (p >= end)
                            break;
                        const int type = f[p++];
                        uint32_t mlen = 0;
                        if (!varlen(mlen) || p + mlen > end)
                            return why = "a damaged MIDI track (meta event)", false;
                        if (type == 0x01 || type == 0x06) // text or a marker: "loop end" says where the song loops
                        {
                            std::string low(reinterpret_cast<const char *>(&f[p]), mlen);
                            for (char &ch : low)
                                ch = char(std::tolower((unsigned char)ch));
                            if (low.rfind("loop end", 0) == 0 || low.rfind("rom end", 0) == 0)
                                loopEndTick = int64_t(tick);
                        }
                        if (type == 0x03) // the track's name: "Lead [3]" asks for instrument 3
                        {
                            const std::string nm(reinterpret_cast<const char *>(&f[p]), mlen);
                            const size_t lb = nm.find('['), rb = lb == std::string::npos ? lb : nm.find(']', lb);
                            bool numbered = false;
                            if (rb != std::string::npos)
                            {
                                size_t q = lb + 1;
                                if (q < rb && (nm[q] == 'i' || nm[q] == 'I'))
                                    ++q;
                                if (q < rb && nm.find_first_not_of("0123456789", q) >= rb)
                                {
                                    trackProgram[tracks - 1] = std::atoi(nm.substr(q, rb - q).c_str());
                                    numbered = true;
                                }
                            }
                            if (!numbered)
                                if (const int byWord = gm::instrumentFromName(nm); byWord >= 0)
                                    trackProgram[tracks - 1] = byWord;
                        }
                        if (type == 0x51 && mlen == 3)
                            tempos.push_back({tick, uint32_t(f[p]) << 16 | uint32_t(f[p + 1]) << 8 | f[p + 2]});
                        p += mlen;
                        if (type == 0x2f)
                            break;
                        continue;
                    }
                    if (status == 0xf0 || status == 0xf7) // system exclusive: skipped
                    {
                        uint32_t slen = 0;
                        if (!varlen(slen) || p + slen > end)
                            return why = "a damaged MIDI track (sysex)", false;
                        p += slen;
                        continue;
                    }
                    running = status;
                    const int kind = status & 0xf0, ch = status & 15;
                    const int need = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
                    if (p + size_t(need) > end)
                        return why = "a damaged MIDI track (cut short)", false;
                    Raw r;
                    r.tick = tick;
                    r.seq = seq++;
                    r.ch = ch;
                    r.a = f[p] & 0x7f;
                    r.b = need == 2 ? f[p + 1] & 0x7f : 0;
                    p += size_t(need);
                    r.kind = kind;
                    r.track = tracks - 1;
                    if (kind == 0x90 && r.b == 0)
                        r.kind = 0x80; // a note on with no velocity is a note off
                    if (kind == 0xd0 || kind == 0xa0)
                        continue; // aftertouch: skipped
                    r.order = r.kind == 0x80 ? 0 : 1; // note offs first; the rest in the file's order (a controller then a note, then another controller and note, at one tick)
                    raw.push_back(r);
                }
                endTick = std::max(endTick, tick);
            }
            if (tracks == 0)
                return why = "a MIDI file with no tracks", false;

            std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo &a, const Tempo &b) { return a.tick < b.tick; });
            std::stable_sort(raw.begin(), raw.end(), [](const Raw &a, const Raw &b)
                             { return a.tick != b.tick ? a.tick < b.tick : a.order != b.order ? a.order < b.order : a.seq < b.seq; });
            // ticks -> seconds through the tempo map
            auto toSec = [&](uint64_t tick)
            {
                double sec = 0;
                uint64_t last = 0;
                uint32_t us = 500000;
                for (const Tempo &t : tempos)
                {
                    if (t.tick >= tick)
                        break;
                    sec += double(t.tick - last) * double(us) / 1e6 / division;
                    last = t.tick;
                    us = t.usPerQuarter;
                }
                return sec + double(tick - last) * double(us) / 1e6 / division;
            };
            auto toTicks = [](double sec) { return int(std::lround(sec * rau::kTicksPerSecond)); };

            struct Chan
            {
                int vol = 127, expr = 127, pan = 64, program = 0;
                double bendCents = 0, range = 2.0;
                int rpnMsb = 127, rpnLsb = 127;
                bool sustain = false;
            };
            Chan ch[16];
            struct Active
            {
                size_t ev;
                double on;
            };
            std::map<std::pair<int, int>, std::vector<Active>> active; // (channel, note) -> the notes still sounding, oldest first
            std::vector<std::pair<size_t, int>> pending[16];           // released under the pedal: event, note
            std::vector<double> pendingOn[16];
            std::vector<rau::Event> &ev = out.song.ev;
            ev.clear();
            { // where a channel starts: the `channels` map, else its track's name; played from the beginning, and a program change in the file takes over
                std::map<int, int> initial = opt.channels;
                std::map<int, bool> warnedName;
                for (const Raw &r : raw)
                {
                    const auto tp = trackProgram.find(r.track);
                    if (r.ch == 9 || tp == trackProgram.end() || initial.count(r.ch))
                        continue;
                    if (tp->second < 0 || tp->second >= opt.programCount)
                    {
                        if (!warnedName[tp->second])
                            out.warnings.push_back("a track is named for instrument " + std::to_string(tp->second) + ", but the ROM's are 0 to " + std::to_string(opt.programCount - 1) + ": ignored");
                        warnedName[tp->second] = true;
                        continue;
                    }
                    initial[r.ch] = tp->second;
                }
                for (const auto &kv : initial)
                    if (kv.first != 9 && kv.second >= 0 && kv.second < opt.programCount)
                    {
                        rau::Event e;
                        e.k = 'p';
                        e.t = 0;
                        e.ch = kv.first;
                        e.v = kv.second;
                        ev.push_back(e);
                    }
            }
            std::map<int, bool> warnedDrum, warnedProgram;
            double lastEnd = 0;
            auto closeNote = [&](size_t index, double on, double off)
            {
                ev[index].dur = std::max(1, toTicks(off - on));
                lastEnd = std::max(lastEnd, off);
            };
            for (const Raw &r : raw)
            {
                const double t = toSec(r.tick);
                Chan &c = ch[r.ch];
                switch (r.kind)
                {
                case 0xc0:
                {
                    if (r.ch == 9)
                        break;
                    int prog = r.a;
                    const auto m = opt.programs.find(r.a);
                    if (m != opt.programs.end())
                        prog = m->second;
                    else if (opt.gmPrograms)
                    {
                        bool exact = true;
                        prog = gm::programToRom(r.a, &exact);
                        if (!exact && !warnedProgram[r.a])
                            out.warnings.push_back("General MIDI program " + std::to_string(r.a) + " has no instrument of its own: played as instrument " + std::to_string(prog) + " (" +
                                                   gm::romInstrument(prog).name + "), the nearest in its family; 'programs' can say otherwise");
                        warnedProgram[r.a] = true;
                    }
                    else if (prog >= opt.programCount)
                    {
                        if (!warnedProgram[r.a])
                            out.warnings.push_back("program " + std::to_string(r.a) + " is not one of the ROM's " + std::to_string(opt.programCount) + " instruments: played as instrument 0 (add it to 'programs')");
                        warnedProgram[r.a] = true;
                        prog = 0;
                    }
                    rau::Event e;
                    e.k = 'p';
                    e.t = toTicks(t);
                    e.ch = r.ch;
                    e.v = std::clamp(prog, 0, std::max(0, opt.programCount - 1));
                    ev.push_back(e);
                    break;
                }
                case 0xb0:
                    switch (r.a)
                    {
                    case 7: c.vol = r.b; break;
                    case 11: c.expr = r.b; break;
                    case 10: c.pan = r.b; break;
                    case 101: c.rpnMsb = r.b; break;
                    case 100: c.rpnLsb = r.b; break;
                    case 6:
                        if (c.rpnMsb == 0 && c.rpnLsb == 0)
                            c.range = r.b + (c.range - std::floor(c.range));
                        break;
                    case 38:
                        if (c.rpnMsb == 0 && c.rpnLsb == 0)
                            c.range = std::floor(c.range) + r.b / 100.0;
                        break;
                    case 64:
                    {
                        const bool down = r.b >= 64;
                        if (c.sustain && !down) // the pedal comes up: what was released under it ends now
                        {
                            for (size_t i = 0; i < pending[r.ch].size(); ++i)
                                closeNote(pending[r.ch][i].first, pendingOn[r.ch][i], t);
                            pending[r.ch].clear();
                            pendingOn[r.ch].clear();
                        }
                        c.sustain = down;
                        break;
                    }
                    }
                    break;
                case 0xe0:
                {
                    const int value = (r.b << 7 | r.a) - 8192;
                    const double cents = double(value) / 8192.0 * c.range * 100.0;
                    if (std::fabs(cents - c.bendCents) > 0.5)
                    {
                        c.bendCents = cents;
                        rau::Event e;
                        e.k = 'B';
                        e.t = toTicks(t);
                        e.ch = r.ch;
                        e.v = int(std::lround(cents));
                        ev.push_back(e);
                    }
                    break;
                }
                case 0x90:
                {
                    int pb = r.a;
                    if (r.ch == 9)
                    {
                        const auto m = opt.drums.find(r.a);
                        pb = m != opt.drums.end() ? m->second : r.a < opt.drumCount ? r.a : opt.gmDrums ? gm::drumToRom(r.a) : r.a;
                        if (pb < 0 || pb >= opt.drumCount)
                        {
                            if (!warnedDrum[r.a])
                                out.warnings.push_back("drum note " + std::to_string(r.a) + " is not one of the ROM's " + std::to_string(opt.drumCount) + " drums (0 to " +
                                                       std::to_string(opt.drumCount - 1) + "): left out (add it to 'drums')");
                            warnedDrum[r.a] = true;
                            break;
                        }
                    }
                    if (r.ch != 9 && std::fabs(c.bendCents) > 0.5) // a bend held from before: the new note starts bent
                    {
                        rau::Event b;
                        b.k = 'B';
                        b.t = toTicks(t);
                        b.ch = r.ch;
                        b.v = int(std::lround(c.bendCents));
                        ev.push_back(b);
                    }
                    rau::Event e;
                    e.k = 'n';
                    e.t = toTicks(t);
                    e.ch = r.ch;
                    e.pb = pb;
                    e.vel = r.b;
                    e.pan = c.pan;
                    e.vol = std::clamp(c.vol * c.expr / 127, 0, 127);
                    e.dur = 1;
                    ev.push_back(e);
                    active[{r.ch, r.a}].push_back({ev.size() - 1, t});
                    ++out.notes;
                    break;
                }
                case 0x80:
                {
                    auto it = active.find({r.ch, r.a});
                    if (it == active.end() || it->second.empty())
                        break;
                    const Active a = it->second.front();
                    it->second.erase(it->second.begin());
                    if (c.sustain)
                    {
                        pending[r.ch].push_back({a.ev, r.a});
                        pendingOn[r.ch].push_back(a.on);
                    }
                    else
                        closeNote(a.ev, a.on, t);
                    break;
                }
                }
            }
            const double endSec = toSec(endTick);
            for (auto &kv : active)
                for (const Active &a : kv.second)
                    closeNote(a.ev, a.on, std::max(endSec, a.on + 0.01));
            for (int c = 0; c < 16; ++c)
                for (size_t i = 0; i < pending[c].size(); ++i)
                    closeNote(pending[c][i].first, pendingOn[c][i], std::max(endSec, pendingOn[c][i] + 0.01));
            // slides: on the named channels a note inside another note is a slide of it (see the top of this file)
            for (int slideCh : opt.slideChannels)
            {
                std::vector<size_t> idx;
                for (size_t i = 0; i < ev.size(); ++i)
                    if (ev[i].k == 'n' && ev[i].ch == slideCh)
                        idx.push_back(i);
                std::map<size_t, std::vector<size_t>> slidesOf; // main note -> its slides
                for (size_t i : idx)
                {
                    const rau::Event &sNote = ev[i];
                    long best = -1;
                    for (size_t j : idx)
                    {
                        const rau::Event &m = ev[j];
                        if (j == i || m.t > sNote.t || m.t + m.dur < sNote.t + sNote.dur)
                            continue;
                        const bool bigger = m.t < sNote.t || m.t + m.dur > sNote.t + sNote.dur;
                        if (!bigger && j < i)
                            continue; // the same start and end: the one listed first is the slide
                        if (best < 0 || m.dur >= ev[size_t(best)].dur)
                            best = long(j); // the outermost one is the note
                    }
                    if (best >= 0 && ev[size_t(best)].pb != sNote.pb)
                        slidesOf[size_t(best)].push_back(i);
                }
                std::vector<bool> drop(ev.size(), false);
                std::vector<rau::Event> added;
                for (auto &kv : slidesOf)
                {
                    const rau::Event m = ev[kv.first];
                    std::sort(kv.second.begin(), kv.second.end(), [&](size_t a, size_t b) { return ev[a].t != ev[b].t ? ev[a].t < ev[b].t : a < b; });
                    double cur = 0; // cents above the note, where the last slide left it
                    for (size_t si : kv.second)
                    {
                        const rau::Event &sl = ev[si];
                        drop[si] = true;
                        const double target = (sl.pb - m.pb) * 100.0;
                        const int t0 = sl.t, t1 = sl.t + std::max(1, sl.dur);
                        for (int tk = t0 + 1; tk <= t1; ++tk)
                        {
                            rau::Event b;
                            b.k = 'B';
                            b.t = tk;
                            b.ch = slideCh;
                            b.v = int(std::lround(cur + (target - cur) * double(tk - t0) / double(t1 - t0)));
                            added.push_back(b);
                        }
                        cur = target;
                    }
                    rau::Event z; // the note is over: the next one on this channel starts at its own pitch
                    z.k = 'B';
                    z.t = m.t + m.dur;
                    z.ch = slideCh;
                    z.v = 0;
                    added.push_back(z);
                }
                std::vector<rau::Event> kept;
                for (size_t i = 0; i < ev.size(); ++i)
                    if (!drop[i])
                        kept.push_back(ev[i]);
                    else
                        --out.notes;
                kept.insert(kept.end(), added.begin(), added.end());
                ev = kept;
            }
            if (out.notes == 0)
                return why = "the MIDI file has no notes in it", false;
            out.seconds = loopEndTick >= 0 ? toSec(uint64_t(loopEndTick)) : std::max(endSec, lastEnd);
            out.song.end = std::max(1, toTicks(out.seconds));
            std::stable_sort(ev.begin(), ev.end(), [](const rau::Event &a, const rau::Event &b) { return a.t < b.t; }); // already in order but for bends put ahead of their note
            return true;
        }

    } // namespace midi
} // namespace gp
