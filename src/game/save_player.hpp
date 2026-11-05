#pragma once
#include "../engine/tuning.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <map>
#include "frontend.hpp" // starterKit
#include "npc_anim_ids.hpp" // the species ids
#include "player.hpp"
#include "save.hpp"

namespace gp
{
    // Kept only for old callers. Saves no longer use it.
    inline int npcIndexOf(const std::string &id) // "base:npc/48" -> 48
    {
        const size_t p = id.rfind('/');
        return p == std::string::npos ? -1 : std::atoi(id.c_str() + p + 1);
    }

    // A save keeps a base species as its number and a mod's species as its id (its number depends on the mods that are loaded); the number
    // saved beside the id is 0, what a game without that mod falls back to.
    inline void saveSpecies(SaveData &s, int species)
    {
        s.setInt("species", species >= 0 && species <= 2 ? species : 0);
        if (species > 2 && knownSpecies(species))
            s.setStr("speciesId", artOf(species).id);
        else
            s.kv.erase("speciesId");
    }
    inline int loadSpecies(const SaveData &s)
    {
        const std::string id = s.getStr("speciesId");
        if (!id.empty())
            if (const int n = speciesNumber(id); n >= 0)
                return n;
        return std::clamp(s.getInt("species", 0), 0, 2);
    }

    // Make sure p.questsCompleted == quests.counter before calling this.
    inline void savePlayer(const Player &p, SaveData &s)
    {
        s.setStr("name", p.name);
        s.setInt("questsCompleted", p.questsCompleted);
        s.setInt("money", p.money);
        saveSpecies(s, p.species);
        s.setInt("colour", p.baseColour);
        s.setInt("appearance", p.appearance);
        s.setInt("illness", p.illnessId);
        s.setInt("tricksKnown", p.tricksKnown);
        s.setInt("trickMask", p.trickMask);
        if (p.trickCodeRolled)
            s.setList("trickCode", std::vector<int>(p.trickCode.begin(), p.trickCode.end()));
        s.setList("personality", std::vector<int>(p.personality.begin(), p.personality.end()));
        s.setList("care", std::vector<int>(p.care.begin(), p.care.end()));
        s.setIdList("friendIds", p.friended); // string ids, so mod NPCs survive a save
        s.setIdMap("itemIds", p.inventory);
        s.setIdList("flagIds", p.flags);
        s.setIdMap("counterIds", p.counters);
        s.setMap("equip", p.equip);
        s.setInt("tutorialsShown", int(p.tutorialsShown & 0xffffu));
        // The HUD objectives of scripts: the ids in order, then one "objective.<id>" = "text|item|need" each (ids, texts and
        // items cannot contain a space or a |). Old objective keys are cleared first so a cleared one does not linger.
        for (auto it = s.kv.begin(); it != s.kv.end();)
            it = it->first.rfind("objective.", 0) == 0 ? s.kv.erase(it) : std::next(it);
        s.kv.erase("objectives");
        if (!p.objectives.empty())
        {
            std::string order;
            for (const ScriptObjective &o : p.objectives)
            {
                order += (order.empty() ? "" : " ") + o.id;
                s.setStr("objective." + o.id, o.text + "|" + o.item + "|" + std::to_string(o.need));
            }
            s.setStr("objectives", order);
        }
    }

    // CreateNewPet's SetInventorySlot calls for this species: the home a new pet starts with.
    inline std::map<int, int> starterEquip(int species)
    {
        std::map<int, int> e;
        for (const auto &slotItem : fe::starterKit(species).equip)
            e[slotItem[0]] = slotItem[1];
        return e;
    }

