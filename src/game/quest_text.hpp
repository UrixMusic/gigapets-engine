#pragma once
#include <algorithm>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
#include "quest.hpp"

namespace gp
{
    // Replaces {name} with its value, in one pass (substituted values are not rescanned). Unknown {names} stay as written.
    inline std::string fillTemplate(const std::string &t, std::initializer_list<std::pair<const char *, std::string>> vars)
    {
        std::string out;
        for (size_t i = 0; i < t.size();)
        {
            if (t[i] == '{')
            {
                const size_t j = t.find('}', i);
                if (j != std::string::npos)
                {
                    const std::string key = t.substr(i + 1, j - i - 1);
                    bool hit = false;
                    for (const auto &v : vars)
                        if (key == v.first)
                        {
                            out += v.second;
                            hit = true;
                            break;
                        }
                    if (hit)
                    {
                        i = j + 1;
                        continue;
                    }
                }
            }
            out += t[i++];
        }
        return out;
    }

    // GetAreaNamePreposition: which string to use for an area tier.
    inline const char *prepKey(int tier)
    {
        if (tier == 4 || tier == 2 || tier == 6)
            return "prep_on";
        if (tier == 3 || tier == 9)
            return "prep_at";
        return "prep_in";
    }

    // Everything the builders need from the engine. All of it is filled in walk.cpp.
    struct QuestTextEnv
    {
        std::function<std::string(int)> npcName;  // GetQuestGiverNameTextId(pool), decoded
        std::function<std::string(int)> areaName; // GetAreaNameTextId(area), decoded
        std::function<int(int)> npcTier;          // GetPoolEntryAreaUnlockTier(pool) = the NPC's area
        std::function<std::string(const Id &, int)> itemName;
        std::function<std::string(const std::string &)> str; // quest string key -> text; EMPTY when there is no such string
        RandFn rnd;                                          // rnd(lo, hi): hi exclusive

        bool has(const std::string &key) const { return str && !str(key).empty(); }
        // A missing string shows as "(key)" so the gap is visible in game instead of an empty box.
        std::string s(const std::string &key) const
        {
            const std::string v = str ? str(key) : std::string();
            return v.empty() ? "(" + key + ")" : v;
        }
        std::string prep(int tier) const { return s(prepKey(tier)); }
    };

    // GetIllnessDescriptionText: keyed by the medicine's item number. Strings "illness_<hex>_<n>", n = 1, 2, ...;
    // several variants = a random pick. No strings for the item = "illness_default".

    // base:item/87 -> "57" (old hex form); mod ids are used as written
    inline std::string illnessKeyBase(const Id &id)
    {
        static const std::string pre = "base:item/";
        if (id.compare(0, pre.size(), pre) == 0)
        {
            int n = std::atoi(id.c_str() + pre.size());
            char buf[16];
            std::snprintf(buf, sizeof buf, "%x", n);
            return buf;
        }
        return id;
    }

    inline std::string illnessDesc(const Id &item, const QuestTextEnv &e)
    {
        std::vector<std::string> v;
        const std::string base = illnessKeyBase(item);
        for (int n = 1; n <= 8; ++n)
        {
            const std::string k = "illness_" + base + "_" + std::to_string(n);
            if (!e.has(k))
                break;
            v.push_back(e.str(k));
        }
        if (v.empty())
            return e.s("illness_default");
        if (v.size() == 1)
            return v[0];
        return v[size_t(e.rnd(1, int(v.size()) + 1)) - 1];
    }

    // GetQuestCompleteDialogue
    inline std::string completeText(int typeMode, const QuestTextEnv &e)
    {
        const int r = e.rnd(1, 4); // rolled even for modes 2/3/6/7, like the ROM
        switch (typeMode)
        {
        case 2:
        case 3:
        case 6:
        case 7:
            return e.s("complete_" + std::to_string(typeMode));
        default:
            break;
        }
        return e.s("complete_default_" + std::to_string(r)); // r is 1..3
    }

