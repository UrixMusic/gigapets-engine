// tests/react_mirror_test.cpp - which pet reaction art is mirrored when the pet faces sideways.
#include <cstdio>
#include "../src/game/npc_anim_ids.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    // The panda's happy sequence is intro (0x101 side) -> hop (0xee side) x N -> outro (0x101 side).
    // All three are mirrored by the same rule, or the pet flips in and out around the hop.
    check("panda happy hop (side) is side art", isSideReactArt(2, 0xee));
    check("panda intro/outro (side) is side art", isSideReactArt(2, 0x101));
    check("panda happy hop (front) is not side art", !isSideReactArt(2, 0xed));
    check("panda intro/outro (front) is not side art", !isSideReactArt(2, 0x100));
    check("the whole sequence mirrors the same way facing left",
          reactMirror(2, false) && isSideReactArt(2, 0x101) && isSideReactArt(2, 0xee));
    check("and not at all facing right", !reactMirror(2, true));

    // dog / monkey: they have no intro/outro art, and their hops are side art
    check("dog hop is side art", isSideReactArt(0, 0x1f));
    check("monkey hop is side art", isSideReactArt(1, 0x86));
    check("monkey has no 0x101 art", !isSideReactArt(1, 0x101));
    check("no animation is never side art", !isSideReactArt(2, -1));
    check("an unrelated id is not side art", !isSideReactArt(2, 249));

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
