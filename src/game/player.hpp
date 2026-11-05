// game/player.hpp
// Who the player (the pet) is: progression data that NPCs, items and quests read and write.
// Position and movement live elsewhere (walk.cpp + player_input.hpp).
#pragma once
#include "../engine/tuning.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

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

    // A line a script put on the HUD with objective.set: `text` is a text id (may be empty), `item` an item id (may be empty) and
    // `need` how many of it to show a "have/need" counter for. Kept in the save, in the order they were added.
    struct ScriptObjective
    {
        std::string id, text, item;
        int need = 0;
    };

    struct Player
    {
        std::array<int, kNumPersonality> personality{};
        std::array<int, kNumCare> care{};
        int tricksKnown = 0; // g_tricksKnownCount: how many tricks it knows (always the number of bits in trickMask; set both with setTrickMask)
        int trickMask = 0;   // which tricks it knows: bit 0 = slot 1 (left) .. bit 2 (right). In the game they come in order (1, then 2, then 3); a mod may teach them in any order
        bool knowsTrick(int slot) const { return slot >= 0 && slot < 31 && (trickMask >> slot & 1) != 0; }
        void setTrickMask(int m)
        {
            trickMask = m < 0 ? 0 : m & 7;
            tricksKnown = (trickMask & 1) + (trickMask >> 1 & 1) + (trickMask >> 2 & 1);
        }
        void setTricksInOrder(int n) { setTrickMask((1 << std::clamp(n, 0, 3)) - 1); } // the first n tricks
        // g_petTraits: the Trick Teacher's secret code, 5 values 0..3. Not rolled until the first lesson is offered.
        std::array<int, 5> trickCode{};
        bool trickCodeRolled = false;
        // The reroll half of SetTricksKnownAndRerollTraits: RandRange(0, 4) five times. rr(lo, hi) has hi exclusive.
        template <class Rr>
        void rerollTrickCode(Rr &&rr)
        {
            for (int &c : trickCode)
                c = rr(0, 4);
            trickCodeRolled = true;
        }
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
        std::map<std::string, int> counters;  // numbers scripts keep (counter.get / add / set), and side quests count with
        std::vector<ScriptObjective> objectives; // what scripts show on the HUD (objective.set), oldest first
        static constexpr size_t kMaxObjectives = 8;
        const ScriptObjective *objective(const std::string &id) const
        {
            for (const ScriptObjective &o : objectives)
                if (o.id == id)
                    return &o;
            return nullptr;
        }
        // Adds, or replaces the one with the same id in place. False if the list is full.
        bool setObjective(ScriptObjective o)
        {
            for (ScriptObjective &e : objectives)
                if (e.id == o.id)
                {
                    e = std::move(o);
                    return true;
                }
            if (objectives.size() >= kMaxObjectives)
                return false;
            objectives.push_back(std::move(o));
            return true;
        }
        bool clearObjective(const std::string &id)
        {
            for (auto it = objectives.begin(); it != objectives.end(); ++it)
                if (it->id == id)
                {
                    objectives.erase(it);
                    return true;
                }
            return false;
        }
        // The furnishings of the player's home: inventory slot -> ROM item number (SetInventorySlot). Slot 0 is the
        // wall, 1 the floor, 2..4 wall pieces, 0x1b furniture, 0x1c the bed, and so on (game/room_scene.hpp).
        std::map<int, int> equip;
        // Which first-time tutorial messages have been shown, one bit per event (g_oneTimeEventsShown, per save slot).
        unsigned tutorialsShown = 0;

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

        // Every change to money, a personality stat or a care stat passes through this first (a mod's onStatChange seam): (kind, stat,
        // delta) -> the delta to apply. kind 0 = care, 1 = personality, 2 = money; stat is the CareStat / PersonalityStat number.
        std::function<int(int, int, int)> deltaFilter;

        // Port of AdjustPlayerMoney (player part only). Returns the change actually applied,
        // which is what AddToLifetimeEarnings should receive if you port it.
        // Not handled here: SaveMoneyToSlot, CheckStatsAndMoneyGate.
        int adjustMoney(int delta)
        {
            if (deltaFilter)
                delta = deltaFilter(2, 0, delta);
            const int before = money;
            money = std::clamp(money + delta, 0, tuning().maxMoney);
            pendingMoney += money - before; // AddToLifetimeEarnings: the stat-change panel reads and clears it (not saved)
            return money - before;
        }
        int pendingMoney = 0;

        // Port of AdjustPersonalityStat (player part only). Changing halos or lightbulbs
        // clears "encountered" for every unfriended NPC, so they get a fresh intro.
        // Not handled here: SavePetStatsToSlot, the mini-pet mirror, CheckStatsAndMoneyGate.
        void adjustPersonality(PersonalityStat s, int delta)
        {
            if (deltaFilter)
                delta = deltaFilter(1, int(s), delta);
            personality[s] = std::clamp(personality[s] + delta, 0, tuning().maxPersonality);
            if (s == Halos || s == Lightbulbs)
                clearEncounteredForUnfriended();
        }

        // Port of AdjustCareStat.
        void adjustCare(CareStat s, int delta)
        {
            if (deltaFilter)
                delta = deltaFilter(0, int(s), delta);
            if (delta < 0 && questsCompleted < tuning().negativeCareMinQuests)
                return;
            care[s] = std::clamp(care[s] + delta, 0, tuning().maxCare);
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