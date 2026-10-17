// g++ -std=c++17 -Wall -Wextra npc_walk_test.cpp -o npc_walk_test && ./npc_walk_test
#include "npc_walk.hpp"
#include <cstdio>
using namespace gp;

static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-44s %s\n", n, ok ? "ok" : "FAIL"); if (!ok) ++fails; }

// runs until tickWalk reports done; returns ticks used
static int walk(WalkActor& a, int tx, int ty, int maxTicks = 500) {
    WalkState s; int n = 0;
    while (n < maxTicks) { ++n; if (tickWalk(a, s, tx, ty)) break; }
    return n;
}

int main() {
    { WalkActor a{0, 50, FaceDown, 0}; int n = walk(a, 30, 50);
      check("right 30 px: pos", a.x == 30 && a.y == 50);
      check("right 30 px: 1 setup + 10 steps + 1 done = 12 ticks", n == 12); }
    { WalkActor a{0, 50, FaceDown, 1}; int n = walk(a, 30, 50);
      check("style flag: 2 px per tick (15 steps)", a.x == 30 && n == 17); }
    { WalkActor a{100, 50, FaceDown, 0}; int n = walk(a, 70, 50);
      check("left 30 px, facing 0", a.x == 70 && a.facing == FaceLeft && n == 12); }
    { WalkActor a{50, 0, FaceLeft, 0}; int n = walk(a, 50, 20);
      check("down 20 px: facing 3, 2 px per tick", a.y == 20 && a.facing == FaceDown && n == 12); }
    { WalkActor a{50, 40, FaceLeft, 0}; walk(a, 50, 20);
      check("up 20 px: facing 2", a.y == 20 && a.facing == FaceUp); }
    { WalkActor a{0, 0, FaceDown, 0}; walk(a, 30, 10);
      check("shallow diagonal reaches target exactly", a.x == 30 && a.y == 10); }
    { WalkActor a{0, 0, FaceDown, 0}; walk(a, 10, 30);
      check("steep diagonal reaches target exactly", a.x == 10 && a.y == 30); }
    { WalkActor a{100, 100, FaceDown, 0}; walk(a, 60, 120);
      check("left + down reaches target", a.x == 60 && a.y == 120); }
    { WalkActor a{100, 100, FaceDown, 0}; walk(a, 130, 70);
      check("right + up reaches target", a.x == 130 && a.y == 70); }
    { WalkActor a{5, 5, FaceDown, 0}; WalkState s{kActWanderPath, 3, 5, 5};
      check("phase 3: reports done and clears action", tickWalk(a, s, 5, 5) && s.actionId == 0); }
    return fails ? 1 : 0;
}
