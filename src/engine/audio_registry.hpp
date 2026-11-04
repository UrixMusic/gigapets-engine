// audio_registry.hpp - the sounds and songs mods bring (sound / music records), by the numbers the game asks for them with (no SDL).
//
// A sound effect is asked for by number: the ROM's 0..0x5f, or kModSoundBase + n for a mod's own. A song likewise: the ROM's 0..N, or
// kModMusicBase + n. A mod can also REPLACE one of the ROM's (a record on "base:sound/84" or "base:music/4"): the number stays the ROM's
// and the audio engine plays the mod's file instead. The numbers are made while the mods load and are not saved.
#pragma once
#include <map>
#include <string>
#include <vector>

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
    };

    struct AudioRegistry
    {
        std::vector<ModSound> sounds, songs;           // mod's own: number = base + index
        std::map<int, ModSound> soundOver, songOver;   // replacements of the ROM's, by its number
        void clear()
        {
            sounds.clear();
            songs.clear();
            soundOver.clear();
            songOver.clear();
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

    // The file a sound number plays instead of the ROM's, or null (the ROM's own sound).
    inline const ModSound *soundFileFor(int n)
    {
        if (isModSound(n))
            return &audioRegistry().sounds[size_t(n - kModSoundBase)];
        const auto it = audioRegistry().soundOver.find(n);
        return it == audioRegistry().soundOver.end() ? nullptr : &it->second;
    }
    inline const ModSound *songFileFor(int n)
    {
        if (isModSong(n))
            return &audioRegistry().songs[size_t(n - kModMusicBase)];
        const auto it = audioRegistry().songOver.find(n);
        return it == audioRegistry().songOver.end() ? nullptr : &it->second;
    }

} // namespace gp
