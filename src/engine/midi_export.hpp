// midi_export.hpp - `--export-music <folder>`: the ROM's songs as MIDI files, to open in a DAW, change, and use back as a `music` record.
//
// Format 1, 96 ticks to the quarter note at 125 bpm: that is exactly 200 MIDI ticks a second, the rate the game's songs are counted in, so every
// time and length in the file is the song's own. One track for the tempo and notes about the song, then one track per channel the song uses, named
// like "Trumpet [i13]" (the General MIDI sound it is closest to and the instrument's number, which the importer reads back) or "Drums".
//   program      the instrument's General MIDI program (gm_map.hpp)
//   drums        channel 10, each drum as its General MIDI drum note
//   volume, pan  controllers 7 and 10 before each note that changes them (the ROM gives every note its own)
//   bends        the ROM's bends set a pitch directly; here they are pitch bends in cents above the note (range 24 semitones, set with RPN 0)
//   flags        text events "ROM flag 120" at their time (the importer ignores them)
// Reading a file back with the default settings gives the song again: same notes, times, lengths, volumes, pans and instruments.
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include "gm_map.hpp"
#include "rom_audio.hpp"

namespace gp
{
    namespace midi
    {

        constexpr int kExportPpq = 96, kExportBpm = 125; // 96 * 125 / 60 = 200 ticks a second

        namespace detail
        {
            inline void putVar(std::vector<uint8_t> &b, uint32_t v)
            {
                uint8_t t[5];
                int n = 0;
                t[n++] = v & 0x7f;
                while (v >>= 7)
                    t[n++] = uint8_t(0x80 | (v & 0x7f));
                while (n)
                    b.push_back(t[--n]);
            }
            struct Ev
            {
                int tick = 0;
                int order = 1; // note offs first at one tick
                std::vector<uint8_t> data;
            };
            inline std::vector<uint8_t> trackBytes(std::vector<Ev> ev, int endTick)
            {
                std::stable_sort(ev.begin(), ev.end(), [](const Ev &a, const Ev &b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
                std::vector<uint8_t> b;
                int last = 0;
                for (const Ev &e : ev)
                {
                    putVar(b, uint32_t(e.tick - last));
                    last = e.tick;
                    b.insert(b.end(), e.data.begin(), e.data.end());
                }
                putVar(b, uint32_t(std::max(0, endTick - last)));
                b.insert(b.end(), {0xff, 0x2f, 0x00});
                std::vector<uint8_t> t = {'M', 'T', 'r', 'k'};
                const uint32_t n = uint32_t(b.size());
                t.insert(t.end(), {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)});
                t.insert(t.end(), b.begin(), b.end());
                return t;
            }
            inline Ev meta(int tick, int type, const std::string &text)
            {
                Ev e;
                e.tick = tick;
                e.data = {0xff, uint8_t(type)};
                putVar(e.data, uint32_t(text.size()));
                e.data.insert(e.data.end(), text.begin(), text.end());
                return e;
            }
        } // namespace detail

