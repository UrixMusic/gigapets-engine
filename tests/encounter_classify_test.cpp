// encounter_classify_test.cpp - the host's classify hook can change the reaction codes of an encounter, and with them its outcome.
#include <cstdio>
#include <functional>
#include "../src/game/encounter_sequence.hpp"
using namespace gp;

static EncounterSequence::Outcome run(int petDelta, int npcDelta, std::function<void(int, int, int &, int &)> classify, int *calls = nullptr)
{
    EncounterHost h;
    h.petX = [] { return 300; };
    h.npcX = [] { return 100; };
    h.npcY = [] { return 200; };
    h.petY = [] { return 200; };
    h.spawnItem = [](int, int, int) {};
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
    h.markFriended = [] { return 3; };
    h.event = [](const char *, int) {};
    if (classify)
        h.classify = [&, calls](int p, int n, int &pc, int &ac)
        {
            if (calls)
                ++*calls;
            classify(p, n, pc, ac);
        };
    for (ActorIO *a : {&h.pet, &h.npc})
    {
        a->play = [](ReactAnim, int) { return false; };
        a->busy = [] { return false; };
        a->sound = [](int, int) {};
        a->bubble = [](int) {};
        a->settle = [] { return true; };
        a->rand = [](int lo, int) { return lo; };
    }
    EncounterSequence seq(petDelta, npcDelta, false);
    for (int t = 0; t < 5000 && !seq.done(); ++t)
        seq.tick(h);
    return seq.outcome();
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };
    using O = EncounterSequence::Outcome;
    check("no hook: equal personalities become friends", run(40, 40, nullptr) == O::Friend);
    check("no hook: opposite signs are hostile", run(40, -40, nullptr) == O::Negative);
    int calls = 0;
    check("a hook that changes nothing changes nothing", run(40, 40, [](int, int, int &, int &) {}, &calls) == O::Friend && calls == 1); // asked once
    check("hostile codes make a friendly pair hostile", run(40, 40, [](int, int, int &pc, int &ac) { pc = 4; ac = 4; }) == O::Negative);
    check("friendly codes make a hostile pair friends", run(40, -40, [](int, int, int &pc, int &ac) { pc = 1; ac = 1; }) == O::Friend);
    check("a blocking player code stops the friendship", run(40, 40, [](int, int, int &pc, int &) { pc = 4; }) == O::Neutral);
    check("a hook sees the deltas", [&]
          { int sp = 0, sn = 0; run(12, -7, [&](int p, int n, int &, int &) { sp = p; sn = n; }); return sp == 12 && sn == -7; }());
    std::printf(fails ? "encounter_classify_test: %d failure(s)\n" : "encounter_classify_test: all passed\n", fails);
    return fails ? 1 : 0;
}
