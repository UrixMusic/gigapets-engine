#pragma once
// Befriend encounter rules, ported from ClassifyReactionCategory (player side) and
// ClassifyCompatibilityReaction (animal side), used by UpdateNPCEncounter step 0xc.
//
// pet = pet's delta = personalityStatDelta(halos, pitchforks)
// npc = the NPC's delta = personalityStatDelta(npc.halos, npc.pitchforks)
// Both use the same dead zone (-9..9 -> 0).
//
// Confirmed against MAME for a pet at -39: sign cases, zero cases, and the ~0.9 line.
// NOT confirmed: the 0.75 line, positive (halo-heavy) pets, and the exact comparison
// direction at the thresholds (> vs >=).
//
// The cut-offs and reaction codes live in EncounterRules (BefriendRules::encounter); the overloads without a
// rules argument use the base game's values.
#include <algorithm>
#include "befriend_rules.hpp"
namespace gp
{
    namespace detail
    {
        inline int ratioBand(double r, const EncounterRules &e)
        {
            for (const auto &b : e.bands)
                if (r > b.first)
                    return b.second;
            return e.belowBands;
        }
    }

    inline int classifyReaction(int pet, int npc, const EncounterRules &e) // player side: 1, 2, 5 or 6
    {
        if (pet == 0)
            return e.playerPetZero;
        if (npc == 0)
            return e.playerNpcZero;
        if ((pet < 0) != (npc < 0))
            return e.oppositeSigns;
        return detail::ratioBand(double(npc) / double(pet), e);
    }
    inline int classifyReaction(int pet, int npc) { return classifyReaction(pet, npc, defaultBefriendRules().encounter); }

    inline int classifyCompat(int pet, int npc, const EncounterRules &e) // animal side: 1, 2, 4, 5 or 6
    {
        if (npc == 0)
            return e.animalNpcZero;
        if (pet == 0)
            return npc < 0 ? e.animalPetZeroNpcNeg : e.animalPetZeroNpcPos;
        if ((pet < 0) != (npc < 0))
            return e.oppositeSigns;
        return detail::ratioBand(double(pet) / double(npc), e);
    }
    inline int classifyCompat(int pet, int npc) { return classifyCompat(pet, npc, defaultBefriendRules().encounter); }

    // Friendship needs the animal side to be a friendly code and the player side not a blocking one.
    inline bool encounterBefriends(int pet, int npc, const EncounterRules &e)
    {
        const int a = classifyCompat(pet, npc, e), p = classifyReaction(pet, npc, e);
        return std::find(e.friendlyAnimal.begin(), e.friendlyAnimal.end(), a) != e.friendlyAnimal.end() &&
               std::find(e.blockingPlayer.begin(), e.blockingPlayer.end(), p) == e.blockingPlayer.end();
    }
    inline bool encounterBefriends(int pet, int npc) { return encounterBefriends(pet, npc, defaultBefriendRules().encounter); }
}