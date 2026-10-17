#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include "player.hpp"
#include "save.hpp"

namespace gp
{
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
        s.setUInt("eventTriggers", p.eventTriggers);
        s.setList("personality", std::vector<int>(p.personality.begin(), p.personality.end()));
        s.setList("care", std::vector<int>(p.care.begin(), p.care.end()));
        std::vector<int> friends;
        for (const std::string &id : p.friended)
            if (const int i = npcIndexOf(id); i >= 0)
                friends.push_back(i);
        s.setList("friends", friends);
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
        if (s.has("eventTriggers"))
            p.eventTriggers = s.getUInt("eventTriggers");
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
        if (s.has("friends"))
        {
            p.friended.clear();
            for (int i : s.getList("friends"))
                p.friended.insert("base:npc/" + std::to_string(i));
        }
    }

    inline void createNewSave(int slot, const std::string &name, int species, int colour)
    {
        Player p;
        p.name = name;
        p.species = species;
        p.baseColour = p.appearance = colour;
        p.questsCompleted = 1; // the ROM's counter starts at 1
        SaveData s;
        savePlayer(p, s);
        s.setInt("version", 2);
        saveToFile(slotPath(slot), s);
    }
}