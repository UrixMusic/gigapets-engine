// game/player.hpp
// Who the player (the pet) is: progression data that NPCs, items and quests read and write.
// Position and movement live elsewhere (walk.cpp + player_input.hpp).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>

namespace gp
{

    // Index order matches the game's AdjustPersonalityStat / AdjustCareStat.
    enum PersonalityStat : int
    {
        Halos,
        Pitchforks,
        Stars,
        Lightbulbs,
        kNumPersonality
    };
    enum CareStat : int
    {
        Hunger,
        Health,
        Unknown2,
        Happiness,
        Tiredness,
        Cleanliness,
        kNumCare
    };

    constexpr int kHealthy = 10; // g_activeIllnessId: 0..9 illness, 10 = healthy
    constexpr int kMaxPersonality = 9999;
    constexpr int kMaxCare = 100;
    constexpr int kNegativeCareMinQuests = 4; // AdjustCareStat ignores negative deltas until quests > 3

    // with the other constants
    constexpr int kMaxMoney = 99999; // 0x1869f, AdjustPlayerMoney's cap

    inline std::string triggerKey(int n) { return "base:trigger/" + std::to_string(n); }

    struct Player
    {
        std::array<int, kNumPersonality> personality{};
        std::array<int, kNumCare> care{};
        int tricksKnown = 0; // g_tricksKnownCount
        int illnessId = kHealthy;
        int dialogueRotation = 0;       // DAT_000fff: one counter shared by all stationary NPCs
        std::string name = "Fluffypoo"; // GetPetName(): chosen at game start in the original; any default for now

        int species = 0;    // g_playerSpecies
        int baseColour = 0; // g_starterExtraField: rolled at starter pick, never changes
        int appearance = 0; // g_petDisplayAppearanceIdSaved: what's drawn; potion overwrites, wash resets

        void washPet() { appearance = baseColour; }

        int questsCompleted = 0;
        std::map<std::string, int> inventory; // item id -> count
        std::set<std::string> flags;          // event / quest flags

        // Befriending. NPC ids are strings like "base:npc/48".
        std::set<std::string> friended;    // g_npcFriendshipFlags (saved)
        std::set<std::string> encountered; // g_unlockActive (session only, no save call seen)
        uint32_t lastEncounterTick = 0;    // g_lastEncounterTick (0 = never)

        // player.hpp: replace the bit twiddling, delete the eventTriggers member
        bool hasTrigger(int n) const { return hasFlag(triggerKey(n)); }
        void setTrigger(int n) { setFlag(triggerKey(n)); }

        bool isFriended(const std::string &id) const { return friended.count(id) != 0; }
        void markFriended(const std::string &id) { friended.insert(id); } // MarkNPCFriended

        bool hasEncountered(const std::string &id) const { return encountered.count(id) != 0; }
        void setEncountered(const std::string &id) { encountered.insert(id); } // SetSpeciesDiscovered

        // ClearUnlockBitsForUnfriendedNPCs
        void clearEncounteredForUnfriended()
        {
            for (auto it = encountered.begin(); it != encountered.end();)
                it = isFriended(*it) ? std::next(it) : encountered.erase(it);
        }

        // FUN_03c9eb
        void resetFriendships()
        {
            friended.clear();
            encountered.clear();
        }

        // in Player
        int money = 0; // g_playerMoneyHi:Lo as one value

        // Port of AdjustPlayerMoney (player part only). Returns the change actually applied,
        // which is what AddToLifetimeEarnings should receive if you port it.
        // Not handled here: SaveMoneyToSlot, CheckStatsAndMoneyGate.
        int adjustMoney(int delta)
        {
            const int before = money;
            money = std::clamp(money + delta, 0, kMaxMoney);
            return money - before;
        }

        // Port of AdjustPersonalityStat (player part only). Changing halos or lightbulbs
        // clears "encountered" for every unfriended NPC, so they get a fresh intro.
        // Not handled here: SavePetStatsToSlot, the mini-pet mirror, CheckStatsAndMoneyGate.
        void adjustPersonality(PersonalityStat s, int delta)
        {
            personality[s] = std::clamp(personality[s] + delta, 0, kMaxPersonality);
            if (s == Halos || s == Lightbulbs)
                clearEncounteredForUnfriended();
        }

        // Port of AdjustCareStat.
        void adjustCare(CareStat s, int delta)
        {
            if (delta < 0 && questsCompleted < kNegativeCareMinQuests)
                return;
            care[s] = std::clamp(care[s] + delta, 0, kMaxCare);
        }

        bool isHealthy() const { return illnessId == kHealthy; }

        int itemCount(const std::string &id) const
        {
            auto it = inventory.find(id);
            return it == inventory.end() ? 0 : it->second;
        }
        void addItem(const std::string &id, int n = 1) { inventory[id] += n; }
        // in Player, next to addItem
        int removeItem(const std::string &id, int n = 1)
        {
            auto it = inventory.find(id);
            if (it == inventory.end())
                return 0;
            const int removed = std::min(n, it->second);
            it->second -= removed;
            if (it->second == 0)
                inventory.erase(it);
            return removed;
        }
        bool hasFlag(const std::string &f) const { return flags.count(f) != 0; }
        void setFlag(const std::string &f) { flags.insert(f); }
    };

} // namespace gp