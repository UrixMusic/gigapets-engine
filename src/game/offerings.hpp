// game/offerings.hpp
#pragma once
#include "../content/db.hpp"
#include "player.hpp"
#include "fixed_spawn.hpp"   // offeredKey
#include <functional>

namespace gp
{
    // Returns the item handed over, or an empty Id when nothing in the set can be offered.
    // Items are tried in the set's order, one per conversation, like the old firstItem..lastItem scan.
    inline Id tryOffer(const OfferSetDef &s, Player &p, const std::function<int(const Id &)> &held)
    {
        for (const Id &it : s.items)
        {
            if (held(it) < 1 || p.hasFlag(offeredKey(it)))
                continue;
            p.setFlag(offeredKey(it));
            return it;
        }
        return {};
    }

    // CountItemOfferedFlagsSet / CountLegacyFlagsSet: how many of the set's items are already handed over (counting the king's
    // auto-marked one).
    inline int offeredCount(const OfferSetDef &s, const Player &p)
    {
        int n = 0;
        for (const Id &it : s.items)
            n += p.hasFlag(offeredKey(it)) ? 1 : 0;
        return n;
    }

    // The item the NPC gives back for the next hand-in, given the count BEFORE it (-1 = nothing).
    inline int rewardFor(const OfferSetDef &s, int countBefore)
    {
        return countBefore >= 0 && countBefore < int(s.rewardItems.size()) ? s.rewardItems[size_t(countBefore)] : -1;
    }

    // Game_ProcessTick: after 0x10 quests the king's first item counts as offered (data on the record).
    inline void autoMarkOffers(const OfferSetDef &s, int questCounter, Player &p)
    {
        if (s.autoMarkAfterQuests > 0 && !s.autoMarkItem.empty() && questCounter >= s.autoMarkAfterQuests)
            p.setFlag(offeredKey(s.autoMarkItem));
    }
}