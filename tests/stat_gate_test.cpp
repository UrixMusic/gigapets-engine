// tests/stat_gate_test.cpp - the five guards in area 5 (game/stat_gate.hpp).
#include <cstdio>
#include "../src/game/stat_gate.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("five gates; barriers 0x19 0x0f 0x07 0x01 0x16 and guards 0x1b 0x10 0x08 0x03 0x18",
          kStatGates[0].barrier == 0x19 && kStatGates[1].barrier == 0x0f && kStatGates[2].barrier == 0x07 && kStatGates[3].barrier == 0x01 &&
              kStatGates[4].barrier == 0x16 && kStatGates[0].guard == 0x1b && kStatGates[1].guard == 0x10 && kStatGates[2].guard == 0x08 &&
              kStatGates[3].guard == 0x03 && kStatGates[4].guard == 0x18);
    check("the guards' texts are 0x316..0x31a", kStatGates[0].text == 0x316 && kStatGates[4].text == 0x31a && guardAt(0x10)->text == 0x317);
    check("an object that is not a guard has no gate", guardAt(0x11) == nullptr && guardAt(0x19) == nullptr);

    Player p;
    check("a fresh pet opens nothing", !updateStatGates(p));

    p.tricksKnown = 2;
    check("two tricks is not enough", !updateStatGates(p));
    p.tricksKnown = 3;
    check("three opens the trick gate, once", updateStatGates(p) && p.hasFlag(gateFlag(5, 0x19)) && !updateStatGates(p));

    p.personality[Halos] = 249;
    check("249 halos: closed", !updateStatGates(p));
    p.personality[Halos] = 250;
    check("250 halos opens the halo gate", updateStatGates(p) && p.hasFlag(gateFlag(5, 0x0f)));
    p.personality[Pitchforks] = 250;
    check("250 pitchforks opens the pitchfork gate", updateStatGates(p) && p.hasFlag(gateFlag(5, 0x07)));

    p.personality[Stars] = 499;
    p.personality[Lightbulbs] = 499;
    check("499 stars and 499 bulbs: closed", !updateStatGates(p));
    p.personality[Lightbulbs] = 500;
    check("500 lightbulbs (or stars) opens the fourth gate", updateStatGates(p) && p.hasFlag(gateFlag(5, 0x01)));

    p.money = 1999;
    check("1999 Bucks: closed", !updateStatGates(p));
    p.money = 2000;
    check("2000 Bucks opens the money gate", updateStatGates(p) && p.hasFlag(gateFlag(5, 0x16)));

    p.money = 0;
    p.personality[Halos] = 0;
    check("spending it again leaves the gates open", p.hasFlag(gateFlag(5, 0x16)) && !updateStatGates(p) && p.hasFlag(gateFlag(5, 0x0f)));

    std::printf(fails ? "stat_gate_test: %d failure(s)\n" : "stat_gate_test: all passed\n", fails);
    return fails ? 1 : 0;
}
