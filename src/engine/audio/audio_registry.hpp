// audio_registry.hpp - the sounds and songs mods bring (sound / music records), by the numbers the game asks for them with (no SDL).
//
// A sound effect is asked for by number: the ROM's 0..0x5f, or kModSoundBase + n for a mod's own. A song likewise: the ROM's 0..N, or
// kModMusicBase + n. A mod can also REPLACE one of the ROM's (a record on "base:sound/84" or "base:music/4"): the number stays the ROM's
// and the audio engine plays the mod's file instead. The numbers are made while the mods load and are not saved.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "audio_names.hpp" // "base:sound/kaboing" for 91, "base:music/ufo" for 8

namespace gp
{

    constexpr int kModSoundBase = 0x40000, kModMusicBase = 0x50000;
    constexpr int kRomSounds = 0x60, kRomSongs = 64;

    // One sound file and how to play it.
    struct ModSound
    {
        std::string id;   // "mymod:sound/meow", or the ROM's "base:sound/84"
        std::string path; // the file (.wav / .ogg), absolute or relative to the game folder
        float gain = 1.f; // 1 = as the file is
        double semitones = 0; // played this much higher (and so faster) or lower
        double loopStart = 0, loopEnd = 0; // music only, in seconds of the file; 0 = the whole of it
        bool midi = false;                 // music only: a MIDI file, played with the ROM's instruments (engine/midi.hpp)
        std::map<int, int> programs, drums; // for a MIDI file: its program -> the ROM's instrument, its drum note -> the ROM's drum
        bool gmNumbers = true;              // for a MIDI file: its programs and drum notes are General MIDI (else the ROM's own numbers)
        std::map<int, int> channels;        // for a MIDI file: the channel (0..15) -> the ROM's instrument it starts with
        std::vector<int> slideChannels;     // for a MIDI file: the channels (0..15) where two notes starting and ending together are a slide
        std::map<int, std::string> programIds, drumIds, channelIds; // the same three, but the instrument / drum is named by its id ("mymod:instrument/harp", "base:drum/5"); found when the song is made
    };

    // A mod's instrument for MIDI songs. Numbers 0..13 are the ROM's (a record on "base:instrument/3" replaces one), a mod's own are 14 and up
    // in the order they load. One or more samples ("zones"), each for the notes up to `top`; a sample with a loop keeps a held note going.
    constexpr int kRomInstrumentCount = 14, kRomDrumCount = 12;
    struct ModZone
    {
        std::string path;      // the sample (.wav / .ogg)
        float gain = 1.f;
        int root = 60, top = 127; // the MIDI note the sample is at, and the highest note it is used for
        bool loop = false;
        double loopStart = 0, loopEnd = 0; // seconds of the file; loopEnd 0 = the end of the file
    };
    struct ModInstrument
    {
        std::string id; // "mymod:instrument/harp" or "base:instrument/3"
        int program = 0;
        std::vector<ModZone> zones;
        double attack = 0.005, decay = 0, sustain = 1, release = 0.05; // seconds, and the level (0..1) the note settles to
    };
    // A drum: a sample played from its start when its note comes (no pitch from the note). 0..11 are the ROM's; a mod's own are 12 and up.
    struct ModDrum
    {
        std::string id; // "mymod:drum/clap" or "base:drum/5"
        int index = 0;
        std::string path;
        float gain = 1.f;
        double semitones = 0;
    };

    struct AudioRegistry
    {
        std::vector<ModInstrument> instruments;
        std::vector<ModDrum> drums;
        int generation = 0; // changes whenever the registry does: the audio engine rebuilds what it made from it
        std::vector<ModSound> sounds, songs;           // mod's own: number = base + index
        std::map<int, ModSound> soundOver, songOver;   // replacements of the ROM's, by its number
        std::map<int, ModSound> songPatches;           // a ROM song with other instruments (programs / drums / channels, no file), by its number
        void clear()
        {
            sounds.clear();
            songs.clear();
            soundOver.clear();
            songOver.clear();
            songPatches.clear();
            instruments.clear();
            drums.clear();
            ++generation;
        }
    };
    inline AudioRegistry &audioRegistry()
    {
        static AudioRegistry r;
        return r;
    }
    inline void forgetModAudio() { audioRegistry().clear(); } // before the mods are loaded (again)

    inline bool isModSound(int n) { return n >= kModSoundBase && size_t(n - kModSoundBase) < audioRegistry().sounds.size(); }
    inline bool isModSong(int n) { return n >= kModMusicBase && size_t(n - kModMusicBase) < audioRegistry().songs.size(); }

