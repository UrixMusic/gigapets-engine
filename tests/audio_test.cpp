// tests/audio_test.cpp - the mixer's arithmetic (engine/audio.hpp), without a sound device.
#include <cstdio>
#include "../src/engine/audio.hpp"
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

    std::printf(fails ? "audio_test: %d failure(s)\n" : "audio_test: all passed\n", fails);
    return fails ? 1 : 0;
}
