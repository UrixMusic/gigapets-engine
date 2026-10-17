#pragma once
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>
#include "quest.hpp"

namespace gp
{
    struct QuestRomTables
    {
        std::vector<std::pair<int, int>> itemWeights; // (item, weight) x 0x43, word 0xc4c5, stride 2
        int distance[10][10] = {};                    // word 0xe339, [from][to]
    };

    struct QuestWorld
    {
        std::function<int(int)> areaTier; // pool id -> g_npcInfoTable.areaId. Leave EMPTY to disable generic quests.
        std::function<bool(int)> stationary = [](int)
        { return false; }; // "IsSpeciesScripted" is really the NPC's stationary flag
        std::function<int(int)> speciesOf = [](int)
        { return 0; };                         // GetPoolEntrySpeciesId (mode 2 only)
        std::function<int(int, int)> modeItem; // optional override (tests); empty = the ported pickModeItem
        int playerSpecies = 0;
        int poolCount = 0x4e; // RandRange(0, 0x4e) -> ids 0..0x4d
        QuestRomTables rom;
    };

    // PickRandomUnlockedAreaTier's gate (counter starts at 1, so "3 quests done" is counter > 3).
    inline bool tierUnlocked(int tier, int total)
    {
        switch (tier)
        {
        case 0:
            return true;
        case 1:
            return total > 3;
        case 2:
        case 3:
            return total > 0x10;
        case 9:
            return total > 0xf;
        case 6:
            return total > 0x29;
        case 7:
            return total > 0x3c;
        case 5:
            return total > 0x59;
        default:
            return false;
        }
    }

    // PickRandomUnlockedAreaTier: really returns a pool id.
    inline int pickNpc(int total, const QuestWorld &w, const RandFn &rnd)
    {
        for (;;)
        {
            const int id = rnd(0, w.poolCount);
            if (tierUnlocked(w.areaTier(id), total))
                return id;
        }
    }

    // RollQuestProgressGatedValue: tier 0 appears twice.
    inline int progressTier(int total, const RandFn &rnd)
    {
        static constexpr int tier[8] = {0, 1, 3, 2, 6, 7, 5, 0};
        static constexpr int gate[8] = {0, 3, 0xd, 0x10, 0x29, 0x3c, 0x59, 0};
        for (;;)
        {
            const int i = rnd(0, 8);
            if (gate[i] < total)
                return tier[i];
        }
    }

    inline int distanceScore(const QuestRomTables &t, int c1, int c2) // GetQuestCategoryDistanceScore
    {
        auto norm = [](int c)
        { return c == 0x1b ? 9 : c == 0x15 ? 1
                             : c > 9       ? 8
                                           : c; };
        return t.distance[norm(c1)][norm(c2)] + (c1 == 0x1b) + (c2 == 0x1b);
    }

    inline int pickWeighted(const QuestRomTables &t, const RandFn &rnd) // PickWeightedRandomItem
    {
        int total = 0;
        for (const auto &e : t.itemWeights)
            total += e.second;
        const int r = rnd(0, total);
        int acc = 0;
        for (const auto &e : t.itemWeights)
            if (r < (acc += e.second))
                return e.first;
        return r;
    }

    inline QuestEntry pickBonus(const QuestWorld &w, const RandFn &rnd) // PickRandomRewardItem
    {
        return {1, pickWeighted(w.rom, rnd), 0};
    }

