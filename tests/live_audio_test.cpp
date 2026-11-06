// live_audio_test.cpp <rom> - the Audio class's live music switch (setLive), on SDL's dummy audio device.
#include "../src/engine/audio/audio.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace gp;

int main(int argc, char **argv)
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    Rom rom(argc > 1 ? argv[1] : "rom.u7");
    const std::string dir = (std::filesystem::temp_directory_path() / "gp_liveaudio").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/music");
    audioRegistry().clear();

    Audio a;
    check("the dummy device opens", a.open(dir));
    a.setRom(rom);
    check("live is off to begin with", !a.live());
    a.playMusic(4);
    check("off: the ROM's song is baked", a.music() == 4 && !a.playingLive());
    a.setLive(true);
    check("switching on starts the song again, as it goes", a.live() && a.music() == 4 && a.playingLive());
    { // the mixer runs on the dummy device's thread: the scan finds how loud song 4 gets (more than 0 dB: it is turned down)
        for (int i = 0; i < 400 && a.liveScale() <= 0.f; ++i)
            SDL_Delay(25);
        check("the scan finds the song's level", a.liveScale() > 0.2f && a.liveScale() < 1.f);
    }
    a.playMusic(2);
    check("another song is live too", a.music() == 2 && a.playingLive());
    a.playMusic(999);
    check("a song that is not there is not live", !a.playingLive());
    a.playMusic(3);
    check("back to live", a.playingLive());
    a.stopMusic();
    check("stopped", a.music() == -1 && !a.playingLive());

    { // a wav file in the audio folder wins over the ROM's song
        std::ofstream f(dir + "/music/5.wav", std::ios::binary);
        std::vector<uint8_t> w = {'R', 'I', 'F', 'F', 40, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 4, 0, 0, 0, 0, 0x10, 0, 0x10};
        f.write(reinterpret_cast<const char *>(w.data()), std::streamsize(w.size()));
    }
    a.playMusic(5);
    check("a wav file for the song: baked", a.music() == 5 && !a.playingLive());
    { // a MIDI record with a pitch change cannot be live
        audioRegistry().songOver[6] = {"base:music/6", dir + "/nope.mid", 1.f, 3, 0, 0};
        audioRegistry().songOver[6].midi = true;
        a.playMusic(6);
        check("a MIDI song with a pitch change is baked", a.music() == 6 && !a.playingLive());
    }
    { // a ROM song with another instrument on a channel is live too
        ModSound patch;
        patch.id = "base:music/7";
        patch.channels[0] = 3;
        audioRegistry().songPatches[7] = patch;
        ++audioRegistry().generation;
        a.playMusic(7);
        check("a patched ROM song is live", a.music() == 7 && a.playingLive());
    }
    a.setLive(false);
    a.playMusic(4);
    check("off again: baked", a.music() == 4 && !a.playingLive());
    a.setLive(true);
    check("switching on again restarts it live", a.playingLive());
    a.setLive(false);
    check("and off stops the live player", !a.playingLive() && a.music() == 4);

    a.close();
    audioRegistry().clear();
    std::filesystem::remove_all(dir);
    std::printf(fails ? "live_audio_test: %d failure(s)\n" : "live_audio_test: all passed\n", fails);
    return fails ? 1 : 0;
}
