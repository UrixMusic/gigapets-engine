#include "../src/game/encounter.hpp"
#include <cstdio>

struct Case { const char *name; int pet, npc, player, animal; bool friends; };

int main()
{
    const Case cases[] = {
        {"2h/37p",  -39, -35, 1, 2, true},   // measured in MAME
        {"15h/1p",  -39,  14, 5, 5, false},  // measured
        {"27h/28p", -39,   0, 6, 1, true},   // measured (npc delta in the dead zone)
        {"1h/91p",  -39, -90, 2, 6, false},  // measured
    };
    int fail = 0;
    for (const Case &c : cases)
    {
        const int p = gp::classifyReaction(c.pet, c.npc);
        const int a = gp::classifyCompat(c.pet, c.npc);
        const bool f = gp::encounterBefriends(c.pet, c.npc);
        const bool ok = p == c.player && a == c.animal && f == c.friends;
        std::printf("%s %s: player %d animal %d friend %d (want %d %d %d)\n",
                    ok ? "ok  " : "FAIL", c.name, p, a, f, c.player, c.animal, c.friends);
        fail += !ok;
    }
    std::printf(fail ? "encounter_test: %d FAILED\n" : "encounter_test: PASS\n", fail);
    return fail != 0;
}