// npc_anim_test.cpp - which animation an actor plays (npc_anim_ids.hpp): walking by species / facing / style, the turn-arounds, the settled idle.
// (The idle state machine this test once also ran, with its IdleHost, is gone from the game; the tables it read are what is checked here.)
#include "../src/game/npc/npc_anim_ids.hpp"
#include <cstdio>
#include <string>
using namespace gp;

static int fails = 0;
static void check(const char *n, bool ok)
{
    std::printf("%-46s %s\n", n, ok ? "ok" : "FAIL");
    if (!ok)
        ++fails;
}

int main()
{
    // walking animations
    auto w = walkAnim(0, FaceRight, 0);
    check("dog walk right = 0x38, loop, no flip", w.id == 0x38 && !w.mirror && w.loop);
    w = walkAnim(0, FaceLeft, 0);
    check("dog walk left = 0x38 mirrored", w.id == 0x38 && w.mirror);
    check("dog walk up = 0x36, down = 0x37", walkAnim(0, FaceUp, 0).id == 0x36 && walkAnim(0, FaceDown, 0).id == 0x37);
    check("kind 1 walk = 0x9f/0x9d/0x9e", walkAnim(1, FaceRight, 0).id == 0x9f && walkAnim(1, FaceUp, 0).id == 0x9d && walkAnim(1, FaceDown, 0).id == 0x9e);
    check("kind 2 walk = 0xfa/0xe6/0xec", walkAnim(2, FaceRight, 0).id == 0xfa && walkAnim(2, FaceUp, 0).id == 0xe6 && walkAnim(2, FaceDown, 0).id == 0xec);
    check("style flag: side-view anim 0x4e, no up/down", walkAnim(0, FaceRight, 1).id == 0x4e && walkAnim(0, FaceUp, 1).id == -1);
    check("bad kind = no animation", walkAnim(5, FaceRight, 0).id == -1);

    // turning toward the camera after walking
    check("turn after walking sideways (90): ids", turn90(0, FaceLeft).id == 0x35 && turn90(2, FaceLeft, true).id == 0x11c);
    check("turn 90 mirrored when facing right", turn90(0, FaceRight).mirror && !turn90(0, FaceLeft).mirror);
    check("turn-around (180) ids", turn180(0, FaceLeft).id == 0x34 && turn180(1, FaceLeft).id == 0x9b && turn180(2, FaceLeft).id == 0x105);

    // the settled idle
    check("settled idle: dog sit idle = 45, then 149 / 251", settledIdle(0).id == 45 && settledIdle(1).id == 149 && settledIdle(2).id == 251);
    return fails ? 1 : 0;
}
