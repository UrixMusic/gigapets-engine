// tests/stat_gate_test.cpp - the five guards in area 5 (game/stat_gate.hpp).
#include <cstdio>
#include "../src/game/pet/stat_gate.hpp"
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

    const std::vector<GateDef> defs = baseGateDefs(); // the five as records (the importer fills in the barrier object ids)
    std::vector<const GateDef *> gates;
    for (const GateDef &d : defs)
        gates.push_back(&d);
    auto upd = [&](Player &pl, int quests = 1) { return updateStatGates(pl, gates, quests); };
    check("the records: flag names as before, stars or lightbulbs is one gate with two stats",
          defs.size() == 5 && defs[0].id == "base:gate/5/25" && defs[3].id == "base:gate/5/1" && defs[3].stats == std::vector<int>({Stars, Lightbulbs}) &&
              defs[0].kind == GateKind::Tricks && defs[4].kind == GateKind::Money && defs[4].need == 2000);

    Player p;
    check("a fresh pet opens nothing", !upd(p));

    p.tricksKnown = 2;
    check("two tricks is not enough", !upd(p));
    p.tricksKnown = 3;
    check("three opens the trick gate, once", upd(p) && p.hasFlag(gateFlag(5, 0x19)) && !upd(p));

    p.personality[Halos] = 249;
    check("249 halos: closed", !upd(p));
    p.personality[Halos] = 250;
    check("250 halos opens the halo gate", upd(p) && p.hasFlag(gateFlag(5, 0x0f)));
    p.personality[Pitchforks] = 250;
    check("250 pitchforks opens the pitchfork gate", upd(p) && p.hasFlag(gateFlag(5, 0x07)));

    p.personality[Stars] = 499;
    p.personality[Lightbulbs] = 499;
    check("499 stars and 499 bulbs: closed", !upd(p));
    p.personality[Lightbulbs] = 500;
    check("500 lightbulbs (or stars) opens the fourth gate", upd(p) && p.hasFlag(gateFlag(5, 0x01)));

    p.money = 1999;
    check("1999 Bucks: closed", !upd(p));
    p.money = 2000;
    check("2000 Bucks opens the money gate", upd(p) && p.hasFlag(gateFlag(5, 0x16)));

    p.money = 0;
    p.personality[Halos] = 0;
    check("spending it again leaves the gates open", p.hasFlag(gateFlag(5, 0x16)) && !upd(p) && p.hasFlag(gateFlag(5, 0x0f)));

    std::printf(fails ? "stat_gate_test: %d failure(s)\n" : "stat_gate_test: all passed\n", fails);

    { // a gate of a mod's kind: quests done, and a gate only counts in the list it is given (the area's)
        GateDef q;
        q.id = "m:gate/q";
        q.area = 5;
        q.kind = GateKind::Quests;
        q.need = 20;
        Player r;
        const std::vector<const GateDef *> only{&q};
        check("quests gate: closed below, open at the count, once", !updateStatGates(r, only, 19) && updateStatGates(r, only, 20) && r.hasFlag("m:gate/q") && !updateStatGates(r, only, 30));
        GateDef st;
        st.id = "m:gate/s";
        st.kind = GateKind::Personality;
        st.stats = {Pitchforks, Stars};
        st.need = 10;
        Player t2;
        t2.personality[Halos] = 99;
        check("a stat gate only looks at its own stats", !gateMet(st, t2, 1));
        t2.personality[Stars] = 10;
        check("... and any one of them is enough", gateMet(st, t2, 1));
        st.stats.clear();
        check("a stat gate with no stats never opens", !gateMet(st, t2, 1));
    }

    return fails ? 1 : 0;
}
