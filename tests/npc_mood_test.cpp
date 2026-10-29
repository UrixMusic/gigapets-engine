// tests/npc_mood_test.cpp - roaming NPCs' sick, sad and stinky states (game/npc_actor.hpp).
#include <cstdio>
#include "../src/game/npc_actor.hpp"
using namespace gp;

static NpcDef def(int flags, bool stationary = false)
{
    NpcDef d;
    d.x = 100;
    d.y = 100;
    d.behaviorFlags = flags;
    d.stationary = stationary;
    return d;
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    {
        const NpcDef d = def(0);
        NpcActor n(d);
        check("a plain NPC: idle key 0, not sick, not sad, not stinky", n.animKey() == 0 && !n.sick() && !n.sad() && !n.stinky);
    }
    {
        const NpcDef d = def(kNpcSick | kNpcFollowsPath);
        NpcActor n(d);
        check("flag bit 0: the sick idle (key 8)", n.sick() && n.animKey() == 8);
    }
    {
        const NpcDef d = def(kNpcSad | kNpcFollowsPath);
        NpcActor n(d);
        check("flag bit 1: the sad idle (key 9)", n.sad() && n.animKey() == 9);
    }
    {
        const NpcDef d = def(kNpcSick | kNpcSad);
        NpcActor n(d);
        check("both: sick wins, as in UpdateNPCIdleActivity", n.animKey() == 8);
    }
    {
        const NpcDef d = def(kNpcStinky | kNpcFollowsPath);
        NpcActor n(d);
        check("flag 0x100: stinky, and the idle is the plain one", n.stinky && n.animKey() == 0);
    }
    {
        const NpcDef d = def(0);
        NpcActor n(d);
        n.questSick = true;
        check("a sick-NPC quest makes it sick; when it ends, it is well again", n.animKey() == 8);
        n.questSick = false;
        check("...", n.animKey() == 0);
    }
    {
        const NpcDef d = def(0xffff, true);
        NpcActor n(d);
        check("a stationary NPC's flags are a bubble id, never moods", !n.sick() && !n.sad() && !n.stinky);
    }
    {
        const NpcDef d = def(kNpcSick | kNpcFollowsPath);
        d.path.size();
        NpcDef e = d;
        e.path = {{40, 0}, {0, 0}};
        NpcActor n(e);
        Rng rng;
        n.selected = true;
        n.petOnRight = true;
        n.tick(rng);
        check("a sick NPC the pet is talking to stays on the sick idle (no side turn)", n.animKey() == 8 && !n.facingPet);
    }

    std::printf(fails ? "npc_mood_test: %d failure(s)\n" : "npc_mood_test: all passed\n", fails);
    return fails ? 1 : 0;
}