    // GetQuestRequestDialogue (the generic offer text)
    inline std::string requestText(const QuestDef &d, const QuestTextEnv &e)
    {
        switch (d.typeMode)
        {
        case 2:
        {
            Id item = d.objectives.empty() ? Id() : d.objectives[0].item;
            return fillTemplate(e.s("request_2"), {{"npc", e.npcName(d.receiver)},
                                                   {"illness", illnessDesc(item, e)},
                                                   {"item", e.itemName(item, 1)}});
        }
        case 6:
            return e.s("request_6");
        case 7:
            return fillTemplate(e.s("request_7"), {{"area", e.areaName(d.areaTier)}});
        case 3:
        {
            const std::string n = e.npcName(d.receiver);
            const int r = e.rnd(1, 4);
            return fillTemplate(e.s("request_3_" + std::to_string(r)), {{"npc", n}});
        }
        default:
            break;
        }
        const int r = e.rnd(1, 4);
        std::string list;
        const size_t n = std::min<size_t>(d.objectives.size(), 3);
        for (size_t k = 0; k < n; ++k)
        {
            const QuestEntry &o = d.objectives[k];
            if (k == 0)
                ;
            else if (k + 1 >= n)
                list += " " + e.s("list_and") + " ";
            else
                list += o.count <= int(k) + 1 ? " " : ", "; // ported literally from the decompile; looks like a vanilla quirk
            list += std::to_string(o.count) + " " + e.itemName(o.item, o.count);
        }
        std::string t = e.s("request_default_" + std::to_string(r));
        if (list.empty()) // no objectives (a placeholder case): drop the placeholder and the space before it
            if (const size_t p = t.find(" {list}"); p != std::string::npos)
                t.erase(p, 7);
        return fillTemplate(t, {{"list", list}});
    }

    // BuildNextQuestHintMessage. giver = g_questTargetId (the NEXT quest's giver).
    inline std::string hintText(int giver, const QuestTextEnv &e)
    {
        const std::string name = e.npcName(giver);
        const int tier = e.npcTier(giver);
        const std::string area = e.areaName(tier);
        const int r = e.rnd(1, 4);
        return fillTemplate(e.s("hint_" + std::to_string(r)), {{"npc", name}, {"prep", e.prep(tier)}, {"area", area}});
    }

    // BuildQuestStatusText (stats screen). Returns the pool id to show a portrait for, -1 = text only.
    inline int questStatusText(const QuestState &q, const QuestTextEnv &e,
                               const std::function<int(const Id &)> &held, std::string &out)
    {
        auto where = [&](int npc)
        {
            const int t = e.npcTier(npc);
            return fillTemplate(e.s("status_where"), {{"npc", e.npcName(npc)}, {"prep", e.prep(t)}, {"area", e.areaName(t)}});
        };
        out.clear();
        if (q.phase == 0)
        {
            out = fillTemplate(e.s("status_next"), {{"where", where(q.def.giver)}});
            return q.def.giver;
        }
        const size_t n = std::min<size_t>(q.def.objectives.size(), 3);
        bool missing = false;
        for (size_t k = 0; k < n; ++k)
            if (held(q.def.objectives[k].item) < q.def.objectives[k].count)
                missing = true;
        if (!missing)
        {
            out = fillTemplate(e.s("status_complete"), {{"where", where(q.def.receiver)}});
            return q.def.receiver;
        }
        std::string list;
        for (size_t k = 0; k < n; ++k)
        {
            const QuestEntry &o = q.def.objectives[k];
            if (k == 0)
                ;
            else if (k + 1 >= n)
                list += " " + e.s("list_and") + " ";
            else
                list += ", ";
            list += std::to_string(o.count) + " " + e.itemName(o.item, o.count);
        }
        out = fillTemplate(e.s("status_find"), {{"list", list}});
        return -1;
    }
}