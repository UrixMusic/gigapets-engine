// live_music_test.cpp <rom> - a song played as it goes (rau::SongPlayer) sounds the same as the baked one (RomAudio::renderSong).
#include "../src/engine/rom_audio.hpp"
#include <cmath>
#include <cstdio>

using namespace gp;

int main(int argc, char **argv)
{
    int fails = 0;
    auto check = [&](const std::string &w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) fails++; };

    Rom rom(argc > 1 ? argv[1] : "rom.u7");
    rau::RomAudio a(rom);
    const int rate = 22050;
    check("the ROM has songs", a.songCount() > 8);
    for (size_t n = 0; n < a.songCount(); ++n)
    {
        const rau::Song &S = a.songs()[n];
        const std::vector<float> baked = a.renderSong(S, rate);
        rau::SongPlayer p(a, S, rate);
        const std::string name = "song " + std::to_string(n);
        if (baked.empty())
        {
            check(name + ": nothing to play either way", !p.playable());
            continue;
        }
        const size_t body = baked.size() / 2;
        // two times round in uneven pieces (the mixer asks for what it likes); the second time round has the tail of the first folded in
        std::vector<float> live(body * 2 * 2, 0.f);
        for (size_t at = 0; at < body * 2;)
        {
            const size_t n2 = std::min<size_t>(777, body * 2 - at);
            p.render(live.data() + at * 2, n2);
            at += n2;
        }
        float pk = 0;
        for (size_t i = 0; i < body * 2; ++i)
            pk = std::max(pk, std::fabs(live[body * 2 + i]));
        const float k = pk > 0.98f ? 0.98f / pk : 1.f; // the baked one is turned down if it would clip
        double worst = 0, ref = 0;
        for (size_t i = 0; i < body * 2; ++i)
        {
            worst = std::max(worst, double(std::fabs(live[body * 2 + i] * k - baked[i])));
            ref = std::max(ref, double(std::fabs(baked[i])));
        }
        std::printf("      %s: %.1f s, unscaled peak %.2f, worst difference %.6f\n", name.c_str(), body / double(rate), pk, worst);
        check(name + ": the second time round is the baked song", worst < 1e-4);
    }
    {
        rau::Song empty;
        rau::SongPlayer p(a, empty, rate);
        std::vector<float> out(64, 0.f);
        p.render(out.data(), 32);
        check("a song with no notes is silent", !p.playable() && out[0] == 0.f);
    }
    std::printf(fails ? "live_music_test: %d failure(s)\n" : "live_music_test: all passed\n", fails);
    return fails ? 1 : 0;
}
