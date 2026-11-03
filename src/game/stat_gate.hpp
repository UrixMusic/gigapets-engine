// stat_gate.hpp - the five guards on the way to the UFO in Magma Mountain (area 5).
//
// Each guard stands next to a barrier. CheckStatsAndMoneyGate (0x01aeef) runs every tick, after item use and after a
// personality change; when a barrier's condition is met it clears that barrier's progress bit (the object goes away) and
// never sets it again. Talking to a guard always just shows its text (ProcessStoryModeObjectInteraction, texts 0x316..0x31a).
//
//   barrier object  guard object  condition                        text
//   0x19            0x1b          Trick 3 known (tricksKnown > 2)  0x316
//   0x0f            0x10          halos >= 250                     0x317
//   0x07            0x08          pitchforks >= 250                0x318
//   0x01            0x03          stars >= 500 or lightbulbs >= 500  0x319
//   0x16            0x18          2000 Bucks                       0x31a
//
// The base game's five are the table kStatGates (what the importer turns into GateDef records, with the guards' messages); the game
// itself only reads the records, so a mod adds gates with the `gate` record type.
#pragma once
#include <string>
#include <vector>
#include "../content/records.hpp"
#include "player.hpp"

namespace gp
{

    constexpr int kGateArea = 5;

    // The base game's data: object slots in area 5, the guard's text id, the condition (stat2 = -1: only `stat`).
    struct StatGate
    {
        int barrier, guard, text;
        GateKind kind;
        int stat, stat2, need;
    };

    constexpr StatGate kStatGates[5] = {
        {0x19, 0x1b, 0x316, GateKind::Tricks, 0, -1, 3},
        {0x0f, 0x10, 0x317, GateKind::Personality, Halos, -1, 250},
        {0x07, 0x08, 0x318, GateKind::Personality, Pitchforks, -1, 250},
        {0x01, 0x03, 0x319, GateKind::Personality, Stars, Lightbulbs, 500}, // stars or lightbulbs
        {0x16, 0x18, 0x31a, GateKind::Money, 0, -1, 2000},
    };

    inline std::string gateFlag(int area, int barrier) { return "base:gate/" + std::to_string(area) + "/" + std::to_string(barrier); }

    // The five as records. `barrier` is left empty: the importer fills in the id of the object in that slot.
    inline std::vector<GateDef> baseGateDefs()
    {
        std::vector<GateDef> v;
        for (const StatGate &g : kStatGates)
        {
            GateDef d;
            d.id = gateFlag(kGateArea, g.barrier);
            d.area = kGateArea;
            d.kind = g.kind;
            if (g.kind == GateKind::Personality)
            {
                d.stats.push_back(g.stat);
                if (g.stat2 >= 0)
                    d.stats.push_back(g.stat2);
            }
            d.need = g.need;
            v.push_back(std::move(d));
        }
        return v;
    }

    inline bool gateMet(const GateDef &g, const Player &p, int quests)
    {
        switch (g.kind)
        {
        case GateKind::Tricks:
            return p.tricksKnown >= g.need;
        case GateKind::Personality:
            for (int s : g.stats)
                if (s >= 0 && s < kNumPersonality && p.personality[size_t(s)] >= g.need)
                    return true;
            return false;
        case GateKind::Money:
            return p.money >= g.need;
        case GateKind::Quests:
            return quests >= g.need;
        }
        return false;
    }

    // One run of the check over the gates of the area the pet is in (db.gatesIn). Opens (and remembers) every gate whose condition
    // holds; true if any opened just now. `quests` = the quest counter.
    inline bool updateStatGates(Player &p, const std::vector<const GateDef *> &gates, int quests)
    {
        bool any = false;
        for (const GateDef *g : gates)
        {
            if (!g || p.hasFlag(g->id) || !gateMet(*g, p, quests))
                continue;
            p.setFlag(g->id);
            any = true;
        }
        return any;
    }

    // The base game's guard in front of this object slot, or null.
    inline const StatGate *guardAt(int slot)
    {
        for (const StatGate &g : kStatGates)
            if (g.guard == slot)
                return &g;
        return nullptr;
    }

} // namespace gp
