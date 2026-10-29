// tests/trick_panel_test.cpp - the tricks panel's layout and keys (game/trick_panel.hpp).
#include <cstdio>
#include "../src/game/trick_panel.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("the dog's icons are 0x58 0x5b 0x5c, the monkey's 0x5d 0x5a 0x59, the panda's 0x60 0x56 0x57",
          kTrickIcons[0][0] == 0x58 && kTrickIcons[0][2] == 0x5c && kTrickIcons[1][0] == 0x5d && kTrickIcons[1][2] == 0x59 &&
              kTrickIcons[2][0] == 0x60 && kTrickIcons[2][2] == 0x57);
    check("nothing learned: placeholders 0x5f, 0x5e, 0x5f, the left one at 0x69", trickIcon(0, 0, 0).sprite == 0x5f && trickIcon(0, 0, 0).x == 0x69 &&
                                                                                   trickIcon(0, 1, 0).sprite == 0x5e && trickIcon(0, 2, 0).sprite == 0x5f);
    check("one learned: the left slot shows the icon at 0x73, the others still wait", trickIcon(1, 0, 1).sprite == 0x5d && trickIcon(1, 0, 1).x == 0x73 &&
                                                                                       trickIcon(1, 1, 1).sprite == 0x5e);
    check("all three learned: every icon in place", trickIcon(2, 0, 3).sprite == 0x60 && trickIcon(2, 1, 3).sprite == 0x56 && trickIcon(2, 2, 3).sprite == 0x57 &&
                                                       trickIcon(2, 1, 3).x == 0x9d && trickIcon(2, 1, 3).y == 0xb4 && trickIcon(2, 2, 3).x == 199 && trickIcon(2, 2, 3).y == 0xdc);
    check("left is trick 1, up trick 2, right trick 3, down nothing", trickSlotForEvent(0) == 0 && trickSlotForEvent(2) == 1 && trickSlotForEvent(1) == 2 && trickSlotForEvent(3) == -1);
    check("Z, X and Enter close it", trickClosesPanel(4) && trickClosesPanel(5) && trickClosesPanel(6) && !trickClosesPanel(0) && !trickClosesPanel(3));

    std::printf(fails ? "trick_panel_test: %d failure(s)\n" : "trick_panel_test: all passed\n", fails);
    return fails ? 1 : 0;
}