    // PickMode2/3/6/7RewardItem. rnd(lo, hi) is hi-exclusive, like RandRange.
    inline int pickModeItem(int mode, int species, const RandFn &rnd)
    {
        switch (mode)
        {
        case 2:
        {
            static constexpr int items[6] = {0x59, 0x4f, 0x54, 0x4c, 0x56, 0x50};
            const int r = rnd(1, 0xb); // 1..10
            if (r <= 6)
                return items[r - 1];
            if (r <= 8)
                return 0x4e;
            return species == 0 ? 0x57 : species == 1 ? 0x58
                                                      : 0x51;
        }
        case 3:
        {
            static constexpr int items[5] = {0xc9, 0xcb, 0xca, 0xdc, 0xde};
            return items[rnd(1, 6) - 1];
        }
        case 6:
        {
            static constexpr int items[14] = {0x29, 0x29, 0x29, 0x2a, 0x2a, 0x2a, 0x1e,
                                              0x1f, 0x2b, 0x20, 0x2e, 0x2f, 0x1a, 0x28};
            return items[rnd(1, 0xf) - 1];
        }
        case 7:
        {
            static constexpr int items[4] = {0x101, 0x102, 0x103, 0x104};
            return items[rnd(1, 5) - 1];
        }
        default:
            return 0xcd;
        }
    }

    // PickQuestRewardItem. w2: 1 = usually buried, 0 = visible.
    inline QuestEntry pickObjective(const QuestDef &d, const QuestWorld &w, const RandFn &rnd)
    {
        QuestEntry e;
        auto mi = [&](int mode, int sp)
        { return w.modeItem ? w.modeItem(mode, sp) : pickModeItem(mode, sp, rnd); };
        switch (d.typeMode)
        {
        case 6:
            e.item = mi(6, 0);
            e.count = rnd(1, 3);
            return e;
        case 2:
            e.item = mi(2, w.speciesOf(d.receiver));
            e.count = 1;
            return e;
        case 3:
            e.item = mi(3, 0);
            e.count = rnd(2, 6);
            return e;
        case 7:
            e.item = mi(7, 0);
            e.count = rnd(3, 7);
            return e;
        default:
            break;
        }
        static constexpr int items[16] = {0xcd, 0xce, 0xcc, 0xc9, 0xcb, 0xca, 0xcf, 0xd0,
                                          0xd5, 0xd7, 0xd8, 0xd9, 0xda, 0xdc, 0xde, 0xd1};
        static constexpr int buried[16] = {1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1};
        const int r = rnd(1, 0x15);
        if (r <= 16)
        {
            e.item = items[r - 1];
            e.w2 = buried[r - 1];
        }
        else
        {
            const bool one = rnd(1, 3) == 1;
            if (w.playerSpecies == 0)
            {
                e.item = one ? 0xd4 : 0xdd;
                e.w2 = one ? 1 : 0;
            }
            else if (w.playerSpecies == 1)
            {
                e.item = one ? 0xd3 : 0xd6;
                e.w2 = 0;
            }
            else
            {
                e.item = one ? 0xd2 : 0xdb;
                e.w2 = one ? 0 : 1;
            }
        }
        e.count = rnd(2, 6);
        return e;
    }

    inline bool itemUsed(const std::vector<QuestEntry> &v, int item)
    {
        for (const auto &e : v)
            if (e.item == item)
                return true;
        return false;
    }

