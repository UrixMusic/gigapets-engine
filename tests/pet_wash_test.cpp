// tests/pet_wash_test.cpp - washing the pet (HandlePetWashInteraction, activity 0x27).
#include <cstdio>
#include <vector>
#include "../src/game/pet_sit.hpp"
#include "../src/game/pet_wash.hpp"
#include "../src/game/reaction_seq.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("the bar's button 3 is activity 0x27", kWashActivity == 0x27);

    { // stage 2: +100 hygiene (clamped) and the original colour comes back
        Player p;
        p.care[Cleanliness] = 20;
        p.baseColour = 0x1e1;
        p.appearance = 0x2a2; // a colour potion
        applyWash(p);
        check("hygiene is topped up to the cap", p.care[Cleanliness] == kMaxCare);
        check("the original colour is back", p.appearance == 0x1e1);
        check("the displayed palette follows", washedPalette(p, 0x2a2) == 0x1e1);
    }
    { // a wash is the same whatever the starting hygiene
        Player p;
        p.care[Cleanliness] = 0;
        p.baseColour = p.appearance = 0x1e1;
        applyWash(p);
        check("from 0 hygiene it is a full wash (100 clamps to the max)", p.care[Cleanliness] == kMaxCare);
        check("an unchanged colour stays", p.appearance == 0x1e1);
    }
    { // no recorded original colour: keep the current palette rather than switching to colour 0
        Player p;
        p.baseColour = p.appearance = 0;
        applyWash(p);
        check("no original colour recorded: keep the current palette", washedPalette(p, 0x1e3) == 0x1e3);
    }

    { // stage 3 mood top-up: happiness <= 0x4a (74) gets +10, above does not
        Player p;
        p.care[Happiness] = 50;
        applyWashMood(p);
        check("50 -> 60", p.care[Happiness] == 60);
        p.care[Happiness] = 0x4a;
        applyWashMood(p);
        check("exactly 0x4a still gets the bonus (jle)", p.care[Happiness] == 0x4a + 10);
        p.care[Happiness] = 0x4b;
        applyWashMood(p);
        check("0x4b gets nothing", p.care[Happiness] == 0x4b);
        p.care[Happiness] = 100;
        applyWashMood(p);
        check("a happy pet gets nothing", p.care[Happiness] == 100);
    }

    { // the bubble: anim 0xe at 14 fps, one-shot
        check("a 1 frame bubble lasts 3 ticks", bubbleTicks(1) == 3);
        check("a 7 frame bubble lasts 15 ticks (7 / 14 s)", bubbleTicks(7) == 15);
        check("no art: no wait", bubbleTicks(0) == 0);
        check("the bubble is done exactly when the frame number passes the last frame",
              bubbleTicks(7) * kWashBubbleFps / 30 >= 7 && (bubbleTicks(7) - 1) * kWashBubbleFps / 30 < 7);
    }

    { // stage 3: the happy reaction is intro? + hop x3 + outro?, driven like the encounter drives it
        std::vector<ReactAnim> plays;
        int left = 0;
        ActorIO io;
        io.play = [&](ReactAnim a, int)
        { plays.push_back(a); left = 4; return true; };
        io.busy = [&]
        { return left > 0; };
        io.sound = [](int, int) {};
        io.bubble = [](int) {};
        io.settle = [] { return true; };
        io.rand = [](int lo, int) { return lo; };
        ReactionSeq seq(ReactionSeq::Kind::Positive, kWashHappyRepeats, kWashHappyVariant);
        bool done = false;
        for (int t = 0; t < 400 && !done; ++t)
        {
            if (left > 0)
                --left;
            done = seq.tick(io);
        }
        int hops = 0;
        for (ReactAnim a : plays)
            if (a == ReactAnim::Positive)
                ++hops;
        check("the happy reaction finishes", done);
        check("it hops exactly 3 times", hops == 3);
        check("it ends with the outro", !plays.empty() && plays.back() == ReactAnim::Outro);
        check("it starts with the intro", !plays.empty() && plays.front() == ReactAnim::Intro);
    }

    { // a sitting pet gets up first, and the wash waits for that
        const int sp = 2;
        check("sitting is noticed", sitPhase(sp, kSettled[sp], true, false, false) == SitPhase::Sitting);
        check("the wash waits while getting up", sitPhase(sp, kSitAlt[sp], false, true, false) == SitPhase::GettingUp);
        check("then proceeds once standing", sitPhase(sp, kIdleBase[sp], true, false, false) == SitPhase::None);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
