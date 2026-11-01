// rom_audio_test.cpp - the ROM sound decoder: usage rom_audio_test [rom.u7]
#include "../src/engine/rom_audio.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main(int argc, char **argv)
{
    gp::Rom rom(argc > 1 ? argv[1] : "rom.u7");
    gp::rau::RomAudio a(rom);
    CHECK(a.sampleCount() > 100);
    int sfx = 0;
    for (int i = 0; i < 0x60; ++i)
        sfx += !a.sfx(i, 44100).empty();
    CHECK(sfx == 96);
    CHECK(a.songCount() == 9);
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < a.songCount(); ++i)
    {
        const auto m = a.music(i, 44100);
        std::printf("song %zu: %zu events, %d ticks, %.1f s\n", i, a.songs()[i].ev.size(), a.songs()[i].end, m.size() / 2 / 44100.0);
        CHECK(!m.empty());
        float pk = 0;
        for (float f : m)
            pk = std::max(pk, std::fabs(f));
        CHECK(pk > 0.05f && pk <= 1.0f);
    }
    { // retro mode: no blending between samples, so it steps (fewer different neighbouring values) and still has the same length
        gp::rau::RomAudio b(rom);
        b.nearest = true;
        const auto smooth = a.sfx(0x59, 44100), steps = b.sfx(0x59, 44100);
        CHECK(smooth.size() == steps.size() && !steps.empty());
        size_t same = 0, sameSmooth = 0;
        for (size_t i = 2; i + 2 < steps.size(); i += 2)
        {
            same += steps[i] == steps[i + 2];
            sameSmooth += smooth[i] == smooth[i + 2];
        }
        std::printf("sound 0x59: %zu repeated neighbours with no blending, %zu with\n", same, sameSmooth);
        CHECK(same > sameSmooth * 2 + 10);
    }
    std::printf("render all: %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    std::printf(fails ? "FAILED\n" : "PASS\n");
    return fails;
}