    // RollNextQuest. total = the counter of the quest being generated.
    inline QuestDef rollNextQuest(int total, int prevTarget, int prevMode, const std::vector<QuestDef> &scripted,
                                  const QuestWorld &w, const RandFn &rnd)
    {
        QuestDef d;
        d.generic = true;
        int nextGiver = 0x4e; // giver of the next scripted quest: the receiver may not be them
        for (const auto &r : scripted)
            if (r.id == total + 1)
            {
                nextGiver = r.giver;
                break;
            }

        do
            d.giver = pickNpc(total, w, rnd);
        while (d.giver == prevTarget);
        d.typeMode = prevMode;
        do
        {
            const int r = rnd(1, 10);
            if ((r == 1 && total > 3) || r == 2)
            {
                do
                {
                    do
                        d.receiver = pickNpc(total, w, rnd);
                    while (d.receiver == d.giver);
                } while (w.stationary(d.receiver));
                d.typeMode = r == 1 ? 2 : 3;
            }
            else if (r == 3)
            {
                d.receiver = d.giver;
                d.typeMode = 1;
            }
            else if (r == 4 && total > 3)
            {
                d.receiver = d.giver;
                d.typeMode = 6;
            }
            else if (r == 5)
            {
                d.typeMode = prevMode;
                if (prevTarget != 0x48 && nextGiver != 0x48)
                {
                    d.giver = d.receiver = 0x48;
                    d.typeMode = 7;
                }
            }
            else
            {
                d.receiver = rnd(1, 3) == 1 ? d.giver : pickNpc(total, w, rnd);
                d.typeMode = 0;
            }
            if (nextGiver == d.receiver)
                d.typeMode = prevMode;
        } while (d.typeMode == prevMode);

        if (d.typeMode == 2)
            d.spawn = {{1, 0x109, 0}};
        else if (d.typeMode == 6)
            d.spawn = {{1, 0x10a, 0}};

        int n;
        if (d.typeMode == 2)
            n = 1;
        else if (d.typeMode == 6)
            n = rnd(2, 4);
        else if (total < 0x23)
            n = rnd(1, 3);
        else if (total <= 0x45)
            n = rnd(1, 4);
        else
            n = rnd(2, 4);

        if (d.typeMode == 2 || d.typeMode == 6)
            d.areaTier = 1;
        else if (d.typeMode == 1)
            do
                d.areaTier = progressTier(total, rnd);
            while (unsigned(d.areaTier - 5) < 2u); // unsigned compare assumed
        else
            d.areaTier = progressTier(total, rnd);

        for (int k = 0; k < n; ++k)
        {
            QuestEntry e;
            int tries = 0;
            do
                e = pickObjective(d, w, rnd);
            while (itemUsed(d.objectives, e.item) && ++tries < 64);
            if (itemUsed(d.objectives, e.item))
                break; // no distinct item available (placeholder modeItem)
            d.objectives.push_back(e);
        }

        if (d.typeMode == 1)
        {
            int sum = 0;
            for (const auto &o : d.objectives)
                sum += o.count;
            int t = sum < 4 ? sum * 30 + 75 : sum * 30 + 30;
            t += 30 * distanceScore(w.rom, w.areaTier(d.giver), d.areaTier);
            t += 30 * distanceScore(w.rom, d.areaTier, w.areaTier(d.receiver));
            t += rnd(-20, 21);
            d.timeLimit = t;
        }

        const int m = total < 0x23 ? 1 : total <= 0x45 ? rnd(1, 3)
                                                       : rnd(2, 4);
        for (int k = 0; k < m; ++k)
        {
            QuestEntry e;
            int tries = 0;
            do
                e = pickBonus(w, rnd);
            while (itemUsed(d.bonus, e.item) && ++tries < 64);
            if (itemUsed(d.bonus, e.item))
                break;
            d.bonus.push_back(e);
        }
        return d;
    }

    struct ScatterItem
    {
        int item, x, y;
        bool flagged;
    };

    // SpawnScatteredItemCluster (bonus list: persist 0) and SpawnQuestItemsInWorld (spawn list: persist 1).
    // Both are placed with category = 1, state visible.
    inline std::vector<ScatterItem> scatter(const std::vector<QuestEntry> &list, bool questItems,
                                            int camX, int camY, int npcY, const RandFn &rnd)
    {
        std::vector<ScatterItem> out;
        int x = camX + rnd(10, 0x1a);
        int y = std::max(camY, npcY) + (questItems ? 0x14 : rnd(0x12, 0x17));
        for (size_t k = 0; k < list.size() && k < 3; ++k)
        {
            out.push_back({list[k].item, x, y, questItems});
            y += questItems ? 0xf : rnd(7, 0xb);
            const int dx = rnd(0xf, 0x1f);
            x += x < camX ? dx : -dx;
        }
        return out;
    }
}