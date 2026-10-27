#pragma once
#include <vector>
#include "../content/records.hpp"

namespace gp
{
    // The base game's quest types, as data. Nothing here reads the ROM.
    // Names for the mode numbers are guesses except where the old code said so (1 timed, 2 sick NPC, 6 lunch, 7 litter).
    inline std::vector<QuestTypeDef> baseQuestTypes()
    {
        using Var = QuestPoolEntry::Variant;
        auto one = [](int item, int buried = 0, int weight = 1)
        {
            QuestPoolEntry e;
            e.item = legacyItemId(item);
            e.buried = buried;
            e.weight = weight;
            return e;
        };
        auto group = [](int weight, std::vector<Var> v)
        {
            QuestPoolEntry e;
            e.weight = weight;
            e.bySpecies = std::move(v);
            return e;
        };
        auto var = [](int species, int item, int buried = 0, int altItem = -1, int altBuried = 0)
        {
            Var v;
            v.species = species;
            v.item = legacyItemId(item);
            v.buried = buried;
            if (altItem >= 0)
                v.altItem = legacyItemId(altItem);
            v.altBuried = altBuried;
            return v;
        };
        auto mk = [](int mode)
        {
            QuestTypeDef t;
            t.mode = mode;
            t.id = "base:questtype/" + std::to_string(mode);
            return t;
        };
        std::vector<QuestTypeDef> out;

        { // 0: the default errand. Its pool, counts and bonus rules are what other types inherit.
            QuestTypeDef t = mk(0);
            static const int items[16] = {0xcd, 0xce, 0xcc, 0xc9, 0xcb, 0xca, 0xcf, 0xd0,
                                          0xd5, 0xd7, 0xd8, 0xd9, 0xda, 0xdc, 0xde, 0xd1};
            static const int buried[16] = {1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1};
            for (int i = 0; i < 16; ++i)
                t.pool.push_back(one(items[i], buried[i]));
            // species-specific item: 1 in 2 gets the first, else the second
            t.pool.push_back(group(4, {var(0, 0xd4, 1, 0xdd, 0), var(1, 0xd3, 0, 0xd6, 0), var(-1, 0xd2, 0, 0xdb, 1)}));
            t.countLo = 2;
            t.countHi = 5;
            t.objectiveCount = {{0, 1, 2}, {0x23, 1, 3}, {0x46, 2, 3}};
            t.bonusCount = {{0, 1, 1}, {0x23, 1, 2}, {0x46, 2, 3}};
            out.push_back(std::move(t));
        }
        { // 1: timed, receiver is the giver
            QuestTypeDef t = mk(1);
            t.timed = true;
            t.excludedTiers = {5, 6};
            t.rollLo = t.rollHi = 3;
            t.receiver = QuestReceiver::Self;
            out.push_back(std::move(t));
        }
        { // 2: sick NPC
            QuestTypeDef t = mk(2);
            for (int i : {0x59, 0x4f, 0x54, 0x4c, 0x56, 0x50})
                t.pool.push_back(one(i));
            t.pool.push_back(one(0x4e, 0, 2));
            t.pool.push_back(group(2, {var(0, 0x57), var(1, 0x58), var(-1, 0x51)}));
            t.speciesFromReceiver = true;
            t.countLo = t.countHi = 1;
            t.objectiveCount = {{0, 1, 1}};
            t.spawn = {QuestEntry{1, legacyItemId(0x109), 0}};
            t.fixedAreaTier = 1;
            t.spawnsOnAccept = false;
            t.reminderKey = "reminder_2";
            t.rollLo = t.rollHi = 1;
            t.rollMinTotal = 4;
            t.receiver = QuestReceiver::Other;
            out.push_back(std::move(t));
        }
        { // 3
            QuestTypeDef t = mk(3);
            for (int i : {0xc9, 0xcb, 0xca, 0xdc, 0xde})
                t.pool.push_back(one(i));
            t.countLo = 2;
            t.countHi = 5;
            t.reminderKey = "reminder_3";
            t.rollLo = t.rollHi = 2;
            t.receiver = QuestReceiver::Other;
            out.push_back(std::move(t));
        }
        for (int m : {4, 5}) // scripted only; the quest data carries its own spawns
        {
            QuestTypeDef t = mk(m);
            t.spawnsOnAccept = false;
            out.push_back(std::move(t));
        }
        { // 6: lunch
            QuestTypeDef t = mk(6);
            for (int i : {0x29, 0x29, 0x29, 0x2a, 0x2a, 0x2a, 0x1e, 0x1f, 0x2b, 0x20, 0x2e, 0x2f, 0x1a, 0x28})
                t.pool.push_back(one(i));
            t.countLo = 1;
            t.countHi = 2;
            t.objectiveCount = {{0, 2, 3}};
            t.spawn = {QuestEntry{1, legacyItemId(0x10a), 0}};
            t.fixedAreaTier = 1;
            t.spawnsOnAccept = false;
            t.reminderKey = "reminder_6";
            t.rollLo = t.rollHi = 4;
            t.rollMinTotal = 4;
            t.receiver = QuestReceiver::Self;
            out.push_back(std::move(t));
        }
        { // 7: litter, always NPC 0x48
            QuestTypeDef t = mk(7);
            for (int i : {0x101, 0x102, 0x103, 0x104})
                t.pool.push_back(one(i));
            t.countLo = 3;
            t.countHi = 6;
            t.reminderKey = "reminder_7";
            t.rollLo = t.rollHi = 5;
            t.receiver = QuestReceiver::Fixed;
            t.fixedNpc = 0x48;
            out.push_back(std::move(t));
        }
        return out;
    }
}