// game/befriend.hpp
// Pure rules from TryBefriendNPC / GrantFriendship / GetPersonalityStatDelta.
// Only the confirmed pieces are here; the full TryBefriendNPC flow is not ported yet.
#pragma once
#include "npc_brain.hpp" // Rng (range(lo, hi) has hi EXCLUSIVE, like RandRange)

namespace gp
{

    // GetPersonalityStatDelta(a, b): d = a - b, with a dead zone of -9..9 that returns 0.
    inline int personalityStatDelta(int a, int b)
    {
        int d = a - b;
        return (d >= 10 || d <= -10) ? d : 0;
    }

    // game/befriend.hpp
    inline const char *statHintKey(int delta, int halos, int pitchforks)
    {
        if (delta < 0)
            return halos > 9998 ? "hint_fewer_halos" : "hint_more_pitchforks";
        return pitchforks > 9998 ? "hint_fewer_pitchforks" : "hint_more_halos";
    }

    // TryBefriendNPC, friended NPC, first visit this session: base chance, doubled once if stars >= requirement.
    // (befriendChance above is RollAnimalBefriendChance's: always doubled.)
    inline int friendVisitChance(int base, int stars, int starRequirement)
    {
        return stars >= starRequirement ? base * 2 : base;
    }

    // First-contact befriend chance (TryBefriendNPC, branch B, species not yet discovered):
    // base chance from GetAnimalBaseBefriendChance, doubled if stars >= the NPC's star requirement.
    // RollAnimalBefriendChance: ShiftLeft(base, 1) always, and again if stars >= the NPC's requirement.
    inline int befriendChance(int baseChance, int stars, int starRequirement)
    {
        int c = baseChance * 2;
        if (stars >= starRequirement)
            c *= 2;
        return c;
    }

    // The success roll: RandRange(1, 0x65) is 1..100, and success is roll < chance,
    // so a chance of 50 really succeeds 49 times in 100.
    inline bool befriendRoll(Rng &rng, int chance)
    {
        return rng.range(1, 0x65) < chance;
    }

    // GrantFriendship's greeting roll, RandRange(1, 4): each outcome 1 in 3.
    enum class FriendGreeting
    {
        Hello = 1,
        Gift = 2,
        Game = 3
    };

    // GetDefaultNPCDialogueLine (non-stationary NPCs): a random entry of a 6-word RAM table that boot code
    // fills with text ids 0x2e1..0x2e6 (writer pc 0xfa7e). Lower bound of the pick (0 or 1) NOT verified.
    constexpr int kDefaultLineFirst = 0x2e1, kDefaultLineCount = 6;
    inline std::string defaultLineId(Rng &rng)
    {
        return "base:text/" + std::to_string(kDefaultLineFirst + rng.range(0, kDefaultLineCount));
    }
    inline FriendGreeting rollFriendGreeting(Rng &rng)
    {
        return FriendGreeting(rng.range(1, 4));
    }

} // namespace gp