// tests/potion_test.cpp - colour potions (game/potion.hpp).
#include <cstdio>
#include "../src/game/potion.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("potion 0 on a dog / monkey / panda", potionPalette(0, 0) == 0x1df && potionPalette(1, 0) == 0x29e && potionPalette(2, 0) == 0x31e);
    check("potion 4 on each", potionPalette(0, 4) == 0x1fe && potionPalette(1, 4) == 0x2bd && potionPalette(2, 4) == 0x33d);
    check("the monkey's third potion is 0x2ab, the panda's is 0x32b", potionPalette(1, 2) == 0x2ab && potionPalette(2, 2) == 0x32b);
    check("outside the table: -1", potionPalette(3, 0) == -1 && potionPalette(0, 5) == -1 && potionPalette(-1, 0) == -1 && potionPalette(0, -1) == -1);

    check("the exclamation: intro frames then outro frames at 12 fps", exclaimView(0, 4, 3).anim == 0xb && exclaimView(0, 4, 3).frame == 0);
    const int intro = exclaimSteps(4);
    check("the intro lasts 10 ticks for 4 frames, ending on frame 3", intro == 10 && exclaimView(intro - 1, 4, 3).anim == 0xb && exclaimView(intro - 1, 4, 3).frame == 3);
    check("then the outro from its first frame", exclaimView(intro, 4, 3).anim == 0xc && exclaimView(intro, 4, 3).frame == 0);
    check("and it is gone after the outro", !exclaimView(intro + exclaimSteps(3) - 1, 4, 3).done && exclaimView(intro + exclaimSteps(3), 4, 3).done);

    std::printf(fails ? "potion_test: %d failure(s)\n" : "potion_test: all passed\n", fails);
    return fails ? 1 : 0;
}