        // One song as a MIDI file. `audio` gives the samples a bend is measured against.
        inline std::vector<uint8_t> songToMidi(const rau::Song &song, const rau::RomAudio &audio, const std::string &title)
        {
            using namespace detail;
            // events of each channel
            std::map<int, std::vector<Ev>> chan;
            std::vector<Ev> cond;
            const int end = std::max(1, song.end);
            cond.push_back(meta(0, 0x03, title));
            cond.push_back(meta(end, 0x06, "loop end")); // where the song loops: notes still sounding then ring on into the next round
            {
                Ev t;
                const uint32_t us = 60000000u / kExportBpm; // microseconds to the quarter note
                t.data = {0xff, 0x51, 0x03, uint8_t(us >> 16), uint8_t(us >> 8), uint8_t(us)};
                cond.push_back(t);
                Ev ts;
                ts.data = {0xff, 0x58, 0x04, 4, 2, 24, 8}; // 4/4
                cond.push_back(ts);
            }
            std::map<int, int> program, vol, pan, firstProgram; // what each channel has now
            std::map<int, bool> rangeSet;
            struct Active
            {
                int on, off, pb, program;
                bool bent = false;
            };
            std::map<int, std::vector<Active>> notes;
            std::map<int, int> bendNow;
            for (const rau::Event &e : song.ev)
            {
                const int c = e.ch & 15;
                if (e.k == 'f')
                    cond.push_back(meta(e.t, 0x01, "ROM flag " + std::to_string(e.v)));
                else if (e.k == 'p')
                {
                    program[c] = e.v;
                    if (!firstProgram.count(c))
                        firstProgram[c] = e.v;
                    Ev p;
                    p.tick = e.t;
                    p.data = {uint8_t(0xc0 | c), uint8_t(gm::romInstrument(e.v).program)};
                    if (c != 9)
                        chan[c].push_back(p);
                }
                else if (e.k == 'n')
                {
                    auto cc = [&](int number, int value, std::map<int, int> &state)
                    {
                        if (state.count(c) && state[c] == value)
                            return;
                        state[c] = value;
                        Ev x;
                        x.tick = e.t;
                        x.data = {uint8_t(0xb0 | c), uint8_t(number), uint8_t(std::clamp(value, 0, 127))};
                        chan[c].push_back(x);
                    };
                    cc(7, e.vol, vol);
                    cc(10, e.pan, pan);
                    const int note = c == 9 ? gm::romDrum(e.pb).note : e.pb;
                    Ev on, off;
                    on.tick = e.t;
                    on.data = {uint8_t(0x90 | c), uint8_t(std::clamp(note, 0, 127)), uint8_t(std::clamp(e.vel, 1, 127))};
                    off.tick = e.t + std::max(1, e.dur);
                    off.order = 0;
                    off.data = {uint8_t(0x80 | c), uint8_t(std::clamp(note, 0, 127)), 0};
                    chan[c].push_back(on);
                    chan[c].push_back(off);
                    notes[c].push_back({e.t, off.tick, e.pb, program.count(c) ? program[c] : 0});
                }
                else if (e.k == 'B' || e.k == 'b')
                {
                    // the note it bends: the latest one on the channel that is sounding
                    double cents = 0;
                    bool found = false;
                    if (e.k == 'B')
                    {
                        cents = e.v;
                        found = true;
                    }
                    else
                    {
                        const std::vector<Active> &ns = notes[c];
                        for (size_t i = ns.size(); i-- > 0;)
                            if (ns[i].on <= e.t && e.t < ns[i].off && c != 9)
                            {
                                const rau::Sample *sm = audio.pick(ns[i].program, ns[i].pb);
                                if (sm)
                                {
                                    const double hdr = double(std::max<uint32_t>(sm->rate ? sm->rate : 11025, 8000));
                                    const double normal = std::pow(2.0, (ns[i].pb - sm->root) / 12.0) * hdr;
                                    const double bent = e.v * 281250.0 / 524288.0;
                                    if (normal > 0 && bent > 0)
                                    {
                                        cents = 1200.0 * std::log2(bent / normal);
                                        found = true;
                                    }
                                }
                                break;
                            }
                    }
                    if (!found || c == 9)
                        continue;
                    if (!rangeSet[c])
                    {
                        rangeSet[c] = true;
                        for (const std::pair<int, int> &cv : {std::pair<int, int>{101, 0}, {100, 0}, {6, 24}, {38, 0}})
                        {
                            Ev x;
                            x.tick = 0;
                            x.data = {uint8_t(0xb0 | c), uint8_t(cv.first), uint8_t(cv.second)};
                            chan[c].push_back(x);
                        }
                    }
                    const int value = std::clamp(int(std::lround(8192.0 + cents / 2400.0 * 8192.0)), 0, 16383);
                    Ev b;
                    b.tick = e.t;
                    b.data = {uint8_t(0xe0 | c), uint8_t(value & 127), uint8_t(value >> 7)};
                    chan[c].push_back(b);
                    bendNow[c] = value;
                    // when the note ends, the bend goes back to centre so the next one starts at its own pitch
                    for (size_t i = notes[c].size(); i-- > 0;)
                        if (notes[c][i].on <= e.t && e.t < notes[c][i].off)
                        {
                            notes[c][i].bent = true;
                            break;
                        }
                }
            }
            for (auto &kv : notes)
                for (const Active &a : kv.second)
                    if (a.bent)
                    {
                        Ev b;
                        b.tick = a.off;
                        b.data = {uint8_t(0xe0 | kv.first), 0x00, 0x40};
                        chan[kv.first].push_back(b);
                    }

            std::vector<std::vector<uint8_t>> tracks;
            tracks.push_back(trackBytes(cond, end));
            for (auto &kv : chan)
            {
                const int c = kv.first;
                std::string name = "Channel " + std::to_string(c + 1);
                if (c == 9)
                    name = "Drums";
                else if (firstProgram.count(c))
                    name = std::string(gm::romInstrument(firstProgram[c]).name) + " [i" + std::to_string(firstProgram[c]) + "]";
                std::vector<Ev> ev = kv.second;
                ev.push_back(meta(0, 0x03, name));
                tracks.push_back(trackBytes(ev, end));
            }
            std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, uint8_t(tracks.size() >> 8), uint8_t(tracks.size()), 0, uint8_t(kExportPpq)};
            for (const auto &t : tracks)
                f.insert(f.end(), t.begin(), t.end());
            return f;
        }

    } // namespace midi

    // Writes song_0.mid ... and songs.txt into `dir`. Returns "" or what went wrong.
    inline std::string exportMusic(const Rom &rom, const std::string &dir)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
            return "cannot make " + dir;
        const rau::RomAudio audio(rom);
        std::ostringstream txt;
        txt << "The ROM's songs as MIDI files (96 ticks to the quarter note at 125 bpm = the game's 200 ticks a second, so every time is exact).\n"
               "Each channel is its own track named like \"Trumpet [i13]\": the General MIDI sound it is closest to and the ROM instrument's number. Drums are on\n"
               "channel 10 as General MIDI drum notes. Use a changed file with a music record: { \"type\": \"music\", \"mode\": \"replace\", \"id\": \"base:music/4\",\n"
               "\"file\": \"audio/song_4.mid\" }.\n\n";
        for (size_t i = 0; i < audio.songs().size(); ++i)
        {
            const rau::Song &s = audio.songs()[i];
            const std::vector<uint8_t> bytes = midi::songToMidi(s, audio, "ROM song " + std::to_string(i));
            const std::string name = "song_" + std::to_string(i) + ".mid";
            std::ofstream f(fs::path(dir) / name, std::ios::binary);
            f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
            if (!f)
                return "cannot write " + name;
            txt << name << ": " << (s.end / rau::kTicksPerSecond) << " seconds, plays in zone";
            std::string zones;
            for (int z = 0; z < 10; ++z)
                if (rau::zoneSong(z) == int(i))
                    zones += " " + std::to_string(z);
            txt << (zones.empty() ? "s: none (the menu uses song 4)" : zones) << (int(i) == rau::kMenuSong ? " and the menu" : "") << "\n";
        }
        std::ofstream t(fs::path(dir) / "songs.txt");
        t << txt.str();
        return t ? "" : "cannot write songs.txt";
    }

} // namespace gp
