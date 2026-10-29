#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <map>
#include "frontend.hpp" // starterKit
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

    // Make sure p.questsCompleted == quests.counter before calling this.
    inline void savePlayer(const Player &p, SaveData &s)
    {
        s.setStr("name", p.name);
        s.setInt("questsCompleted", p.questsCompleted);
        s.setInt("money", p.money);
        s.setInt("species", p.species);
        s.setInt("colour", p.baseColour);
        s.setInt("appearance", p.appearance);
        s.setInt("illness", p.illnessId);
        s.setInt("tricksKnown", p.tricksKnown);
        if (p.trickCodeRolled)
            s.setList("trickCode", std::vector<int>(p.trickCode.begin(), p.trickCode.end()));
        s.setList("personality", std::vector<int>(p.personality.begin(), p.personality.end()));
        s.setList("care", std::vector<int>(p.care.begin(), p.care.end()));
        s.setIdList("friendIds", p.friended); // string ids, so mod NPCs survive a save
        s.setIdMap("itemIds", p.inventory);
        s.setIdList("flagIds", p.flags);
        s.setMap("equip", p.equip);
        s.setInt("tutorialsShown", int(p.tutorialsShown & 0xffffu));
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
            p.money = std::clamp(s.getInt("money"), 0, kMaxMoney);
        if (s.has("illness"))
            p.illnessId = std::clamp(s.getInt("illness", kHealthy), 0, kHealthy);
        if (s.has("tricksKnown"))
            p.tricksKnown = std::max(0, s.getInt("tricksKnown"));
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
            p.species = std::clamp(s.getInt("species"), 0, 2);
        if (s.has("colour"))
            p.baseColour = s.getInt("colour");
        p.appearance = s.has("appearance") ? s.getInt("appearance") : p.baseColour;
        const std::vector<int> per = s.getList("personality"), care = s.getList("care");
        for (size_t i = 0; i < per.size() && i < p.personality.size(); ++i)
            p.personality[i] = std::clamp(per[i], 0, kMaxPersonality);
        for (size_t i = 0; i < care.size() && i < p.care.size(); ++i)
            p.care[i] = std::clamp(care[i], 0, kMaxCare);

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
        // Saves from before homes had furnishings start with what CreateNewPet gives that species.
        p.equip = s.has("equip") ? s.getMap("equip") : starterEquip(p.species);
        p.tutorialsShown = s.has("tutorialsShown") ? unsigned(s.getInt("tutorialsShown")) & 0xffffu : 0u;
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