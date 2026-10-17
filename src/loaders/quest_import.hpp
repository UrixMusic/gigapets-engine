#pragma once
#include <cstdint>
#include <vector>
#include "../game/quest.hpp"
#include "../game/quest_gen.hpp"

namespace gp
{
    struct QuestRecordLayout
    {
        int id = 0, typeMode = 1, giver = 2, receiver = 3, offer = 4, completion = 5,
            timeLimit = 6, spawn = 7, objectives = 8, bonus = 9, areaTier = 10;
    };

    template <class Word>
    std::vector<int> readTextIds(const Word &w, uint32_t p)
    {
        std::vector<int> v;
        if (p == 0) return v;
        for (int i = 0; i < 64; ++i)
        {
            const int x = int16_t(w(p + i));
            if (x == -1) break;
            v.push_back(x);
        }
        return v;
    }

    template <class Word>
    std::vector<QuestEntry> readEntries(const Word &w, uint32_t p)
    {
        std::vector<QuestEntry> v;
        if (p == 0) return v;
        for (int i = 0; i < 16; ++i, p += 3)
        {
            const int c = int16_t(w(p));
            if (c == -1) break;
            v.push_back({c, int(w(p + 1)), int(w(p + 2))});
        }
        return v;
    }

    template <class Word>
    std::vector<QuestDef> importQuestTable(const Word &w, uint32_t base = 0xc8ea, int count = 0x27,
                                           const QuestRecordLayout &L = {})
    {
        std::vector<QuestDef> out;
        for (int i = 0; i < count; ++i)
        {
            const uint32_t r = base + uint32_t(i) * 11;
            QuestDef d;
            d.id = w(r + L.id);
            d.typeMode = w(r + L.typeMode);
            d.giver = w(r + L.giver);
            d.receiver = w(r + L.receiver);
            d.offerText = readTextIds(w, w(r + L.offer));
            d.completionText = readTextIds(w, w(r + L.completion));
            d.timeLimit = w(r + L.timeLimit);
            d.spawn = readEntries(w, w(r + L.spawn));
            d.objectives = readEntries(w, w(r + L.objectives));
            d.bonus = readEntries(w, w(r + L.bonus));
            d.areaTier = w(r + L.areaTier);
            out.push_back(std::move(d));
        }
        return out;
    }

    // Item-weight table (0x43 entries) and the 10x10 category distance matrix.
    template <class Word>
    QuestRomTables importQuestTables(const Word &w)
    {
        QuestRomTables t;
        for (int i = 0; i < 0x43; ++i) t.itemWeights.push_back({int(w(0xc4c5 + 2 * i)), int(w(0xc4c6 + 2 * i))});
        for (int a = 0; a < 10; ++a)
            for (int b = 0; b < 10; ++b) t.distance[a][b] = int16_t(w(0xe339 + a * 10 + b));
        return t;
    }
}