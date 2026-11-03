// g++ -std=c++17 -Wall -Wextra npc_spawn_test.cpp -o npc_spawn_test && ./npc_spawn_test
#include "npc_spawn.hpp"
#include <cstdio>
using namespace gp;

static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-50s %s\n", n, ok ? "ok" : "FAIL"); if (!ok) ++fails; }

int main() {
    NpcSpawnInfo a; a.areaId = 3; a.spawnX = 100; a.spawnY = 200; a.behaviorFlags = 0x810; a.speciesId = 1;
    NpcInstanceInit n = initNpcInstance(a, 2);
    check("start position and facing down", n.x == 100 && n.y == 200 && n.facing == 3 && n.style == 0);
    check("flags get 0x4000, wander starts forward at 0", n.flags == (0x810 | 0x4000) && n.wander.forward && n.wander.index == 0);
    check("no idle bits -> activity 0x19", n.pendingActivity == 0x19);
    a.behaviorFlags = 0x11; check("flag bit 1 -> 0x1b", initNpcInstance(a, 0).pendingActivity == 0x1b);
    a.behaviorFlags = 0x12; check("flag bit 2 -> 0x1d", initNpcInstance(a, 0).pendingActivity == 0x1d);
    a.behaviorFlags = 0x13; check("bit 1 wins over bit 2", initNpcInstance(a, 0).pendingActivity == 0x1b);
    a.speciesId = -2; check("species -2 = player's species", initNpcInstance(a, 2).kind == 2);
    NpcSpawnInfo s; s.stationary = true; s.spawnX = 5; s.spawnY = 6; s.speciesId = 0x77;
    NpcInstanceInit si = initNpcInstance(s, 0);
    check("stationary: flags 0, idle 0x19, no wander", si.flags == 0 && si.pendingActivity == 0x19 && !si.wander.forward && si.stationary);
    return fails ? 1 : 0;
}
