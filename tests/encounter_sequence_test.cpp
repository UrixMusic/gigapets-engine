// Runs the encounter state machine against a fake host and checks the visited steps against the original's
// g_petSequenceStep numbers (measured in MAME: 2 3 6 7 8 9 10 11 12 20 21 22 23 for an angry encounter).
#include "../src/game/encounter_sequence.hpp"
#include <cstdio>

using gp::EncounterSequence;

struct Fake
{
    int petX = 300, npcX = 200, npcY = 100;
    bool wandering = false;   // npcLock() reports a walking NPC (adds step 4)
    bool neverArrive = false; // the pet cannot reach its target (adds the give-up timeout)
    int friends = 0, calls = 0;
};

static gp::EncounterHost makeHost(Fake &f)
{
    gp::EncounterHost h;
    h.npcLock = [&]
    { return f.wandering; };
    h.petX = [&]
    { return f.petX; };
    h.npcX = [&]
    { return f.npcX; };
    h.npcY = [&]
    { return f.npcY; };
    h.petWalkToX = [&](int)
    { return !f.neverArrive && ++f.calls % 3 == 0; };
    h.petWalkToY = [&](int)
    { return !f.neverArrive && ++f.calls % 3 == 0; };
    h.petFace = [](bool) {};
    h.petNoise = [] {};
    h.petSad = [] {};
    h.petReact = [](int) {};
    h.npcSettled = []
    { return true; };
    h.npcUnlock = [] {};
    h.npcNoise = [] {};
    h.npcReact = [](int) {};
    h.markFriended = [&]
    { return ++f.friends; };
    h.event = [](const char *, int) {};
    return h;
}

struct Case
{
    const char *name;
    int pet, npc;
    bool wandering;
    EncounterSequence::Outcome outcome;
    std::vector<int> steps;
};

int main()
{
    using O = EncounterSequence::Outcome;
    const Case cases[] = {
        {"friend", -39, -35, false, O::Friend, {2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
        {"zero-delta npc is a friend", -39, 0, false, O::Friend, {2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
        {"angry (MAME: 2 3 6 7 8 9 10 11 12 20 21 22 23)", -39, 14, false, O::Negative, {2, 3, 6, 7, 8, 9, 10, 11, 12, 20, 21, 22, 23}},
        {"bored npc", -39, -90, false, O::Neutral, {2, 3, 6, 7, 8, 9, 10, 11, 12, 14, 15}},
        {"wandering npc settles first", -39, -35, true, O::Friend, {2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
    };
    int fail = 0;
    for (const Case &c : cases)
    {
        Fake f;
        f.wandering = c.wandering;
        gp::EncounterHost h = makeHost(f);
        EncounterSequence s(c.pet, c.npc, false);
        for (int i = 0; i < 5000 && !s.done(); ++i)
            s.tick(h);
        const bool ok = s.done() && s.outcome() == c.outcome && s.history == c.steps;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", c.name);
        fail += !ok;
    }
    { // the pet cannot get there: it gives up after kGiveUpTicks and carries on
        Fake f;
        f.neverArrive = true;
        gp::EncounterHost h = makeHost(f);
        EncounterSequence s(-39, -35, false);
        int ticksTo9 = -1;
        for (int i = 1; i < 5000 && !s.done(); ++i)
        {
            s.tick(h);
            if (ticksTo9 < 0 && s.step() == 9)
                ticksTo9 = i;
        }
        const bool ok = s.done() && ticksTo9 >= EncounterSequence::kGiveUpTicks && ticksTo9 <= EncounterSequence::kGiveUpTicks + 6;
        std::printf("%s give-up (reached step 9 after %d ticks)\n", ok ? "ok  " : "FAIL", ticksTo9);
        fail += !ok;
    }
    {
        const bool ok = EncounterSequence::milestoneEvent(10) == 0xc4 && EncounterSequence::milestoneEvent(35) == 0x5d &&
                        EncounterSequence::milestoneEvent(11) == 0;
        std::printf("%s milestones\n", ok ? "ok  " : "FAIL");
        fail += !ok;
    }
    std::printf(fail ? "encounter_sequence_test: %d FAILED\n" : "encounter_sequence_test: PASS\n", fail);
    return fail != 0;
}