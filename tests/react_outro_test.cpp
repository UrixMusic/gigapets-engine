// tests/react_outro_test.cpp - an NPC's reaction outro plays last frame to first, and nothing else changes.
#include <cstdio>
#include <vector>
#include "../src/game/npc_actor.hpp"
using namespace gp;

// Drive one forced animation the way walk.cpp's updateNpcAnim does (setAnim, then advanceAnim, every tick) and
// record the frame shown on each tick until it finishes.
static std::vector<int> frames(NpcActor &n, int count, int maxTicks = 100)
{
    std::vector<int> seen;
    for (int t = 0; t < maxTicks; ++t)
    {
        n.setAnim(6, count, n.forcedRate, false, n.forcedReverse);
        n.advanceAnim(count);
        seen.push_back(n.anim.currentFrame);
        if (!n.animBusy())
            break;
    }
    return seen;
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };
    NpcDef def;

    { // the panda's 2-frame outro (0x101): tall frame first, then the short one, then done
        NpcActor n(def);
        n.forceAnim("x");
        n.forcedReverse = true;
        const std::vector<int> f = frames(n, 2);
        std::printf("      reverse, 2 frames:");
        for (int v : f)
            std::printf(" %d", v);
        std::printf("\n");
        check("reverse starts on the LAST frame (no flash of the first)", !f.empty() && f.front() == 1);
        check("reverse ends on the FIRST frame", f.back() == 0);
        check("reverse never goes back up (1.. then 0..)", [&]
              { bool seen0 = false; for (int v : f) { if (v == 0) seen0 = true; else if (seen0) return false; } return true; }());
    }
    { // forward (the intro) still goes first to last
        NpcActor n(def);
        n.forceAnim("x");
        const std::vector<int> f = frames(n, 2);
        check("forward starts on the first frame", !f.empty() && f.front() == 0);
        check("forward ends on the last frame", f.back() == 1);
    }
    { // a 7-frame hop is unaffected by the new flag, and a longer reverse plays every frame in order
        NpcActor n(def);
        n.forceAnim("x");
        n.forcedReverse = true;
        const std::vector<int> f = frames(n, 7);
        check("7-frame reverse steps 6,5,4,3,2,1,0", [&]
              { int want = 6; for (int v : f) { if (v > want) return false; if (v < want) want = v; } return f.front() == 6 && f.back() == 0; }());
    }
    { // forceAnim / clearForced reset the flag, so the next reaction is forward again
        NpcActor n(def);
        n.forcedReverse = true;
        n.forceAnim("x");
        check("forceAnim resets forcedReverse", !n.forcedReverse);
        n.forcedReverse = true;
        n.clearForced();
        check("clearForced resets forcedReverse", !n.forcedReverse);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
