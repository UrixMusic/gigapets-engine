// tests/pet_sit_test.cpp - a sitting pet gets up (activity 8) before it moves; it can't be controlled while sitting down
// or getting up.
#include <cstdio>
#include "../src/game/pet_sit.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    for (int sp = 0; sp < 3; ++sp)
    {
        std::printf("-- species %d (sit down %d, sitting %d, get up %d)\n", sp, kSitDown[sp], kSettled[sp], kSitAlt[sp]);
        check("standing idle is not sitting", sitPhase(sp, kIdleBase[sp], true, false, false) == SitPhase::None);
        check("walking is not sitting", sitPhase(sp, kWalkDown[sp], false, false, false) == SitPhase::None);
        check("the sit-down animation is SittingDown", sitPhase(sp, kSitDown[sp], false, true, false) == SitPhase::SittingDown);
        check("the sitting idle is Sitting", sitPhase(sp, kSettled[sp], true, false, false) == SitPhase::Sitting);
        check("the get-up animation is GettingUp", sitPhase(sp, kSitAlt[sp], false, true, false) == SitPhase::GettingUp);
        check("a finished get-up is no longer GettingUp", sitPhase(sp, kSitAlt[sp], false, true, true) == SitPhase::None);
        check("sitting idle in a one-shot pose is not Sitting", sitPhase(sp, kSettled[sp], false, true, false) == SitPhase::None);
    }

    check("get-up ids match the ROM table (0x31, 0x82, 0xea)", kSitAlt[0] == 0x31 && kSitAlt[1] == 0x82 && kSitAlt[2] == 0xea);
    check("sit-down ids match the ROM table (0x2e, 0x9a, 0x104)", kSitDown[0] == 0x2e && kSitDown[1] == 0x9a && kSitDown[2] == 0x104);

    check("sitting + direction: get up", startsGetUp(SitPhase::Sitting, true, false));
    check("sitting + action button: get up (the press is swallowed)", startsGetUp(SitPhase::Sitting, false, true));
    check("sitting, no input: stay sitting", !startsGetUp(SitPhase::Sitting, false, false));
    check("standing + direction: just move", !startsGetUp(SitPhase::None, true, false));
    check("already getting up: don't restart", !startsGetUp(SitPhase::GettingUp, true, true));
    check("sitting down: don't get up yet", !startsGetUp(SitPhase::SittingDown, true, true));

    check("input is blocked while sitting down and while getting up",
          blocksInput(SitPhase::SittingDown) && blocksInput(SitPhase::GettingUp));
    check("input is accepted while standing or sitting", !blocksInput(SitPhase::None) && !blocksInput(SitPhase::Sitting));

    { // a held direction, frame by frame: sit, press, get up (no movement), then walk
        const int sp = 1;
        int animId = kSettled[sp];
        bool idle = true, special = false, done = false;
        int movedFrames = 0, blockedFrames = 0;
        const bool held = true;
        bool getUpStarted = false;
        for (int frame = 0; frame < 20; ++frame)
        {
            if (startsGetUp(sitPhase(sp, animId, idle, special, done), held, false))
            {
                animId = kSitAlt[sp]; // playAnim(kSitAlt, one-shot, Special)
                idle = false;
                special = true;
                done = false;
                getUpStarted = true;
            }
            const bool locked = blocksInput(sitPhase(sp, animId, idle, special, done));
            if (locked)
                ++blockedFrames;
            else
                ++movedFrames;
            if (frame == 6 && special) // the get-up animation ends and the pet drops back to the standing idle
            {
                done = true;
                animId = kIdleBase[sp];
                idle = true;
                special = false;
            }
        }
        check("the held direction starts the get-up on the first frame", getUpStarted);
        check("no movement while getting up (frames 0..6)", blockedFrames == 7);
        check("movement resumes straight after, with the key still held", movedFrames == 13);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