    // The number of a mod's sound / song by id, or -1.
    inline int modSoundNumber(const std::string &id)
    {
        const auto &v = audioRegistry().sounds;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].id == id)
                return kModSoundBase + int(i);
        return -1;
    }
    inline int modSongNumber(const std::string &id)
    {
        const auto &v = audioRegistry().songs;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].id == id)
                return kModMusicBase + int(i);
        return -1;
    }

    // The number of a mod's instrument / drum by id (the ROM's "base:instrument/3" is 3), or -1.
    inline int modInstrumentNumber(const std::string &id)
    {
        for (const ModInstrument &i : audioRegistry().instruments)
            if (i.id == id)
                return i.program;
        return -1;
    }
    inline int modDrumNumber(const std::string &id)
    {
        for (const ModDrum &d : audioRegistry().drums)
            if (d.id == id)
                return d.index;
        return -1;
    }

    // The number an id names: "base:instrument/3" is 3, a mod's is the number it was given; -1 if there is no such instrument.
    inline int instrumentIdNumber(const std::string &id)
    {
        const std::string p = "base:instrument/";
        if (id.rfind(p, 0) == 0)
        {
            char *end = nullptr;
            const long n = std::strtol(id.c_str() + p.size(), &end, 10);
            return id.size() > p.size() && !*end && n >= 0 && n < kRomInstrumentCount ? int(n) : -1;
        }
        return modInstrumentNumber(id);
    }
    inline int drumIdNumber(const std::string &id)
    {
        const std::string p = "base:drum/";
        if (id.rfind(p, 0) == 0)
        {
            char *end = nullptr;
            const long n = std::strtol(id.c_str() + p.size(), &end, 10);
            return id.size() > p.size() && !*end && n >= 0 && n < kRomDrumCount ? int(n) : -1;
        }
        return modDrumNumber(id);
    }
    // A MIDI record's programs / drums / channels with the ids turned into numbers (an id that names nothing is left out and added to `unknown`).
    inline void resolveModSound(const ModSound &m, std::map<int, int> &programs, std::map<int, int> &drums, std::map<int, int> &channels, std::vector<std::string> *unknown = nullptr)
    {
        programs = m.programs;
        drums = m.drums;
        channels = m.channels;
        auto go = [&](const std::map<int, std::string> &ids, std::map<int, int> &dst, bool drum) {
            for (const auto &kv : ids)
            {
                const int n = drum ? drumIdNumber(kv.second) : instrumentIdNumber(kv.second);
                if (n >= 0)
                    dst[kv.first] = n;
                else if (unknown)
                    unknown->push_back(std::string("no such ") + (drum ? "drum" : "instrument") + " '" + kv.second + "'");
            }
        };
        go(m.programIds, programs, false);
        go(m.drumIds, drums, true);
        go(m.channelIds, channels, false);
    }
    // Every instrument's id and number, the ROM's as "base:instrument/N" too: what a track name in square brackets can be.
    inline std::map<std::string, int> instrumentIdTable()
    {
        std::map<std::string, int> t;
        for (int i = 0; i < kRomInstrumentCount; ++i)
            t["base:instrument/" + std::to_string(i)] = i;
        for (const ModInstrument &i : audioRegistry().instruments)
            t[i.id] = i.program;
        return t;
    }

    // The number an id names, whoever made it: the ROM's ("base:sound/91" or "base:sound/kaboing"; "base:music/8" or "base:music/ufo") or a mod's. -1 if none.
    inline int soundNumberOf(const std::string &id)
    {
        const int r = romSoundNumber(id);
        return r >= 0 ? r : modSoundNumber(id);
    }
    inline int songNumberOf(const std::string &id)
    {
        const int r = romSongNumber(id);
        return r >= 0 ? r : modSongNumber(id);
    }

    // The file a sound number plays instead of the ROM's, or null (the ROM's own sound).
    inline const ModSound *soundFileFor(int n)
    {
        if (isModSound(n))
            return &audioRegistry().sounds[size_t(n - kModSoundBase)];
        const auto it = audioRegistry().soundOver.find(n);
        return it == audioRegistry().soundOver.end() ? nullptr : &it->second;
    }
    // The changes a mod makes to the instruments of the ROM's song n (when no mod gives a file for it), or null.
    inline const ModSound *songPatchFor(int n)
    {
        if (audioRegistry().songOver.count(n))
            return nullptr;
        const auto it = audioRegistry().songPatches.find(n);
        return it == audioRegistry().songPatches.end() ? nullptr : &it->second;
    }
    inline const ModSound *songFileFor(int n)
    {
        if (isModSong(n))
            return &audioRegistry().songs[size_t(n - kModMusicBase)];
        const auto it = audioRegistry().songOver.find(n);
        return it == audioRegistry().songOver.end() ? nullptr : &it->second;
    }

} // namespace gp
