// tests/befriend_drop_test.cpp - a newly befriended NPC drops item 0xa3 (163) where the pet is about to stand, and every
// 10th / 20th / 30th / 35th / 40th friend adds a milestone item (UpdateNPCEncounter step 0xc, SpawnPendingEvent 0x03f792).
#include <cstdio>
#include <vector>
#include "../src/game/encounter_sequence.hpp"
using namespace gp;

struct Drop
{
    int item, x, y;
};

// Run a friendly encounter to the end. petX/npcX place the two sides; `friends` is what markFriended reports.
static EncounterSequence::Outcome run(int petX, int petY, int npcX, int friends, std::vector<Drop> &drops, bool withSpawn = true)
{
    EncounterHost h;
    h.petX = [&] { return petX; };
    h.npcX = [&] { return npcX; };
    h.npcY = [&] { return petY; };
    h.petY = [&] { return petY; };
    if (withSpawn)
        h.spawnItem = [&](int item, int x, int y) { drops.push_back({item, x, y}); };
    h.petWalkToX = [](int) { return true; };
    h.petWalkToY = [](int) { return true; };
    h.petFace = [](bool) {};
    h.petEngage = [] {};
    h.petIdle = [] {};
    h.npcIdle = [] {};
    h.npcFacePet = [] {};
    h.npcSettled = [] { return true; };
    h.npcLock = [] { return false; };
    h.npcUnlock = [] {};
    h.npcNoise = [] {};
    h.npcReact = [](int) {};
    h.markFriended = [&] { return friends; };
    h.event = [](const char *, int) {};
    for (ActorIO *a : {&h.pet, &h.npc})
    {
        a->play = [](ReactAnim, int) { return false; }; // no art: every animation is skipped
        a->busy = [] { return false; };
        a->sound = [](int, int) {};
        a->bubble = [](int) {};
        a->settle = [] { return true; };
        a->rand = [](int lo, int) { return lo; };
    }
    EncounterSequence seq(40, 40, false); // same-sided, equal personalities: the NPC likes the pet
    for (int t = 0; t < 5000 && !seq.done(); ++t)
        seq.tick(h);
    return seq.outcome();
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    { // NPC on the left of the pet: the pet walks 20 px closer, i.e. to petX - 0x14
        std::vector<Drop> d;
        const auto out = run(300, 200, 100, 3, d);
        check("it is a friendly encounter", out == EncounterSequence::Outcome::Friend);
        check("exactly one drop for an ordinary friend", d.size() == 1);
        check("the drop is item 0xa3 (163)", !d.empty() && d[0].item == 0xa3);
        check("it lands where the pet will stand: petX - 0x14", !d.empty() && d[0].x == 300 - 0x14);
        check("and 10 px below the pet", !d.empty() && d[0].y == 200 + 10);
    }
    { // NPC on the right: the pet walks right
        std::vector<Drop> d;
        run(100, 150, 300, 3, d);
        check("NPC on the right: the drop is at petX + 0x14", !d.empty() && d[0].x == 100 + 0x14 && d[0].y == 160);
    }
    { // milestones
        struct M
        {
            int friends, item;
        };
        // From the listing at 0x018917: 0xc4 / 0x10 / 0xd / 0x11, and 0x5d only for exactly 35 friends (so 40 gets 0x11).
        for (const M &m : {M{10, 0xc4}, M{20, 0x10}, M{30, 0xd}, M{35, 0x5d}, M{40, 0x11}})
        {
            std::vector<Drop> d;
            run(300, 200, 100, m.friends, d);
            char what[96];
            std::snprintf(what, sizeof what, "friend #%d also drops milestone item 0x%x, 20 px out and 20 px down", m.friends, m.item);
            check(what, d.size() == 2 && d[0].item == 0xa3 && d[1].item == m.item && d[1].x == 300 - 0x14 + 0x14 && d[1].y == 200 + 0x14);
        }
    }
    { // other counts have no milestone
        for (int friends : {1, 9, 11, 19, 36, 41})
        {
            std::vector<Drop> d;
            run(300, 200, 100, friends, d);
            char what[64];
            std::snprintf(what, sizeof what, "friend #%d: just the 0xa3 drop", friends);
            check(what, d.size() == 1 && d[0].item == 0xa3);
        }
    }
    { // a host with no spawn hook (an old test host) still finishes
        std::vector<Drop> d;
        const auto out = run(300, 200, 100, 10, d, false);
        check("without a spawn hook the encounter still completes", out == EncounterSequence::Outcome::Friend && d.empty());
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
