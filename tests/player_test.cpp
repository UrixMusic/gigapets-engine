// g++ -std=c++17 -Wall -Wextra player_test.cpp -o player_test && ./player_test
#include "exits.hpp"
#include "player.hpp"
#include <cstdio>
using namespace gp;

struct WallWorld : IWorld {            // wall at x >= 100, big open room
    Room room{2560, 1600, 42, 42, 62, 20};
    bool tileWalkable(int x, int) const override { return x < 100; }
    bool inRoom(int x, int y) const override { return gp::inRoom(room, x, y); }
    bool objectBlocks(int, int) const override { return false; }
};

static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-40s %s\n", n, ok ? "ok" : "FAIL"); if (!ok) ++fails; }
static bool is(Point p, int x, int y) { return p.x == x && p.y == y; }

int main() {
    check("up",         is(stepFor(DirUp), 0, -2));
    check("down",       is(stepFor(DirDown), 0, 2));
    check("left",       is(stepFor(DirLeft), -3, 0));
    check("right",      is(stepFor(DirRight), 3, 0));
    check("up+right",   is(stepFor(DirUp | DirRight), 2, -2));
    check("down+left",  is(stepFor(DirDown | DirLeft), -2, 2));
    check("down+right", is(stepFor(DirDown | DirRight), 2, 2));
    check("up+left",    is(stepFor(DirUp | DirLeft), -2, -2));
    check("up+down: down wins",  is(stepFor(DirUp | DirDown), 0, 2));
    check("left+right: left wins", is(stepFor(DirLeft | DirRight), -3, 0));
    check("left+right+up: left, 2", is(stepFor(DirLeft | DirRight | DirUp), -2, -2));
    check("promptState 0: right = 2", is(stepFor(DirRight, 0), 2, 0));
    check("promptState 0: left = -2", is(stepFor(DirLeft, 0), -2, 0));
    check("promptState 0: diagonal same", is(stepFor(DirDown | DirRight, 0), 2, 2));
    check("idle",       is(stepFor(0), 0, 0));

    // input events
    InputTick t = applyInputEvents(0, {}, {BtnRight, BtnUp}, 1);
    check("press right+up (state 1)", t.held == (DirRight | DirUp));
    t = applyInputEvents(t.held, {BtnUp}, {}, 1);
    check("release up", t.held == DirRight);
    t = applyInputEvents(0, {}, {BtnUp, BtnDown}, 0);
    check("up/down ignored in state 0", t.held == 0);
    t = applyInputEvents(DirUp, {BtnUp}, {}, 0);
    check("up release ignored in state 0 (quirk)", t.held == DirUp);
    t = applyInputEvents(DirLeft, {BtnLeft}, {BtnLeft}, 1);
    check("releases before presses", t.held == DirLeft);
    t = applyInputEvents(0, {}, {BtnAction, BtnSecondary}, 1);
    check("action/secondary flags", t.actionPressed && t.secondaryPressed && t.held == 0);
    check("pollHeld", pollHeld(true, false, false, true) == (DirLeft | DirDown));

    // walking into a wall
    WallWorld w;
    check("walk freely", is(stepPlayer(w, {50, 200}, DirRight).pos, 53, 200));
    MoveResult r = stepPlayer(w, {98, 200}, DirRight);
    check("wall: pushed to x=99", is(r.pos, 99, 200) && r.slid);

    // exits
    const StoryExit* e = findStoryExit(0, 0x68);
    check("exit (area 0, obj 0x68) -> reason 4", e && e->spawnReason == 4 && e->consumeAndSpawn);
    e = findStoryExit(3, 0x1b);
    check("exit (area 3, obj 0x1b) -> 0x22, no spawn", e && e->spawnReason == 0x22 && !e->consumeAndSpawn);
    check("same object, other area: no exit", findStoryExit(4, 0x68) == nullptr);
    return fails ? 1 : 0;
}