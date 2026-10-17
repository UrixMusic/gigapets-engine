// game/offerings.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace gp
{
    struct OfferSet
    {
        int npc, firstItem, lastItem;
    };
    constexpr OfferSet kKingOffers{0x49, 0xaa, 0xb5}, kGardeniaOffers{0x40, 0xdf, 0xea};

    inline int popcount16(unsigned v)
    {
        int n = 0;
        for (; v; v &= v - 1)
            ++n;
        return n;
    }

    struct OfferResult
    {
        int item = -1;
        int reward = -1;
    }; // item -1 = nothing to offer

    // flags: g_itemOfferedFlags_A (king) / DAT_001e18 (Gardenia), saved with the pet.
    // rewards: the 12 words at 0x1e36 / 0x1e2a (RAM, runtime-filled: dump them live before trusting).
    inline OfferResult tryOffer(const OfferSet &s, uint16_t &flags, const std::function<int(int)> &held,
                                const std::vector<int> &rewards)
    {
        for (int it = s.firstItem; it <= s.lastItem; ++it)
        {
            if (held(it) < 1 || (flags >> (it - s.firstItem) & 1))
                continue;
            OfferResult r;
            r.item = it;
            const int idx = popcount16(flags);
            r.reward = idx < int(rewards.size()) ? rewards[size_t(idx)] : -1;
            flags = uint16_t(flags | 1u << (it - s.firstItem));
            return r;
        }
        return {};
    }

    // Game_ProcessTick: after 0x10 quests the king's first item (0xaa) counts as offered.
    inline void autoMarkKing(int questCounter, uint16_t &kingFlags)
    {
        if (questCounter > 0x10)
            kingFlags |= 1;
    }
}