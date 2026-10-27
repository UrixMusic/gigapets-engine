// game/befriend_rules.hpp - the base game's befriending numbers, as a record. No ROM, no Rng.
#pragma once
#include <string>
#include "../content/records.hpp"

namespace gp
{
    // Values from TryBefriendNPC / RollAnimalBefriendChance / GrantFriendship / GetPersonalityStatDelta.
    inline BefriendRules baseBefriendRules()
    {
        BefriendRules r;
        r.greetings = {{"hello", 1}, {"gift", 1}, {"game", 1}};
        // boot code fills a 6-word RAM table with text ids 0x2e1..0x2e6 (writer pc 0xfa7e)
        for (int k = 0; k < 6; ++k)
            r.defaultLines.push_back("base:text/" + std::to_string(0x2e1 + k));
        // Confirmed against MAME for a pet at -39: sign cases, zero cases, and the ~0.9 line.
        // NOT confirmed: the 0.75 line, positive (halo-heavy) pets, and > vs >= at the thresholds.
        r.encounter.bands = {{0.9, 2}, {0.75, 1}};
        return r;
    }

    inline const BefriendRules &defaultBefriendRules()
    {
        static const BefriendRules r = baseBefriendRules();
        return r;
    }
}