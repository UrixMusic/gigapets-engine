// instrument_export.hpp - `--export-instruments <folder>`: the ROM's instruments and drums as audition files, so a composer can hear what each number is.
//
//   instrument_00.wav .. instrument_13.wav  a scale up through the instrument's range, a note about every half second, played the way the game plays it
//   drum_00.wav .. drum_11.wav               the drum hit
//   instruments.txt                          the numbers, how many samples each instrument has, its range, and which of the ROM's songs use it
// (no SDL: it only needs the ROM)
#pragma once
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include "audio_clip.hpp"
#include "gm_map.hpp"
#include "rom_audio.hpp"

namespace gp
{

    inline std::string noteName(int n)
    {
        static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return std::string(names[((n % 12) + 12) % 12]) + std::to_string(n / 12 - 1); // 60 = C4
    }

    // Writes the files into `dir` (made if needed). Returns "" or what went wrong.
    inline std::string exportInstruments(const Rom &rom, const std::string &dir)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
            return "cannot make " + dir;
        const rau::RomAudio audio(rom);
        const int rate = 44100;
        auto render = [&](const rau::Song &s)
        {
            const std::vector<float> f = audio.renderSong(s, rate);
            std::vector<int16_t> pcm(f.size());
            for (size_t i = 0; i < f.size(); ++i)
                pcm[i] = int16_t(std::clamp(f[i], -1.f, 1.f) * 32000.f);
            return pcm;
        };

        // which of the ROM's songs use which instrument and drum
        std::map<int, std::set<int>> usedBy, drumUsedBy;
        for (size_t si = 0; si < audio.songs().size(); ++si)
            for (const rau::Event &e : audio.songs()[si].ev)
            {
                if (e.k == 'p')
                    usedBy[e.v].insert(int(si));
                else if (e.k == 'n' && e.ch == 9)
                    drumUsedBy[e.pb].insert(int(si));
            }

        std::ostringstream txt;
        txt << "The ROM's 14 instruments and 12 drums, with the General MIDI sound each is closest to (a guess is marked with ?).\n"
               "A song written for any General MIDI player works as it is: a program change picks the closest instrument, and drum notes on channel 10 pick the closest\n"
               "drum. To choose one yourself where your editor writes no program changes (FL Studio): name the channel with the instrument's number, \"Lead [i3]\" (a plain [3] is FL's own), or\n"
               "with its sound, \"Trumpet\"; or list the channels in the music record, \"channels\": {\"1\": 3}. With \"numbers\": \"rom\" a program is the ROM's number\n"
               "(0 to 13) and a drum note is the drum number (0 to 11).\n\n";
        for (int prog = 0; prog < int(audio.programCount()) - 1; ++prog) // the last group is the drum kit
        {
            const std::vector<const rau::Sample *> zones = audio.instrumentSamples(prog);
            if (zones.empty())
                continue;
            const gm::RomInstrument &gmi = gm::romInstrument(prog);
            txt << "instrument " << prog << " = " << gmi.name << (gmi.guess ? "?" : "") << " (General MIDI program " << gmi.program << "; name a channel \"" << gmi.keyword
                << "\" or \"[i" << prog << "]\"): " << zones.size() << " sample" << (zones.size() == 1 ? "" : "s") << ", ";
            int lowest = 0;
            std::ostringstream detail;
            bool looped = false;
            for (const rau::Sample *z : zones)
            {
                detail << "  up to " << noteName(z->zoneTop) << " (" << z->zoneTop << "): sample rate " << z->rate << " Hz, centred on " << noteName(z->root) << " (" << z->root << ")"
                       << (z->hasLoop ? ", loops" : ", plays once") << (z->env ? ", has an envelope" : "") << "\n";
                looped = looped || z->hasLoop;
                lowest = std::max(lowest, z->zoneTop);
            }
            txt << (looped ? "sustains while the note is held" : "plays through once") << "; notes up to " << noteName(zones.back()->zoneTop) << "\n" << detail.str();
            txt << "  used by song";
            if (usedBy[prog].empty())
                txt << "s: none of the ROM's";
            else
            {
                txt << (usedBy[prog].size() > 1 ? "s" : "");
                for (int s : usedBy[prog])
                    txt << " " << s;
            }
            txt << "\n\n";

            // a scale from C2 to C6 in minor thirds, every 0.55 s; the notes are 0.45 s long
            rau::Song song;
            rau::Event p;
            p.k = 'p';
            p.ch = 0;
            p.v = prog;
            song.ev.push_back(p);
            int t = 0;
            for (int n = 36; n <= 84; n += 3)
            {
                rau::Event e;
                e.k = 'n';
                e.t = t;
                e.ch = 0;
                e.pb = n;
                e.dur = 90;
                e.vol = 127;
                e.vel = 110;
                e.pan = 64;
                song.ev.push_back(e);
                t += 110;
            }
            song.end = t + 400;
            char name[64];
            std::snprintf(name, sizeof name, "instrument_%02d.wav", prog);
            if (!writeWav16((fs::path(dir) / name).string(), render(song), rate))
                return std::string("cannot write ") + name;
        }
        txt << "(The ROM has a 15th group of samples: they are the 12 drums below, not an instrument to play notes on.)\n\nDrums (MIDI channel 10):\n";
        for (int d = 0; d < int(audio.drumCount()); ++d)
        {
            const rau::Sample *z = audio.drum(size_t(d));
            txt << "drum " << d << " = " << gm::romDrum(d).name << " (General MIDI drum note " << gm::romDrum(d).note << "): " << (z ? std::to_string(z->len) + " samples at " + std::to_string(z->rate) + " Hz" : std::string("(none)")) << "; used by song";
            if (drumUsedBy[d].empty())
                txt << "s: none of the ROM's";
            else
            {
                txt << (drumUsedBy[d].size() > 1 ? "s" : "");
                for (int s : drumUsedBy[d])
                    txt << " " << s;
            }
            txt << "\n";
            rau::Song song;
            rau::Event e;
            e.k = 'n';
            e.ch = 9;
            e.pb = d;
            e.vol = 127;
            e.vel = 120;
            e.pan = 64;
            song.ev.push_back(e);
            song.end = 500;
            char name[64];
            std::snprintf(name, sizeof name, "drum_%02d.wav", d);
            if (!writeWav16((fs::path(dir) / name).string(), render(song), rate))
                return std::string("cannot write ") + name;
        }
        std::ofstream f(fs::path(dir) / "instruments.txt");
        f << txt.str();
        return f ? "" : "cannot write instruments.txt";
    }

} // namespace gp
