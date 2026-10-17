#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include "quest.hpp"

namespace gp
{
    // Everything the builders need from the engine. All of it is filled in walk.cpp.
    struct QuestTextEnv
    {
        std::function<std::string(int)> npcName;       // GetQuestGiverNameTextId(pool), decoded
        std::function<std::string(int)> areaName;      // GetAreaNameTextId(area), decoded
        std::function<int(int)> npcTier;               // GetPoolEntryAreaUnlockTier(pool) = the NPC's area
        std::function<std::string(int, int)> itemName; // GetItemNamePluralized(item, count); count < 2 = singular
        RandFn rnd;                                    // rnd(lo, hi): hi exclusive
    };

    // GetAreaNamePreposition (strings at 0xe32a / 0xe32f / 0xe334).
    inline const char *areaPrep(int tier)
    {
        if (tier == 4 || tier == 2 || tier == 6)
            return " on ";
        if (tier == 3 || tier == 9)
            return " at ";
        return " in ";
    }

    // GetIllnessDescriptionText: keyed by the medicine's item id.
    inline std::string illnessDesc(int item, const RandFn &rnd)
    {
        switch (item)
        {
        case 0x59:
            return "is molting";
        case 0x4f:
            return "has brain freeze";
        case 0x54:
            return "has cold feet";
        case 0x4c:
            return "has brain ache";
        case 0x56:
            return "has the oogies";
        case 0x50:
            return "has cooties";
        case 0x4e:
            return rnd(1, 3) == 1 ? "has a fever" : "has a cold";
        case 0x57:
            return "has dog breath";
        case 0x58:
            return "has gone bananas";
        default:
            return "has panda pox";
        }
    }

    // GetQuestCompleteDialogue
    inline std::string completeText(int typeMode, const RandFn &rnd)
    {
        const int r = rnd(1, 4);
        switch (typeMode)
        {
        case 2:
            return "Oh, thanks!";
        case 3:
            return "How nice! Thank you!";
        case 6:
            return "Mmmmm! Thank you very much.";
        case 7:
            return "Great! The area you cleaned up looks so nice now. Thanks!";
        default:
            break;
        }
        return r == 1   ? "Thank you for bringing me these items! Here is your reward."
               : r == 2 ? "Let's see... Yes! You have found the items I need. Thanks!"
                        : "You found everything! Here is your reward for helping me.";
    }

    // GetQuestRequestDialogue (the generic offer text)
    inline std::string requestText(const QuestDef &d, const QuestTextEnv &e)
    {
        switch (d.typeMode)
        {
        case 2:
        {
            const int item = d.objectives.empty() ? 0 : d.objectives[0].item;
            return e.npcName(d.receiver) + " " + illnessDesc(item, e.rnd) + " and really needs " + e.itemName(item, 1) + ".";
        }
        case 6:
            return "I'm so hungry. I'm looking for someone to get my lunch.";
        case 7:
            return "There is a lot of litter around " + e.areaName(d.areaTier) + " that needs to be picked up.";
        case 3:
        {
            const std::string n = e.npcName(d.receiver);
            const int r = e.rnd(1, 4);
            return r == 1   ? "I heard " + n + " is sad and needs some cheering up."
                   : r == 2 ? n + " seems sad."
                            : "I'd really like to try to cheer up " + n + ".";
        }
        default:
            break;
        }
        const int r = e.rnd(1, 4);
        std::string s = r == 1   ? "I would be grateful if you would help me find"
                        : r == 2 ? "I really need"
                                 : "I need someone to find me";
        const size_t n = std::min<size_t>(d.objectives.size(), 3);
        for (size_t k = 0; k < n; ++k)
        {
            const QuestEntry &o = d.objectives[k];
            if (k == 0)
                s += " ";
            else if (k + 1 >= n)
                s += " and ";
            else
                s += o.count <= int(k) + 1 ? " " : ", "; // ported literally from the decompile; looks like a vanilla quirk
            s += std::to_string(o.count) + " " + e.itemName(o.item, o.count);
        }
        return s + ".";
    }

    // BuildNextQuestHintMessage. giver = g_questTargetId (the NEXT quest's giver).
    inline std::string hintText(int giver, const QuestTextEnv &e)
    {
        const std::string name = e.npcName(giver);
        const int tier = e.npcTier(giver);
        const std::string area = e.areaName(tier);
        const int r = e.rnd(1, 4);
        if (r == 1)
            return "I hear that " + name + " may need some help. Find " + name + areaPrep(tier) + area + ".";
        if (r == 2)
            return "If you want to help some more, go to " + area + " and find " + name + ".";
        return name + areaPrep(tier) + area + " was looking for some help.";
    }

    // BuildQuestStatusText (stats screen). Returns the pool id to show a portrait for, -1 = text only.
    inline int questStatusText(const QuestState &q, const QuestTextEnv &e,
                               const std::function<int(int)> &held, std::string &out)
    {
        auto where = [&](int npc)
        {
            const int t = e.npcTier(npc);
            return "Find " + e.npcName(npc) + areaPrep(t) + e.areaName(t);
        };
        out.clear();
        if (q.phase == 0)
        {
            out = where(q.def.giver) + " for your next quest.";
            return q.def.giver;
        }
        const size_t n = std::min<size_t>(q.def.objectives.size(), 3);
        bool missing = false;
        for (size_t k = 0; k < n; ++k)
            if (held(q.def.objectives[k].item) < q.def.objectives[k].count)
                missing = true;
        if (!missing)
        {
            out = where(q.def.receiver) + " to complete your quest.";
            return q.def.receiver;
        }
        out = "Find";
        for (size_t k = 0; k < n; ++k)
        {
            const QuestEntry &o = q.def.objectives[k];
            out += k == 0 ? " " : k + 1 >= n ? " and "
                                             : ", ";
            out += std::to_string(o.count) + " " + e.itemName(o.item, o.count);
        }
        out += ".";
        return -1;
    }
}