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
#pragma once
#include <string>
#include "player.hpp"

namespace gp
{

    constexpr int kGateArea = 5;

    enum class GateKind
    {
        Tricks,
        Personality,
        StarsOrBulbs,
        Money
    };

    struct StatGate
    {
        int barrier, guard, text; // object slots in area 5, and the guard's text id
        GateKind kind;
        int stat, need; // Personality: which stat; need: the least value that opens it
    };

    constexpr StatGate kStatGates[5] = {
        {0x19, 0x1b, 0x316, GateKind::Tricks, 0, 3},
        {0x0f, 0x10, 0x317, GateKind::Personality, Halos, 250},
        {0x07, 0x08, 0x318, GateKind::Personality, Pitchforks, 250},
        {0x01, 0x03, 0x319, GateKind::StarsOrBulbs, 0, 500},
        {0x16, 0x18, 0x31a, GateKind::Money, 0, 2000},
    };

    inline std::string gateFlag(int area, int barrier) { return "base:gate/" + std::to_string(area) + "/" + std::to_string(barrier); }

    inline bool gateMet(const StatGate &g, const Player &p)
    {
        switch (g.kind)
        {
        case GateKind::Tricks:
            return p.tricksKnown >= g.need;
        case GateKind::Personality:
            return p.personality[size_t(g.stat)] >= g.need;
        case GateKind::StarsOrBulbs:
            return p.personality[Lightbulbs] >= g.need || p.personality[Stars] >= g.need;
        case GateKind::Money:
            return p.money >= g.need;
        }
        return false;
    }

    // One run of the check. Opens (and remembers) every gate whose condition holds; true if any opened just now.
    inline bool updateStatGates(Player &p)
    {
        bool any = false;
        for (const StatGate &g : kStatGates)
        {
            const std::string f = gateFlag(kGateArea, g.barrier);
            if (!p.hasFlag(f) && gateMet(g, p))
            {
                p.setFlag(f);
                any = true;
            }
        }
        return any;
    }

    // The guard in front of this object slot, or null.
    inline const StatGate *guardAt(int slot)
    {
        for (const StatGate &g : kStatGates)
            if (g.guard == slot)
                return &g;
        return nullptr;
    }

} // namespace gp
