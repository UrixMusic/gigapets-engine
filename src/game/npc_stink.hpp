// npc_stink.hpp - stink spreading between the pet and the NPC it is standing next to.
//
// TickCareStatWatchdog conditions 9 and 11 (read from the ROM and from a RAM dump, blocks at 0x1ed5 and 0x1ee3):
//   condition 9, a timer of 45 ROM ticks (0.75 s), once more than 3 quests are done. With the selected NPC (the one the pet can
//   talk to; RAM 0x1a9f):
//     pet stinky (Cleanliness <= 0x19): near an NPC that is not stinky, count; a different NPC than last time restarts the count at 1;
//     pet clean:                         near a stinky NPC, count.
//     Anything else counts nothing. The count (RAM 0x1e94) is NOT cleared by walking away, only when it pays off.
//   condition 11, when the count reaches 45 (about 34 s of such checks): the count goes back to 0, and
//     pet stinky: the NPC beside it becomes stinky (SetPetFlagBit8, flag 0x100: the stink cloud);
//     pet clean:  the pet's Cleanliness drops by 0x50 (AdjustCareStat(5, -0x50)).
// The minipet-link branch of condition 9 (g_miniPetTrackingFlag) is not ported.
#pragma once
#include "thought_bubble.hpp"

namespace gp
{

    constexpr int kStinkCheckRomTicks = 45, kStinkCountNeeded = 45, kStinkHygieneDrop = 0x50;
    constexpr int kStinkCheckTicks = int(romToSim(kStinkCheckRomTicks)); // 23 steps of the 30 Hz sim

    struct StinkTransfer
    {
        int accum = 0;
        int count = 0;
        int lastNpc = -1;
    };

    enum class StinkEvent
    {
        None,
        NpcStinks, // the NPC beside a stinky pet is now stinky
        PetStinks  // a clean pet's Cleanliness drops (call adjustCare(Cleanliness, -kStinkHygieneDrop))
    };

    // One sim step. npcIndex = the selected NPC's pool index, -1 for none; npcStinky = whether it already stinks.
    inline StinkEvent stinkTransferStep(StinkTransfer &s, int quests, int hygiene, int npcIndex, bool npcStinky)
    {
        if (++s.accum < kStinkCheckTicks)
            return StinkEvent::None;
        s.accum = 0;
        if (quests <= 3)
            return StinkEvent::None;
        const bool petStinky = hygiene < kStinkBelow; // Cleanliness <= 0x19
        if (npcIndex >= 0)
        {
            if (petStinky && !npcStinky)
            {
                if (npcIndex == s.lastNpc)
                    ++s.count;
                else
                {
                    s.count = 1;
                    s.lastNpc = npcIndex;
                }
            }
            else if (!petStinky && npcStinky)
                ++s.count;
        }
        if (s.count < kStinkCountNeeded)
            return StinkEvent::None;
        s.count = 0;
        if (petStinky)
            return npcIndex >= 0 && !npcStinky ? StinkEvent::NpcStinks : StinkEvent::None;
        return StinkEvent::PetStinks;
    }

} // namespace gp