    inline void loadPlayer(const SaveData &s, Player &p)
    {
        if (s.has("name"))
            p.name = s.getStr("name");
        if (s.has("questsCompleted"))
            p.questsCompleted = std::max(1, s.getInt("questsCompleted", 1)); // the ROM's counter starts at 1
        if (s.has("money"))
            p.money = std::clamp(s.getInt("money"), 0, tuning().maxMoney);
        if (s.has("illness"))
            p.illnessId = std::clamp(s.getInt("illness", kHealthy), 0, kHealthy);
        if (s.has("trickMask")) // (a save from before that has only the count: the first n tricks)
            p.setTrickMask(s.getInt("trickMask"));
        else if (s.has("tricksKnown"))
            p.setTricksInOrder(std::max(0, s.getInt("tricksKnown")));
        const std::vector<int> code = s.getList("trickCode");
        if (code.size() == p.trickCode.size())
        {
            for (size_t i = 0; i < code.size(); ++i)
                p.trickCode[i] = std::clamp(code[i], 0, 3);
            p.trickCodeRolled = true;
        }
        if (s.has("eventTriggers"))
            for (int n = 0; n < 32; ++n)
                if (s.getUInt("eventTriggers") >> n & 1u)
                    p.setTrigger(n);
        if (s.has("species"))
            p.species = loadSpecies(s);
        if (s.has("colour"))
            p.baseColour = s.getIntStrict("colour", p.baseColour);
        p.appearance = s.has("appearance") ? s.getIntStrict("appearance", p.baseColour) : p.baseColour;
        const std::vector<int> per = s.getList("personality"), care = s.getList("care");
        for (size_t i = 0; i < per.size() && i < p.personality.size(); ++i)
            p.personality[i] = std::clamp(per[i], 0, tuning().maxPersonality);
        for (size_t i = 0; i < care.size() && i < p.care.size(); ++i)
            p.care[i] = std::clamp(care[i], 0, tuning().maxCare);

        if (s.has("friendIds")) // v3
            p.friended = s.getIdList("friendIds");
        else if (s.has("friends")) // v2: NPC numbers
        {
            p.friended.clear();
            for (int i : s.getList("friends"))
                p.friended.insert("base:npc/" + std::to_string(i));
        }
        if (s.has("itemIds"))
            p.inventory = s.getIdMap("itemIds");
        if (s.has("flagIds"))
            p.flags = s.getIdList("flagIds");
        if (s.has("counterIds"))
            p.counters = s.getIdMap("counterIds");
        // Saves from before homes had furnishings start with what CreateNewPet gives that species.
        p.equip = s.has("equip") ? s.getMap("equip") : starterEquip(p.species);
        p.tutorialsShown = s.has("tutorialsShown") ? unsigned(s.getInt("tutorialsShown")) & 0xffffu : 0u;
        p.objectives.clear();
        if (s.has("objectives"))
        {
            const std::string order = s.getStr("objectives");
            size_t pos = 0;
            while (pos < order.size())
            {
                size_t end = order.find(' ', pos);
                if (end == std::string::npos)
                    end = order.size();
                const std::string id = order.substr(pos, end - pos);
                pos = end + 1;
                if (id.empty() || !s.has("objective." + id))
                    continue;
                const std::string v = s.getStr("objective." + id);
                const size_t a = v.find('|'), b = a == std::string::npos ? a : v.find('|', a + 1);
                if (b == std::string::npos)
                    continue;
                ScriptObjective o;
                o.id = id;
                o.text = v.substr(0, a);
                o.item = v.substr(a + 1, b - a - 1);
                o.need = std::clamp(std::atoi(v.c_str() + b + 1), 0, 99);
                p.setObjective(std::move(o));
            }
        }
    }

    inline void createNewSave(int slot, const std::string &name, int species, int colour)
    {
        Player p;
        p.name = name;
        p.species = species;
        p.baseColour = p.appearance = colour;
        p.questsCompleted = 1; // the ROM's counter starts at 1
        p.equip = starterEquip(species);
        SaveData s;
        savePlayer(p, s);
        s.setInt("version", 3);
        saveToFile(slotPath(slot), s);
    }
}