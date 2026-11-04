// gm_map.hpp - the ROM's instruments and drums, and the General MIDI sounds they sound closest to (no SDL, no ROM).
//
// The names are a listener's closest matches (instrument-names.txt in instruments/); 4, 7 and 11 are guesses from the sound's spectrum:
//   4  a nearly pure sine tone              -> Flute
//   7  odd harmonics only, no attack        -> Clarinet
//   11 a sustained pad, strong 2nd harmonic -> String Ensemble
// Each ROM instrument has one canonical GM program (what --export-music writes, so a song comes back as it was); other GM programs are taken to
// the nearest one in their family, so a song written for any General MIDI player plays with something sensible.
#pragma once
#include <cctype>
#include <string>

namespace gp
{
    namespace gm
    {

        constexpr int kRomInstruments = 14, kRomDrums = 12;

        struct RomInstrument
        {
            const char *name;     // closest General MIDI sound
            int program;          // its GM program number (0-based, as written in a MIDI file)
            bool guess;           // not confirmed by ear
            const char *keyword;  // a channel named with this word picks it ("Trumpet")
        };
        inline const RomInstrument &romInstrument(int i)
        {
            static const RomInstrument t[kRomInstruments] = {
                {"Vibraphone", 11, false, "vibraphone"},
                {"Acoustic Bass", 32, false, "bass"},
                {"Piano (Acoustic Grand)", 0, false, "piano"},
                {"Piano (second sample set)", 1, false, "bright piano"},
                {"Flute", 73, true, "flute"},
                {"Pizzicato Strings", 45, false, "pizzicato"},
                {"Pizzicato Strings (second)", 46, false, "harp"},
                {"Clarinet", 71, true, "clarinet"},
                {"Steel-String Guitar", 25, false, "guitar"},
                {"Synth Voice (slow attack)", 54, false, "synth voice"},
                {"Synth Voice (immediate)", 85, false, "voice lead"},
                {"String Ensemble", 48, true, "strings"},
                {"Orchestra Hit", 55, false, "orchestra hit"},
                {"Trumpet", 56, false, "trumpet"},
            };
            return t[i < 0 ? 0 : i >= kRomInstruments ? kRomInstruments - 1 : i];
        }

        struct RomDrum
        {
            const char *name;
            int note; // its General MIDI drum note
        };
        inline const RomDrum &romDrum(int i)
        {
            static const RomDrum t[kRomDrums] = {
                {"Pedal Hi-Hat", 44}, {"Bass Drum", 36}, {"Low Bongo", 61}, {"Acoustic Snare", 38}, {"Low Conga", 64}, {"Side Stick", 37},
                {"Crash Cymbal (no direct match)", 49}, {"Electric Snare", 40}, {"Tambourine", 54}, {"Maracas", 70}, {"Hand Clap", 39}, {"Low Agogo", 68},
            };
            return t[i < 0 ? 0 : i >= kRomDrums ? kRomDrums - 1 : i];
        }

        // A General MIDI program (0..127) -> the ROM's instrument. `exact` is set when it is that instrument's own program; otherwise it is the nearest
        // in the GM family.
        inline int programToRom(int gmProgram, bool *exact = nullptr)
        {
            for (int i = 0; i < kRomInstruments; ++i)
                if (romInstrument(i).program == gmProgram)
                {
                    if (exact)
                        *exact = true;
                    return i;
                }
            if (exact)
                *exact = false;
            static const int family[16] = {2,   // 0-7 pianos
                                           0,   // 8-15 chromatic percussion
                                           11,  // 16-23 organs
                                           8,   // 24-31 guitars
                                           1,   // 32-39 basses
                                           11,  // 40-47 strings (45 is pizzicato)
                                           11,  // 48-55 ensemble
                                           13,  // 56-63 brass
                                           7,   // 64-71 reeds
                                           4,   // 72-79 pipes
                                           9,   // 80-87 synth leads
                                           11,  // 88-95 synth pads
                                           9,   // 96-103 synth effects
                                           8,   // 104-111 ethnic
                                           0,   // 112-119 percussive
                                           9};  // 120-127 sound effects
            if (gmProgram == 46)
                return 6;
            return family[(gmProgram < 0 ? 0 : gmProgram > 127 ? 127 : gmProgram) / 8];
        }

        // A General MIDI drum note (35..81) -> the ROM's drum, or -1 when there is nothing like it.
        inline int drumToRom(int note)
        {
            switch (note)
            {
            case 35: case 36: return 1;                          // bass drums
            case 37: case 75: return 5;                          // side stick, claves
            case 38: return 3;                                   // acoustic snare
            case 39: return 10;                                  // hand clap
            case 40: return 7;                                   // electric snare
            case 41: case 43: case 45: case 47: case 48: case 50: return 4; // toms
            case 42: case 44: case 46: return 0;                 // hi-hats
            case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 6; // cymbals
            case 54: return 8;                                   // tambourine
            case 56: case 67: case 68: return 11;                // cowbell, agogos
            case 60: case 61: case 65: case 66: return 2;        // bongos, timbales
            case 62: case 63: case 64: return 4;                 // congas
            case 69: case 70: return 9;                          // cabasa, maracas
            }
            return -1;
        }

        // The instrument a channel's name asks for, by a word in it ("Trumpet", "my Pizzicato lead"), or -1. The longest matching word wins.
        inline int instrumentFromName(const std::string &name)
        {
            std::string low;
            for (char c : name)
                low += char(std::tolower((unsigned char)c));
            int best = -1;
            size_t bestLen = 0;
            for (int i = 0; i < kRomInstruments; ++i)
            {
                const std::string k = romInstrument(i).keyword;
                if (k.size() > bestLen && low.find(k) != std::string::npos)
                {
                    best = i;
                    bestLen = k.size();
                }
            }
            return best;
        }

    } // namespace gm
} // namespace gp
