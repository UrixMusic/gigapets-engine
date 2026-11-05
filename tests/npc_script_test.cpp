// npc_script_test.cpp - an NPC driven by a script (NpcActor::scriptWalkTo / scriptFace / scriptStop / scriptRelease): it walks to the goal with the
// pixel-exact wanderer's walk, stands still otherwise, and gives the roll of activities back on release. No ROM.
#include "../src/game/npc_actor.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                                       \
    do                                                                 \
    {                                                                  \
        if (!(c))                                                      \
        {                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);           \
            ++fails;                                                   \
        }                                                              \
    } while (0)

static gp::NpcDef def(bool stationary, int x, int y)
{
    gp::NpcDef d;
    d.id = "m:npc/t";
    d.stationary = stationary;
    d.x = x;
    d.y = y;
    d.area = 0;
    return d;
}

int main()
{
    using namespace gp;
    Rng rng;
    { // a stationary NPC does nothing by itself, and walks when a script says so (its own walking: right, then up/down, in 30 Hz steps)
        const NpcDef d = def(true, 100, 100);
        NpcActor a(d);
        for (int i = 0; i < 50; ++i)
            a.tick(rng);
        CHECK(a.x() == 100 && a.y() == 100 && !a.scripted && !a.walking());
        a.scriptWalkTo(160, 100);
        CHECK(a.scripted && a.scriptWalk);
        int ticks = 0;
        while (a.scriptWalk && ticks < 200)
        {
            a.tick(rng);
            ++ticks;
            if (a.scriptWalk)
                CHECK(a.walking() && a.animKey() == 3); // the sideways walk animation while it goes
        }
        CHECK(a.x() == 160 && a.y() == 100 && !a.scriptWalk && !a.walking() && ticks > 15 && ticks < 40); // 60 px at 3 px a tick
        CHECK(a.body.facing == FaceRight && a.scripted);
        for (int i = 0; i < 100; ++i)
            a.tick(rng);
        CHECK(a.x() == 160 && a.y() == 100 && !a.walking()); // it stands where it got to
        // down and left, a new goal while it walks
        a.scriptWalkTo(160, 180);
        for (int i = 0; i < 10; ++i)
            a.tick(rng);
        CHECK(a.body.facing == FaceDown && a.y() > 100 && a.y() < 180);
        a.scriptWalkTo(100, 120);
        int guard = 0;
        while (a.scriptWalk && guard++ < 400)
            a.tick(rng);
        CHECK(a.x() == 100 && a.y() == 120 && !a.scriptWalk);
        // face: turns and holds still; stop: stands
        a.scriptFace(FaceLeft);
        CHECK(a.body.facing == FaceLeft && !a.scriptWalk && a.scripted);
        a.scriptWalkTo(100, 200);
        for (int i = 0; i < 5; ++i)
            a.tick(rng);
        a.scriptStop();
        const int stopY = a.y();
        for (int i = 0; i < 20; ++i)
            a.tick(rng);
        CHECK(a.y() == stopY && !a.walking() && a.scripted);
        // a goal where it already is: no walk
        a.scriptWalkTo(a.x(), a.y());
        CHECK(!a.scriptWalk);
        // release: its own behaviour again (a stationary one does nothing, and is no longer scripted)
        a.scriptRelease(rng);
        CHECK(!a.scripted && !a.scriptWalk);
        const int rx = a.x(), ry = a.y();
        for (int i = 0; i < 50; ++i)
            a.tick(rng);
        CHECK(a.x() == rx && a.y() == ry);
    }
    { // a roaming NPC: while a script drives it the wandering stops; after the release the NPC's own roll of activities goes on
        NpcDef d = def(false, 200, 200);
        d.behaviorFlags = kNpcFollowsPath;
        d.pingPong = true;
        d.path = {{0, 0}, {60, 0}, {60, 40}};
        NpcActor a(d);
        for (int i = 0; i < 200; ++i)
            a.tick(rng);
        CHECK(!a.scripted);
        a.scriptWalkTo(a.x() + 30, a.y());
        const int goalX = a.x() + 30;
        int guard = 0;
        while (a.scriptWalk && guard++ < 300)
            a.tick(rng);
        CHECK(a.x() == goalX && !a.walking());
        const int x0 = a.x(), y0 = a.y();
        for (int i = 0; i < 300; ++i)
            a.tick(rng); // (its own roll would have sent it wandering by now)
        CHECK(a.x() == x0 && a.y() == y0);
        a.scriptRelease(rng);
        bool moved = false;
        for (int i = 0; i < 3000 && !moved; ++i)
        {
            a.tick(rng);
            moved = a.x() != x0 || a.y() != y0;
        }
        CHECK(moved); // it wanders again
    }
    { // an encounter holds an NPC: a script's walk waits
        const NpcDef d = def(true, 50, 50);
        NpcActor a(d);
        a.scriptWalkTo(90, 50);
        a.locked = true;
        for (int i = 0; i < 20; ++i)
            a.tick(rng);
        CHECK(a.x() == 50);
        a.locked = false;
        for (int i = 0; i < 40; ++i)
            a.tick(rng);
        CHECK(a.x() == 90);
    }
    std::printf(fails ? "npc_script_test: %d failure(s)\n" : "npc_script_test: all passed\n", fails);
    return fails ? 1 : 0;
}
