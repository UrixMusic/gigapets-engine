// tests/npc_face_pet_test.cpp - a roaming NPC stops and turns to face the pet while it is in talking range
// (TickAllPets, 0x03cc8e), then turns back to the front when the pet leaves.
#include <cstdio>
#include "../src/game/npc_actor.hpp"
using namespace gp;

static NpcDef roamer()
{
    NpcDef d;
    d.x = 100;
    d.y = 100;
    d.behaviorFlags = kNpcFollowsPath; // no mood bits: bit 0 would make it sick
    d.path = {{40, 0}, {0, 0}};
    return d;
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };
    Rng rng;
    rng.state = 777u;

    { // a walking NPC stops where it is, faces the pet on its right, and stays put while selected
        const NpcDef d = roamer();
        NpcActor n(d);
        n.activity = kActivityWander;
        n.activityTicks = 0;
        for (int i = 0; i < 5; ++i) // short of the 40 px waypoint, so it is still mid-walk
            n.tick(rng);
        check("it is walking before the pet arrives", n.walking() && n.x() != d.x);
        const int stopX = n.x(), stopY = n.y();
        n.selected = true;
        n.petOnRight = true;
        n.tick(rng);
        check("selected: stops walking", !n.walking() && n.activity == kActivityIdle);
        check("selected: faces right (pet is on its right)", n.body.facing == FaceRight);
        check("selected: idles sideways (anim key 7)", n.animKey() == 7 && n.settled());
        for (int i = 0; i < 300; ++i)
            n.tick(rng);
        check("stays exactly where it stopped for 10 s", n.x() == stopX && n.y() == stopY && n.animKey() == 7);

        n.petOnRight = false; // the pet walks around to the other side
        n.tick(rng);
        check("follows the pet to the other side", n.body.facing == FaceLeft && n.animKey() == 7);

        // the pet leaves: it turns back to face the front with the 90 degree turn, then carries on
        n.selected = false;
        n.tick(rng);
        check("released: starts the 90 degree turn (key 5)", n.animKey() == 5 && !n.facingPet);
        n.anim.flags |= 1; // the turn animation finishes
        n.tick(rng);
        check("released: ends facing the front, plain idle", n.body.facing == FaceDown && n.animKey() == 0);
        for (int i = 0; i < 2000; ++i)
            n.tick(rng);
        check("released: eventually wanders again", n.x() != stopX || n.y() != stopY);
    }

    { // an NPC idling after a waypoint stop (activity 9) turns too, without waiting out its idle timer
        const NpcDef d = roamer();
        NpcActor n(d);
        n.activity = kActivityTalkedTo;
        n.activityTicks = 90;
        n.selected = true;
        n.petOnRight = true;
        n.tick(rng);
        check("idle after a stop turns at once", n.facingPet && n.body.facing == FaceRight && n.animKey() == 7);
    }

    { // a left-facing pet side
        const NpcDef d = roamer();
        NpcActor n(d);
        n.selected = true;
        n.petOnRight = false;
        n.tick(rng);
        check("pet on its left: faces left", n.body.facing == FaceLeft && n.animKey() == 7);
    }

    { // an idle NPC turns too; a locked one (an encounter has it) is left alone
        const NpcDef d = roamer();
        NpcActor n(d);
        n.locked = true;
        n.selected = true;
        n.petOnRight = true;
        const int face = n.body.facing;
        n.tick(rng);
        check("locked NPC ignores the selection", !n.facingPet && n.body.facing == face);
    }

    { // an NPC in the middle of a turn finishes it before facing the pet
        const NpcDef d = roamer();
        NpcActor n(d);
        n.settle = NpcActor::SettleTurn90;
        n.body.facing = FaceRight;
        n.selected = true;
        n.petOnRight = false;
        n.tick(rng);
        check("a turn in progress is not interrupted", n.settle == NpcActor::SettleTurn90 && !n.facingPet);
        n.anim.flags |= 1;
        n.tick(rng); // the turn ends facing down
        n.tick(rng); // now it faces the pet
        check("then it faces the pet", n.facingPet && n.body.facing == FaceLeft);
    }

    { // stationary NPCs never turn
        NpcDef d = roamer();
        d.stationary = true;
        NpcActor n(d);
        n.selected = true;
        n.tick(rng);
        check("stationary NPCs are untouched", !n.facingPet);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
