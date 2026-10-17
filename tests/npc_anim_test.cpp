// g++ -std=c++17 -Wall -Wextra npc_anim_test.cpp -o npc_anim_test && ./npc_anim_test
#include "npc_anim.hpp"
#include <cstdio>
#include <string>
using namespace gp;

static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-46s %s\n", n, ok ? "ok" : "FAIL"); if (!ok) ++fails; }

struct FakeHost : IdleHost {
    std::string log; int busyTicks = 0; int busy = 0; int nextRand = 1;
    void play(const AnimChoice& a) override { log += "P" + std::to_string(a.animId) + (a.altBank ? "'" : "") + " "; busy = busyTicks; }
    void playScriptedIdle() override { log += "S "; busy = busyTicks; }
    bool animBusy() const override { return busy > 0; }
    int rand(int, int) override { return nextRand; }
    void tick() { if (busy > 0) --busy; }
};

int main() {
    // walking animations
    auto w = walkAnim(0, FaceRight, 0); check("dog walk right = 0x38, loop, no flip", w.animId == 0x38 && !w.altBank && w.mode == AnimLoop);
    w = walkAnim(0, FaceLeft, 0);       check("dog walk left = 0x38 mirrored", w.animId == 0x38 && w.altBank);
    check("dog walk up = 0x36, down = 0x37", walkAnim(0, FaceUp, 0).animId == 0x36 && walkAnim(0, FaceDown, 0).animId == 0x37);
    check("kind 1 walk = 0x9f/0x9d/0x9e", walkAnim(1, FaceRight, 0).animId == 0x9f && walkAnim(1, FaceUp, 0).animId == 0x9d && walkAnim(1, FaceDown, 0).animId == 0x9e);
    check("kind 2 walk = 0xfa/0xe6/0xec", walkAnim(2, FaceRight, 0).animId == 0xfa && walkAnim(2, FaceUp, 0).animId == 0xe6 && walkAnim(2, FaceDown, 0).animId == 0xec);
    check("style flag: side-view anim 0x4e, no up/down", walkAnim(0, FaceRight, 1).animId == 0x4e && walkAnim(0, FaceUp, 1).animId == -1);
    check("idle dir tables", directionalIdleAnim(0, FaceLeft, 0).animId == 0x35 && directionalIdleAnim(2, FaceLeft, 1).animId == 0x11c);
    check("idle dir mirrored when facing right", directionalIdleAnim(0, FaceRight, 0).altBank && !directionalIdleAnim(0, FaceLeft, 0).altBank);
    check("turn-around ids", turnAroundAnim(0, FaceLeft).animId == 0x34 && turnAroundAnim(1, FaceLeft).animId == 0x9b && turnAroundAnim(2, FaceLeft).animId == 0x105);
    check("bad kind = no animation", walkAnim(5, FaceRight, 0).animId == -1);

    // SettleFacing
    { FakeHost h; h.busyTicks = 2; IdleActor a{FaceDown, 0, 0}; IdleState s;
      check("settle: already facing down finishes at once", tickSettleFacing(a, s, h) && h.log.empty()); }
    { FakeHost h; h.busyTicks = 2; h.nextRand = 2; IdleActor a{FaceUp, 0, 0}; IdleState s; int n = 0; bool done = false;
      while (!done && n < 40) { done = tickSettleFacing(a, s, h); h.tick(); ++n; }
      check("settle from up: turn, directional idle, face down", done && a.facing == FaceDown && h.log == "P52' P53' ");
      check("settle: rand 2 = right (mirrored idle/turn)", h.log.find('\'') != std::string::npos); }
    { FakeHost h; h.busyTicks = 1; h.nextRand = 1; IdleActor a{FaceUp, 0, 0}; IdleState s; bool done = false; int n = 0;
      while (!done && n < 40) { done = tickSettleFacing(a, s, h); h.tick(); ++n; }
      check("settle: rand 1 = left (no mirror)", done && h.log == "P52 P53 "); }
    { FakeHost h; h.busyTicks = 1; IdleActor a{FaceLeft, 0, 0}; IdleState s; bool done = false; int n = 0;
      while (!done && n < 40) { done = tickSettleFacing(a, s, h); h.tick(); ++n; }
      check("settle from side: only the directional idle", done && h.log == "P53 "); }

    // WalkInThenIdle
    { FakeHost h; h.busyTicks = 2; IdleActor a{FaceRight, 0, 0}; IdleState s; bool done = false; int n = 0;
      while (!done && n < 40) { done = tickWalkInThenIdle(a, s, h); h.tick(); ++n; }
      check("walk-in idle from side: scripted idle only", done && h.log == "S " && a.facing == FaceRight && s.actionId == 0); }
    { FakeHost h; h.busyTicks = 2; h.nextRand = 1; IdleActor a{FaceUp, 0, 0}; IdleState s; bool done = false; int n = 0;
      while (!done && n < 60) { done = tickWalkInThenIdle(a, s, h); h.tick(); ++n; }
      check("walk-in idle from up: turn, dir idle, scripted idle", done && h.log == "P52 P53 S "); }
    return fails ? 1 : 0;
}
