// tests/audio_test.cpp - the mixer's arithmetic (engine/audio.hpp), without a sound device.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "../src/engine/audio/audio.hpp"
using namespace gp;

int main(int, char **)
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    AudioClip c;
    c.pcm = {100, -100, 200, -200, 300, -300}; // three frames
    check("three frames", c.frames() == 3);
    {
        int32_t out[8] = {};
        const size_t p = mixClip(c, 0, false, 1.0f, out, 2);
        check("two frames in at full volume", p == 2 && out[0] == 100 && out[1] == -100 && out[2] == 200 && out[3] == -200);
        const size_t q = mixClip(c, p, false, 1.0f, out + 4, 4);
        check("the last frame, then it ends and adds nothing more", q == 3 && out[4] == 300 && out[6] == 0);
    }
    {
        int32_t out[10] = {};
        mixClip(c, 0, true, 0.5f, out, 5);
        check("a loop wraps round at half volume", out[0] == 50 && out[4] == 150 && out[6] == 50 && out[8] == 100);
    }
    {
        int32_t out[4] = {1000, 1000, 1000, 1000};
        mixClip(c, 0, false, 1.0f, out, 2);
        check("it adds to what is already there", out[0] == 1100 && out[1] == 900);
    }
    {
        AudioClip e;
        int32_t out[2] = {};
        check("an empty clip does nothing", mixClip(e, 0, true, 1.0f, out, 1) == 0 && out[0] == 0);
    }

    { // a mod's sound files: its own sounds and replacements of the ROM's, with volume, pitch and loop points (no sound device needed)
        const std::string dir = (std::filesystem::temp_directory_path() / "gp_audiotest_").string();
        {
            std::vector<uint8_t> w = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a'};
            std::vector<uint8_t> pcm;
            for (int i = 0; i < 1000; ++i)
            {
                pcm.push_back(0x00);
                pcm.push_back(0x10); // 0x1000, 44.1 kHz mono
            }
            const uint32_t n = uint32_t(pcm.size());
            for (int k = 0; k < 4; ++k)
                w.push_back(uint8_t(n >> (8 * k)));
            w.insert(w.end(), pcm.begin(), pcm.end());
            std::ofstream f(dir + "a.wav", std::ios::binary);
            f.write(reinterpret_cast<const char *>(w.data()), std::streamsize(w.size()));
        }
        AudioRegistry &reg = audioRegistry();
        reg.clear();
        reg.sounds.push_back({"m:sound/a", dir + "a.wav", 0.5f, 0, 0, 0});
        reg.soundOver[84] = {"base:sound/84", dir + "a.wav", 1.f, 12, 0, 0}; // an octave up: half as long
        reg.songs.push_back({"m:music/a", dir + "a.wav", 1.f, 0, 0.005, 0.01});
        reg.songOver[4] = {"base:music/4", dir + "missing.wav", 1.f, 0, 0, 0};
        Audio a; // not opened: nothing plays, but the clips can be made
        const AudioClip *own = a.sfxClipFor(kModSoundBase);
        check("a mod's own sound is its file at half volume", own && own->frames() == 1000 && own->pcm[400] == 0x800);
        const AudioClip *over = a.sfxClipFor(84);
        check("a replaced ROM sound is the file, pitched up", over && over->frames() == 500 && over->pcm[400] == 0x1000);
        check("the same clip is not made twice", a.sfxClipFor(kModSoundBase) == own);
        const AudioClip *song = a.musicClipFor(kModMusicBase);
        check("a mod's song carries its loop points", song && song->loopStart == 220 && song->loopEnd == 441); // 0.005 s, 0.01 s at 44.1 kHz
        check("a mod's sound number nobody added has no clip", !isModSound(kModSoundBase + 5) && a.sfxClipFor(kModSoundBase + 5) == nullptr);
        { // a song made on another thread: null at first, then the clip; asking for two at once makes both
            reg.songs.push_back({"m:music/b", dir + "a.wav", 0.5f, 0, 0, 0});
            reg.songs.push_back({"m:music/gone", dir + "nowhere.wav", 1.f, 0, 0, 0});
            auto wait = [&](int n) // asks until the clip is there (or the job is over without one)
            {
                const AudioClip *c = nullptr;
                for (int i = 0; i < 500; ++i)
                {
                    c = a.musicClipAsync(n);
                    if (c || !a.renderingMusic())
                        break;
                    SDL_Delay(10);
                }
                return c ? c : a.musicClipAsync(n);
            };
            const AudioClip *first = a.musicClipAsync(kModMusicBase + 1); // starts the job
            const AudioClip *second = a.musicClipAsync(kModMusicBase + 2);  // asked for while the first is being made: its turn comes next
            (void)first;
            (void)second;
            const AudioClip *b = wait(kModMusicBase + 1);
            check("a song made in the background is its file at half volume", b && b->frames() == 1000 && b->pcm[400] == 0x800);
            const AudioClip *gone = wait(kModMusicBase + 2);
            check("a song whose file is missing stays null and the worker is free again", gone == nullptr && !a.renderingMusic());
            check("asking again gives the same clip", a.musicClipAsync(kModMusicBase + 1) == b && a.musicClipFor(kModMusicBase + 1) == b);
        }
        check("registry lookups", modSoundNumber("m:sound/a") == kModSoundBase && modSongNumber("m:music/a") == kModMusicBase && modSoundNumber("m:sound/zz") == -1);
        reg.clear();
        check("cleared", !isModSound(kModSoundBase) && soundFileFor(84) == nullptr);
        std::filesystem::remove(dir + "a.wav");
    }

    std::printf(fails ? "audio_test: %d failure(s)\n" : "audio_test: all passed\n", fails);
    return fails ? 1 : 0;
}
