// tests/npc_trick_test.cpp - what an idle NPC does for a trick (TickAllPets: the roll table and the repeat counts).
#include <cstdio>
#include "../src/game/npc_brain.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("rolls 3..4 with the level 1 flags: a level 1 trick", pickActivity(3, 0x20 | 0x10).id == kActivityTrick && pickActivity(3, 0x20 | 0x10).param == 1 && pickActivity(4, 0x20 | 0x10).id == kActivityTrick);
    check("rolls 5..6: level 2, 7..8: level 3", pickActivity(5, 0x40 | 0x10).param == 2 && pickActivity(6, 0x40 | 0x10).id == kActivityTrick && pickActivity(7, 0x80 | 0x10).param == 3 && pickActivity(8, 0x80 | 0x10).id == kActivityTrick);
    check("without the known-trick flag nothing happens", pickActivity(3, 0x10).id == -1 && pickActivity(5, 0x20 | 0x10).id == -1 && pickActivity(7, 0x40 | 0x10).id == -1);
    check("without the base flag (0x10) nothing happens", pickActivity(3, 0x20).id == -1);

    Rng rng;
    bool ok1 = true, ok2m = true, ok2o = true, ok3p = true, ok3o = true;
    int lo2 = 99, hi2 = 0, lo3p = 99, hi3p = 0, lo3o = 99, hi3o = 0;
    for (int i = 0; i < 2000; ++i)
    {
        ok1 = ok1 && trickRepeatCount(1, 0, rng) == 1 && trickRepeatCount(1, 2, rng) == 1;
        const int a = trickRepeatCount(2, 1, rng);
        lo2 = a < lo2 ? a : lo2;
        hi2 = a > hi2 ? a : hi2;
        ok2o = ok2o && trickRepeatCount(2, 0, rng) == 1 && trickRepeatCount(2, 2, rng) == 1;
        const int b = trickRepeatCount(3, 2, rng);
        lo3p = b < lo3p ? b : lo3p;
        hi3p = b > hi3p ? b : hi3p;
        const int c = trickRepeatCount(3, 0, rng);
        lo3o = c < lo3o ? c : lo3o;
        hi3o = c > hi3o ? c : hi3o;
    }
    (void)ok2m; (void)ok3p; (void)ok3o;
    check("level 1 is always once", ok1);
    check("level 2: the monkey 2..5 times, the others once", lo2 == 2 && hi2 == 5 && ok2o);
    check("level 3: the panda 1..3 times", lo3p == 1 && hi3p == 3);
    check("level 3: the dog and monkey 2..5 times", lo3o == 2 && hi3o == 5);

    check("rolls 11..12 are the call (1), 13..14 the yawn (7), 1..2 the happy hop (2)", pickActivity(0x0b, 0x200 | 0x10).id == kActivityNoise && pickActivity(0x0d, 0x400 | 0x10).id == kActivityBored &&
                                                                                    pickActivity(1, 0x08 | 0x10).id == kActivityHappy);
    int lo = 99, hi = 0, vlo = 99, vhi = 0;
    for (int i = 0; i < 2000; ++i)
    {
        const int c = reactionRepeatCount(rng), v = happyVariant(rng);
        lo = c < lo ? c : lo;
        hi = c > hi ? c : hi;
        vlo = v < vlo ? v : vlo;
        vhi = v > vhi ? v : vhi;
    }
    check("the call and the hop repeat 2..4 times (RandRange(2, 5), hi exclusive)", lo == 2 && hi == 4);
    check("the hop's variant is 1 or 2", vlo == 1 && vhi == 2);

    std::printf(fails ? "npc_trick_test: %d failure(s)\n" : "npc_trick_test: all passed\n", fails);
    return fails ? 1 : 0;
}
