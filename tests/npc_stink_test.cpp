// tests/npc_stink_test.cpp - stink spreading between the pet and the NPC beside it (game/npc_stink.hpp).
#include <cstdio>
#include "../src/game/npc_stink.hpp"
using namespace gp;

// Runs `checks` whole checks (each kStinkCheckTicks steps) and returns the first non-None event, or None.
static StinkEvent run(StinkTransfer &s, int checks, int quests, int hygiene, int npc, bool npcStinky)
{
    for (int i = 0; i < checks * kStinkCheckTicks; ++i)
    {
        const StinkEvent e = stinkTransferStep(s, quests, hygiene, npc, npcStinky);
        if (e != StinkEvent::None)
            return e;
    }
    return StinkEvent::None;
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("a check every 45 ROM ticks is 23 steps; 45 counts to pay off", kStinkCheckTicks == 23 && kStinkCountNeeded == 45);
    {
        StinkTransfer s;
        check("a stinky pet beside a clean NPC: after 44 checks nothing, the 45th makes the NPC stink",
              run(s, 44, 10, 0x10, 7, false) == StinkEvent::None && run(s, 1, 10, 0x10, 7, false) == StinkEvent::NpcStinks && s.count == 0);
    }
    {
        StinkTransfer s;
        check("a clean pet beside a stinky NPC: the 45th check drops the pet's Cleanliness",
              run(s, 45, 10, 0x50, 7, true) == StinkEvent::PetStinks);
    }
    {
        StinkTransfer s;
        check("a clean pet beside a clean NPC counts nothing", run(s, 200, 10, 0x50, 7, false) == StinkEvent::None && s.count == 0);
        check("nobody beside it counts nothing", run(s, 200, 10, 0x10, -1, false) == StinkEvent::None && s.count == 0);
    }
    {
        StinkTransfer s;
        check("a stinky pet beside a stinky NPC has nothing to spread", run(s, 200, 10, 0x10, 7, true) == StinkEvent::None && s.count == 0);
    }
    {
        StinkTransfer s;
        check("3 quests or fewer: never", run(s, 200, 3, 0x10, 7, false) == StinkEvent::None && s.count == 0);
    }
    {
        StinkTransfer s;
        run(s, 20, 10, 0x10, 7, false);
        check("20 checks beside one NPC is a count of 20", s.count == 20);
        run(s, 5, 10, 0x50, -1, false);
        check("walking away does not clear it", s.count == 20);
        run(s, 1, 10, 0x10, 9, false);
        check("but a different NPC, with the pet stinky, restarts it at 1", s.count == 1 && s.lastNpc == 9);
    }

    std::printf(fails ? "npc_stink_test: %d failure(s)\n" : "npc_stink_test: all passed\n", fails);
    return fails ? 1 : 0;
}
