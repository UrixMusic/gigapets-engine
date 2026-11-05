// tests/trick_panel_test.cpp - the tricks panel's layout and keys (game/trick_panel.hpp).
#include <cstdio>
#include "../src/game/trick_panel.hpp"
#include "../src/content/records.hpp"
#include "../src/game/player.hpp"
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

    { // the layout is data: the defaults are the old numbers, and a record's numbers change them
        MinigameDef m;
        TrickLayout d = TrickLayout::from(m);
        check("defaults match the game's own", trickIcon(1, 0, 1, d).sprite == trickIcon(1, 0, 1).sprite && trickIcon(1, 0, 0, d).x == 0x69 && d.click == 0x59 && d.questionWait == 0.5);
        m.num = {{"knownXTop", 10}, {"unknownTop", 0x77}, {"questionWaitMs", 1000}, {"click", 5}};
        d = TrickLayout::from(m);
        check("a record moves the icon, swaps the placeholder, changes the wait and click", trickIcon(1, 1, 3, d).x == 10 && trickIcon(1, 1, 0, d).sprite == 0x77 && d.questionWait == 1.0 && d.click == 5);
    }
    { // which tricks the pet knows: a mask, in any order; the count follows
        Player p;
        p.setTricksInOrder(2);
        check("two in order: tricks 1 and 2, count 2", p.trickMask == 3 && p.tricksKnown == 2 && p.knowsTrick(0) && p.knowsTrick(1) && !p.knowsTrick(2));
        p.setTrickMask(4);
        check("only trick 3: count 1, and the panel shows its icon and the others' placeholders", p.tricksKnown == 1 && !p.knowsTrick(0) && p.knowsTrick(2) &&
                                                                                                         trickIcon(0, 2, p.trickMask, TrickLayout()).sprite == trickIcon(0, 2, 3).sprite && trickIcon(0, 0, p.trickMask, TrickLayout()).sprite == 0x5f);
        p.setTrickMask(-5);
        check("a bad mask is nothing", p.trickMask == 0 && p.tricksKnown == 0);
    }
    std::printf(fails ? "trick_panel_test: %d failure(s)\n" : "trick_panel_test: all passed\n", fails);
    return fails ? 1 : 0;
}
