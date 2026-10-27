#pragma once
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>
#include "quest.hpp"

namespace gp
{
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
        std::vector<QuestTypeDef> types; // from the db; see baseQuestTypes()

        // The type for a typeMode. Unknown modes behave like mode 0, as they always did.
        const QuestTypeDef &base() const { return type(0); }
        const QuestTypeDef &type(int mode) const
        {
            for (const auto &t : types)
                if (t.mode == mode)
                    return t;
            for (const auto &t : types)
                if (t.mode == 0)
                    return t;
            static const QuestTypeDef none;
            return none;
        }
    };

    inline int pickRange(int lo, int hi, const RandFn &rnd) { return hi > lo ? rnd(lo, hi + 1) : lo; }

    // The range with the largest fromTotal <= total.
    inline QuestCountRange rangeFor(const std::vector<QuestCountRange> &v, int total)
    {
        const QuestCountRange *best = nullptr;
        for (const auto &r : v)
            if (r.fromTotal <= total && (!best || r.fromTotal >= best->fromTotal))
                best = &r;
        return best ? *best : QuestCountRange{};
    }

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

    inline Id pickWeighted(const QuestRomTables &t, const RandFn &rnd) // PickWeightedRandomItem
    {
        int total = 0;
        for (const auto &e : t.itemWeights)
            total += e.second;
        const int r = rnd(0, total);
        int acc = 0;
        for (const auto &e : t.itemWeights)
            if (r < (acc += e.second))
                return e.first;
        return Id(); // unreachable: r < total
    }

    inline QuestEntry pickBonus(const QuestWorld &w, const RandFn &rnd) // PickRandomRewardItem
    {
        return {1, pickWeighted(w.rom, rnd), 0};
    }

    // Weighted draw from a type's pool: one rnd(1, totalWeight + 1), plus a coin flip when a variant has an alt item.
    inline QuestEntry pickFromPool(const std::vector<QuestPoolEntry> &pool, int species, const RandFn &rnd)
    {
        QuestEntry e;
        int total = 0;
        for (const auto &p : pool)
            total += p.weight;
        if (total <= 0)
            return e;
        const int r = rnd(1, total + 1);
        int acc = 0;
        const QuestPoolEntry *hit = &pool.back();
        for (const auto &p : pool)
            if (r <= (acc += p.weight))
            {
                hit = &p;
                break;
            }
        if (hit->bySpecies.empty())
        {
            e.item = hit->item;
            e.w2 = hit->buried;
            return e;
        }
        const QuestPoolEntry::Variant *v = nullptr;
        for (const auto &c : hit->bySpecies)
            if (c.species == species)
            {
                v = &c;
                break;
            }
        if (!v)
            for (const auto &c : hit->bySpecies)
                if (c.species < 0)
                {
                    v = &c;
                    break;
                }
        if (!v)
            v = &hit->bySpecies.back();
        if (!v->altItem.empty() && rnd(1, 3) != 1)
        {
            e.item = v->altItem;
            e.w2 = v->altBuried;
        }
        else
        {
            e.item = v->item;
            e.w2 = v->buried;
        }
        return e;
    }

    // PickQuestRewardItem. w2: 1 = usually buried, 0 = visible.
    inline QuestEntry pickObjective(const QuestDef &d, const QuestWorld &w, const RandFn &rnd)
    {
        const QuestTypeDef &t = w.type(d.typeMode);
        const bool own = !t.pool.empty();
        const QuestTypeDef &src = own ? t : w.base();
        const int sp = src.speciesFromReceiver ? w.speciesOf(d.receiver) : w.playerSpecies;
        QuestEntry e;
        if (own && w.modeItem) // test override
            e.item = legacyItemId(w.modeItem(d.typeMode, src.speciesFromReceiver ? sp : 0));
        else
            e = pickFromPool(src.pool, sp, rnd);
        e.count = pickRange(src.countLo, src.countHi, rnd);
        return e;
    }

    inline bool itemUsed(const std::vector<QuestEntry> &v, const Id &item)
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

        int domain = 10; // rnd(1, domain): rolls 1..9 in the base game; a mod type can widen it
        for (const auto &t : w.types)
            if (t.rollHi >= t.rollLo && t.rollHi + 1 > domain)
                domain = t.rollHi + 1;
        do
        {
            const int r = rnd(1, domain);
            const QuestTypeDef *rt = nullptr;
            for (const auto &t : w.types)
                if (t.rollHi >= t.rollLo && r >= t.rollLo && r <= t.rollHi && total >= t.rollMinTotal)
                {
                    rt = &t;
                    break;
                }
            if (!rt) // no type claims this roll: the base type with a random receiver
            {
                d.receiver = rnd(1, 3) == 1 ? d.giver : pickNpc(total, w, rnd);
                d.typeMode = w.base().mode;
            }
            else
                switch (rt->receiver)
                {
                case QuestReceiver::Other:
                    do
                    {
                        do
                            d.receiver = pickNpc(total, w, rnd);
                        while (d.receiver == d.giver);
                    } while (w.stationary(d.receiver));
                    d.typeMode = rt->mode;
                    break;
                case QuestReceiver::Self:
                    d.receiver = d.giver;
                    d.typeMode = rt->mode;
                    break;
                case QuestReceiver::Fixed:
                    d.typeMode = prevMode;
                    if (prevTarget != rt->fixedNpc && nextGiver != rt->fixedNpc)
                    {
                        d.giver = d.receiver = rt->fixedNpc;
                        d.typeMode = rt->mode;
                    }
                    break;
                case QuestReceiver::Random:
                    d.receiver = rnd(1, 3) == 1 ? d.giver : pickNpc(total, w, rnd);
                    d.typeMode = rt->mode;
                    break;
                }
            if (nextGiver == d.receiver)
                d.typeMode = prevMode;
        } while (d.typeMode == prevMode);

        const QuestTypeDef &t = w.type(d.typeMode);
        const QuestTypeDef &base = w.base();
        d.spawn = t.spawn;

        const QuestCountRange oc = rangeFor(t.objectiveCount.empty() ? base.objectiveCount : t.objectiveCount, total);
        const int n = pickRange(oc.lo, oc.hi, rnd);

        if (t.fixedAreaTier >= 0)
            d.areaTier = t.fixedAreaTier;
        else
            for (;;)
            {
                d.areaTier = progressTier(total, rnd);
                if (std::find(t.excludedTiers.begin(), t.excludedTiers.end(), d.areaTier) == t.excludedTiers.end())
                    break;
            }

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

        if (t.timed)
        {
            int sum = 0;
            for (const auto &o : d.objectives)
                sum += o.count;
            int tl = sum < 4 ? sum * 30 + 75 : sum * 30 + 30;
            tl += 30 * distanceScore(w.rom, w.areaTier(d.giver), d.areaTier);
            tl += 30 * distanceScore(w.rom, d.areaTier, w.areaTier(d.receiver));
            tl += rnd(-20, 21);
            d.timeLimit = tl;
        }

        const QuestCountRange bc = rangeFor(t.bonusCount.empty() ? base.bonusCount : t.bonusCount, total);
        const int m = pickRange(bc.lo, bc.hi, rnd);
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
        Id item;
        int x, y;
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