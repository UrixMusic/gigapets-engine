// tests/trick_showcase_test.cpp - the teacher's trick showcase (TickPetNPCBehavior's trick states).
#include <cstdio>
#include <vector>
#include "../src/game/trick_showcase.hpp"
using namespace gp;

// A fake teacher: every animation takes `frames` ticks. Returns the ids played, in order.
static std::vector<int> run(int level, int species, int playerSpecies, int frames, int *ticksOut = nullptr)
{
    std::vector<int> played;
    int left = 0;
    TrickShowcase s;
    TrickShowcase::Host h;
    h.play = [&](int id, int)
    { played.push_back(id); left = frames; return true; };
    h.busy = [&]
    { return left > 0; };
    s.start(level, species, playerSpecies);
    int t = 0;
    for (; t < 10000; ++t)
    {
        if (left > 0)
            --left; // the animation advances once per tick, before the sequence looks at it
        if (s.tick(h))
            break;
    }
    if (ticksOut)
        *ticksOut = t;
    return played;
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("level 1 plays its trick once", run(1, 0, 0, 6) == std::vector<int>{0x1b});
    check("level 1 species 2 uses 0xfc", run(1, 2, 2, 6) == std::vector<int>{0xfc});
    check("level 2 species 0 plays once, no intro", run(2, 0, 0, 6) == std::vector<int>{0x24});
    check("level 2 species 1 (Hand Stand): intro 0x83 then 0x84 x5",
          run(2, 1, 1, 6) == std::vector<int>{0x83, 0x84, 0x84, 0x84, 0x84, 0x84});
    check("level 2 species 2 plays once", run(2, 2, 2, 6) == std::vector<int>{0xe7});
    check("level 3 species 0: intro 0x33 then 0x21 x5", run(3, 0, 0, 6) == std::vector<int>{0x33, 0x21, 0x21, 0x21, 0x21, 0x21});
    check("level 3 species 1: intro 0x88 then 0x89 x5", run(3, 1, 1, 6) == std::vector<int>{0x88, 0x89, 0x89, 0x89, 0x89, 0x89});
    check("level 3 species 2: intro 0x102 then 0xf0 x3", run(3, 2, 2, 6) == std::vector<int>{0x102, 0xf0, 0xf0, 0xf0});
    check("repeat count follows the player's species", TrickShowcase::repeats(3, 2) == 3 && TrickShowcase::repeats(3, 0) == 5);

    { // each animation is waited for: 1 intro + 3 mains of 6 ticks each is about 24 ticks plus bookkeeping
        int t = 0;
        run(3, 2, 2, 6, &t);
        std::printf("      level 3 / species 2 took %d ticks\n", t);
        check("waits for every animation to finish", t >= 4 * 6 && t < 4 * 6 + 10);
    }

    { // no art for an intro: skips it; no art at all still finishes (the game never hangs)
        std::vector<int> played;
        TrickShowcase s;
        TrickShowcase::Host h;
        h.play = [&](int id, int)
        { played.push_back(id); return id != 0x33; }; // no art for the intro
        h.busy = [] { return false; };
        s.start(3, 0, 0);
        bool done = false;
        for (int i = 0; i < 100 && !done; ++i)
            done = s.tick(h);
        check("missing intro art is skipped", done && played.size() == 6 && played[0] == 0x33 && played[1] == 0x21);
        check("finished showcase is inactive", !s.active());
    }

    // ---- the full sequence: sounds, reactions and the closing animation ----
    struct Fake
    {
        std::vector<int> played, sounds, stopped;
        std::vector<bool> reversed;
        int left = 0, cur = -1, frame = 0, nextRand = 1;
        double now = 0;
        std::vector<int> active;
        TrickShowcase::Host host(int frames)
        {
            TrickShowcase::Host h;
            h.full = true;
            h.play = [this, frames](int id, int) { played.push_back(id); reversed.push_back(false); cur = id; left = frames; return true; };
            h.playReverse = [this, frames](int id, int, bool) { played.push_back(id); reversed.push_back(true); cur = id; left = frames; return true; };
            h.busy = [this] { return left > 0; };
            h.anim = [this] { return cur; };
            h.frame = [this] { return frame; };
            h.sound = [this](int id, int) { sounds.push_back(id); };
            h.soundActive = [this](int id) { for (int a : active) if (a == id) return true; return false; };
            h.stopSound = [this](int id) { stopped.push_back(id); };
            h.rand = [this](int lo, int) { return lo + nextRand - 1; };
            h.seconds = [this] { return now; };
            return h;
        }
        void run(TrickShowcase &s, int frames, int maxTicks = 4000)
        {
            TrickShowcase::Host h = host(frames);
            for (int t = 0; t < maxTicks; ++t)
            {
                if (left > 0)
                    --left;
                now += 1.0 / 30;
                if (s.tick(h))
                    break;
            }
        }
    };
    {
        Fake f;
        TrickShowcase s;
        s.startWith(2, 1, 1); // monkey level 2: the closing animation 0x85 after one trick
        f.run(s, 4);
        check("the monkey's level 2 trick is followed by its closing animation 0x85, then the turn-around 0x9b and back 0x9c", f.played == std::vector<int>{0x83, 0x84, 0x85, 0x9b, 0x9c});
    }
    {
        Fake f;
        TrickShowcase s;
        s.startWith(3, 2, 1); // panda level 3: intro, trick, then 0x102 played backwards
        f.run(s, 4);
        check("the panda's level 3 closing animation is the intro played backwards", f.played.size() >= 3 && f.played.back() == 0x102 && f.reversed.back());
        check("and its channels are stopped before it", f.stopped.size() >= 4);
    }
    {
        Fake f;
        TrickShowcase s;
        s.startWith(1, 1, 1); // monkey level 1: sound 0x5b 0.6 s in
        f.run(s, 60);
        check("monkey level 1 sounds 0x5b once, after 0.6 s", f.sounds == std::vector<int>{0x5b});
    }
    {
        Fake f;
        TrickShowcase s;
        s.startWith(3, 1, 1);
        TrickShowcase::Host h = f.host(40);
        s.tick(h); // intro 0x88
        f.left = 0;
        s.tick(h); // wait done -> main
        s.tick(h); // plays 0x89
        s.tick(h);
        check("monkey level 3 sounds 0x1a while 0x89 plays", !f.sounds.empty() && f.sounds[0] == 0x1a);
    }
    {
        Fake f;
        f.frame = 0xc;
        TrickShowcase s;
        s.startWith(2, 2, 1);
        TrickShowcase::Host h = f.host(40);
        for (int i = 0; i < 5; ++i)
            s.tick(h);
        check("panda level 2 sounds 0x2e on frame 12", !f.sounds.empty() && f.sounds[0] == 0x2e);
        f.active = {0x2e};
        const size_t n = f.sounds.size();
        s.tick(h);
        check("not again while it is still sounding", f.sounds.size() == n);
    }
    {
        Fake f;
        TrickShowcase s;
        s.startWith(1, 2, 1); // panda level 1: the somersault, then its exit animation
        f.run(s, 4);
        check("the panda's level 1 somersault ends with the actor reaction animation 0xea", f.played == std::vector<int>{0xfc, 0xea});
    }
    {
        Fake f;
        TrickShowcase s;
        s.startWith(1, 0, 1); // the dog's level 1 has nothing after it
        f.run(s, 4);
        check("the dog's level 1 has no exit animation", f.played == std::vector<int>{0x1b});
    }
    {
        CameraShake c;
        check("at rest there is no shake", c.step() == 0 && !c.active());
        c.start();
        std::vector<int> d;
        for (int i = 0; i < 27; ++i)
            d.push_back(c.step());
        check("the shake runs the ROM's table as offsets from 0x19: +2, +5, +1, -3, -4, 0 ...", d[0] == 2 && d[1] == 5 && d[2] == 1 && d[3] == -3 && d[4] == -4 && d[5] == 0 && d[26] == 0);
        check("and settles at the rest value", c.step() == 0 && !c.active() && c.step() == 0);
        c.start();
        for (int i = 0; i < 4; ++i)
            c.step();
        c.start(); // past entry 2: starts over
        check("a start later in the shake starts it over", c.step() == 2);
        CameraShake e;
        e.start();
        e.step();
        e.start(); // still within the first 3: ignored
        check("a start in the first three entries is ignored", e.step() == 5);
    }
    {
        const auto rr = [](int lo, int) { return lo; };
        check("the dog's level 3 count: 1..3 up to 99 light bulbs, 3..6 to 999, 6..11 beyond",
              TrickShowcase::petRepeats(3, 0, 50, rr) == 1 && TrickShowcase::petRepeats(3, 0, 500, rr) == 3 && TrickShowcase::petRepeats(3, 0, 5000, rr) == 6);
        check("the monkey's levels 2 and 3 count the same way, level 1 is once", TrickShowcase::petRepeats(2, 1, 5000, rr) == 6 && TrickShowcase::petRepeats(1, 1, 5000, rr) == 1);
        check("the panda's level 3: 1..3, then 3..5, then exactly 5; its other levels once", TrickShowcase::petRepeats(3, 2, 50, rr) == 1 && TrickShowcase::petRepeats(3, 2, 500, rr) == 3 &&
                                                                                              TrickShowcase::petRepeats(3, 2, 5000, rr) == 5 && TrickShowcase::petRepeats(2, 2, 5000, rr) == 1);
        check("the dog's levels 1 and 2 are once", TrickShowcase::petRepeats(1, 0, 5000, rr) == 1 && TrickShowcase::petRepeats(2, 0, 5000, rr) == 1);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
