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
namespace gp
{
    inline int classifyReaction(int pet, int npc) // player side: 1, 2, 5 or 6
    {
        if (pet == 0)
            return 1;
        if (npc == 0)
            return 6;
        if ((pet < 0) != (npc < 0))
            return 5;
        const double r = double(npc) / double(pet);
        return r > 0.9 ? 2 : r > 0.75 ? 1 : 6;
    }

    inline int classifyCompat(int pet, int npc) // animal side: 1, 2, 4, 5 or 6
    {
        if (npc == 0)
            return 1;
        if (pet == 0)
            return npc < 0 ? 4 : 6;
        if ((pet < 0) != (npc < 0))
            return 5;
        const double r = double(pet) / double(npc);
        return r > 0.9 ? 2 : r > 0.75 ? 1 : 6;
    }

    // Friendship needs the animal side to be 1 or 2 and the player side not 4 or 5.
    inline bool encounterBefriends(int pet, int npc)
    {
        const int a = classifyCompat(pet, npc), p = classifyReaction(pet, npc);
        return (a == 1 || a == 2) && p != 4 && p != 5;
    }
}