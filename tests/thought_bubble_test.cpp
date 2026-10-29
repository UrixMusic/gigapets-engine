// tests/thought_bubble_test.cpp - the hungry and tired bubbles over the pet (game/thought_bubble.hpp).
#include <cstdio>
#include "../src/game/thought_bubble.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("59.94 Hz ticks to 30 Hz steps: 1500 -> 751 (25 s), 7200 -> 3604", romToSim(1500) == 751 && romToSim(7200) == 3604);
    check("hunger limit: <=9 2 min, <=0x13 4, <=0x1d 8, <=0x27 16, else 32", hungerInterval(9) == romToSim(7200) && hungerInterval(0x13) == romToSim(14400) &&
                                                                              hungerInterval(0x1d) == romToSim(28800) && hungerInterval(0x27) == romToSim(57600) &&
                                                                              hungerInterval(0x28) == romToSim(115200));

    // ---- hunger ----
    {
        ThoughtTimer t;
        bool spawned = false;
        long steps = 0;
        while (!spawned && steps < 5000)
        {
            spawned = hungerBubbleStep(t, 0x20, false, false);
            ++steps;
        }
        check("a hungry pet gets its bubble 25 s after the start", spawned && steps == kThoughtRetryTicks);
        check("then the limit follows how hungry (0x20: 16 min) and the count restarts", t.accum == 0 && t.limit == hungerInterval(0x20));
    }
    {
        ThoughtTimer t;
        bool any = false;
        for (int i = 0; i < 2000; ++i)
            any = any || hungerBubbleStep(t, 0x29, false, false);
        check("a pet that is not hungry enough (0x29) gets none, and waits 32 min", !any && t.limit == hungerInterval(0x29));
    }
    {
        ThoughtTimer t;
        bool any = false;
        for (int i = 0; i < kThoughtRetryTicks; ++i)
            any = any || hungerBubbleStep(t, 0x10, true, false);
        check("a walking pet is asked again in 25 s, with the limit unchanged", !any && t.limit == 0 && t.accum == -kThoughtRetryTicks);
    }
    {
        ThoughtTimer t;
        t.accum = -1;
        const bool s = hungerBubbleStep(t, 0x10, false, true);
        check("a bubble already up: none, but the timer still moves on", !s && t.limit == hungerInterval(0x10) && t.accum == 0);
    }

    // ---- tired ----
    {
        ThoughtTimer t;
        t.accum = -1;
        check("a tired pet (0x19) at rest gets the ZZZ bubble", tiredBubbleStep(t, 0x19, false, false) && t.accum == 0);
        t.accum = -1;
        check("not tired enough (0x1a): none", !tiredBubbleStep(t, 0x1a, false, false) && t.accum == 0);
        t.accum = -1;
        check("walking: retry in 25 s", !tiredBubbleStep(t, 0x05, true, false) && t.accum == -kThoughtRetryTicks);
        t.accum = -1;
        check("a bubble already up: none and the count is kept", !tiredBubbleStep(t, 0x05, false, true) && t.accum == 0 - 0 + 0);
    }

    // ---- the bubble's life: 0x18 grows (4 frames at 7 fps), 0x17 loops (3 frames), 5 s, 0x18 backwards ----
    {
        const int grow = 4, hold = 3;
        const int gs = thoughtStepsFor(grow); // 18 steps
        ThoughtView v = thoughtView(0, grow, hold);
        check("it starts on the first frame of the grow anim, no icon", v.anim == 0x18 && v.frame == 0 && !v.icon && !v.done);
        v = thoughtView(gs - 1, grow, hold);
        check("the last grow frame is the fourth", v.anim == 0x18 && v.frame == grow - 1);
        v = thoughtView(gs, grow, hold);
        check("then the loop anim with the icon in it", v.anim == 0x17 && v.icon && v.frame == 0);
        v = thoughtView(gs + 5, grow, hold);
        check("the loop wraps round its frames", v.anim == 0x17 && v.frame == (5 * 7 / 30) % hold);
        v = thoughtView(kThoughtShowTicks - 1, grow, hold);
        check("it holds until 5 s", v.anim == 0x17 && v.icon);
        v = thoughtView(kThoughtShowTicks, grow, hold);
        check("then the grow anim runs backwards from its last frame, icon gone", v.anim == 0x18 && v.frame == grow - 1 && !v.icon);
        v = thoughtView(kThoughtShowTicks + gs - 1, grow, hold);
        check("ending on the first frame", v.anim == 0x18 && v.frame == 0 && !v.done);
        v = thoughtView(kThoughtShowTicks + gs, grow, hold);
        check("and it is gone", v.done);
    }

    // ---- the tutorial after it ----
    check("hungry bubble gone, pet resting, first time: event 13", thoughtTutorial(true, 0, kThoughtHungerIcon, true) == 13);
    check("tired bubble: event 14", thoughtTutorial(true, 0, kThoughtTiredIcon, true) == 14);
    check("pet not resting: none (and it won't come back)", thoughtTutorial(true, 0, kThoughtHungerIcon, false) == -1);
    check("already seen: none", thoughtTutorial(true, markShown(0, 13), kThoughtHungerIcon, true) == -1 && thoughtTutorial(true, markShown(0, 14), kThoughtTiredIcon, true) == -1);
    check("tutorials off: none", thoughtTutorial(false, 0, kThoughtTiredIcon, true) == -1);
    check("another icon (sick, 0x140): none", thoughtTutorial(true, 0, 0x140, true) == -1);

    // ---- the stats wearing down ----
    check("limits: tiredness 20000 ROM ticks, hunger 4000", kTiredDecayLimit == romToSim(20000) && kHungerDecayLimit == romToSim(4000));
    check("amounts: sleepiness -2 for species 0 else -1; hunger -2 for species 2 else -1",
          tiredDecayAmount(0) == 2 && tiredDecayAmount(1) == 1 && tiredDecayAmount(2) == 1 &&
              hungerDecayAmount(2) == 2 && hungerDecayAmount(0) == 1 && hungerDecayAmount(1) == 1);
    {
        long a = 0;
        int drops = 0;
        for (long i = 0; i < kHungerDecayLimit * 3; ++i)
            drops += decayStep(a, kHungerDecayLimit, 4);
        check("one drop per limit once past 3 quests", drops == 3);
        a = 0;
        drops = 0;
        for (long i = 0; i < kHungerDecayLimit * 2; ++i)
            drops += decayStep(a, kHungerDecayLimit, 3);
        check("none with 3 quests or fewer, but the count still restarts", drops == 0 && a < kHungerDecayLimit);
    }
    check("crossing into hungry: 0x29 -> 0x28 yes, 0x28 -> 0x27 and 0x30 -> 0x29 no", crossedHungry(0x29, 0x28) && !crossedHungry(0x28, 0x27) && !crossedHungry(0x30, 0x29));

    // ---- where bubbles hang ----
    check("thought bubble, dog: right +0x25, left 0, up/down +0x14, all -0x32",
          thoughtOffset(0, 1, 0).dx == 0x25 && thoughtOffset(0, 0, 0).dx == 0 && thoughtOffset(0, 2, 0).dx == 0x14 && thoughtOffset(0, 2, 0).dy == -0x32);
    check("thought bubble, monkey and panda: right +0x16, left +0xf, other +0x14, -0x46",
          thoughtOffset(1, 1, 0).dx == 0x16 && thoughtOffset(2, 0, 0).dx == 0xf && thoughtOffset(1, 2, 0).dx == 0x14 && thoughtOffset(1, 2, 0).dy == -0x46);
    check("the panda's raised art lifts it 0x14 more; the monkey's never", thoughtOffset(2, 2, 0xf0).dy == -0x5a && thoughtOffset(1, 2, 0xf0).dy == -0x46 && !pandaRaised(0xeb));
    check("overhead loop: +-0x12 or 0, dog -0x32, others -0x4b (panda raised -0x5f)",
          overheadOffset(0, 1, 0).dx == 0x12 && overheadOffset(1, 0, 0).dx == -0x12 && overheadOffset(2, 2, 0).dx == 0 &&
              overheadOffset(0, 2, 0).dy == -0x32 && overheadOffset(1, 2, 0).dy == -0x4b && overheadOffset(2, 2, 0xe4).dy == -0x5f);
    check("the stink cloud is anim 6 at 14 fps below 0x1a", kStinkAnim == 6 && kStinkFps == 14 && kStinkBelow == 0x1a);

    check("a balanced pet has no personality bubble", personalityBubble(0, 0, 0) == -1 && personalityBubble(300, 1, 299) == -1);
    check("halos more than 299 above the pitchforks: bubble 7", personalityBubble(400, 100, 0) == 7 && personalityBubble(300, 0, 5000) == 7);
    check("pitchforks more than 299 above the halos: bubble 0xd", personalityBubble(0, 300, 0) == 0xd);
    check("exactly 299 apart is not enough", personalityBubble(299, 0, 0) == -1 && personalityBubble(0, 299, 0) == -1);
    check("a balanced pet with more than 299 stars: bubble 0xf", personalityBubble(50, 50, 300) == 0xf && personalityBubble(50, 50, 299) == -1);

    std::printf(fails ? "thought_bubble_test: %d failure(s)\n" : "thought_bubble_test: all passed\n", fails);
    return fails ? 1 : 0;
}
