// game/item_use.hpp
// Port of the "use an item from the menu" logic: ItemMenuScreen (mode 0, food/medicine/potion
// branches) and ApplyItemCategoryEffect (0x02dc3e). Pure state change on Player, no SDL, no UI.
// Dialogue, sounds, saving and the potion recolor lookup stay with the caller.
#pragma once
#include <algorithm>
#include <string>
#include "item.hpp" // ItemDef / ItemType (adjust to wherever your item header lives)
#include "player.hpp"

namespace gp
{
    constexpr int kFoodForMiniPets = 8;        // w9 == 8: "This food is for mini pets", no effect
    constexpr int kFoodSpecial = 7;            // w9 == 7: per-item overrides, ids 0x35..0x3c
    constexpr int kMedicineNoIllness = 0xffff; // w9 == -1 on medicine: opens an empty dialogue

    // ItemDef::index -> the string id used in Player::inventory. Change this one function if
    // your ContentDB names items differently.
    inline std::string itemKey(int index) { return "base:item/" + std::to_string(index); }

    // Hook for the pieces that live outside Player. Defaults do nothing.
    struct ItemUseHost
    {
        virtual ~ItemUseHost() = default;
        // AddToStatCounter(counterId, amount). Personality stat s uses counter 2 + 4*s.
        virtual void addStatCounter(int /*counterId*/, int /*amount*/) {}
    };

    enum class ItemUseResult
    {
        NotOwned,      // nothing happened
        NeedsDialogue, // caller shows the item's dialogue (decor types 1-8, TreasureMap, blocked cases)
        Ate,           // food eaten, one removed (menu result 2)
        TookMedicine,  // one removed (menu result 3); illness id is ItemDef::w9
        UsedPotion,    // not consumed here (menu result 4); caller does the 0xb061 recolor lookup
        Nothing        // PickupBag and unhandled types
    };

    namespace detail
    {
        inline int personalityCounter(PersonalityStat s) { return 2 + 4 * static_cast<int>(s); }

        // IDs 0x35..0x3a raise one personality stat by min(20, 9999 - current).
        // IDs 0x37, 0x38, 0x3b, 0x3c lower one by min(current, 10).
        // The stat index is inferred from the AdjustPersonalityStat arguments (the decompiler
        // dropped GetPersonalityStat's parameter, so it is assumed to be the same index).
        inline void applySpecialFood(Player &p, int id, ItemUseHost &host)
        {
            // TODO(verify in MAME): AdjustCareStat(0) with its arguments lost; the notes call this
            // "full restore", so hunger is filled here.
            p.adjustCare(Hunger, kMaxCare);

            PersonalityStat stat;
            bool raise;
            switch (id)
            {
            case 0x35:
                stat = Halos;
                raise = true;
                break;
            case 0x36:
                stat = Pitchforks;
                raise = true;
                break;
            case 0x39:
                stat = Stars;
                raise = true;
                break;
            case 0x3a:
                stat = Lightbulbs;
                raise = true;
                break;
            case 0x3c:
                stat = Halos;
                raise = false;
                break;
            case 0x3b:
                stat = Pitchforks;
                raise = false;
                break;
            case 0x38:
                stat = Stars;
                raise = false;
                break;
            case 0x37:
                stat = Lightbulbs;
                raise = false;
                break;
            default:
                return;
            }
            const int cur = p.personality[stat];
            const int delta = raise ? std::min(20, kMaxPersonality - cur) : -std::min(cur, 10);
            p.adjustPersonality(stat, delta);
            host.addStatCounter(personalityCounter(stat), delta);
        }
    } // namespace detail

    // ApplyItemCategoryEffect. Care stats: Hunger, Health, Happiness.
    inline void applyFoodEffect(Player &p, const ItemDef &d, ItemUseHost &host)
    {
        switch (d.w9)
        {
        case 1:
            p.adjustCare(Hunger, 10);
            p.adjustCare(Happiness, 6);
            p.adjustCare(Health, -6);
            break;
        case 2:
            p.adjustCare(Hunger, 10);
            p.adjustCare(Happiness, 3);
            p.adjustCare(Health, -3);
            break;
        case 3:
            p.adjustCare(Hunger, 3);
            p.adjustCare(Happiness, 6);
            break;
        case 4:
            p.adjustCare(Hunger, 10);
            p.adjustCare(Happiness, 6);
            break;
        case 5:
            p.adjustCare(Hunger, 10);
            p.adjustCare(Health, 6);
            break;
        case kFoodSpecial:
            detail::applySpecialFood(p, d.index, host);
            break;
        default:
            break; // categories 0 and 6: decompiler lost the AdjustCareStat arguments (TODO)
        }
    }

    // GetIllnessItemCure (decompile traced by hand): illness id -> item id.
    inline int illnessCureItem(int illness)
    {
        switch (illness)
        {
        case 0:
            return 0x57;
        case 1:
            return 0x51;
        case 3:
            return 0x4e;
        case 4:
            return 0x59;
        case 5:
            return 0x4f;
        case 6:
            return 0x54;
        case 7:
            return 0x4c; // Aspirin, Brain Ache
        case 8:
            return 0x56;
        case 9:
            return 0x50;
        default:
            return 0x58; // illness 2 (Panda Pox), and also 0xa; the original has no explicit case for it
        }
    }

    // Cures when the medicine matches the current illness. Returns true if the pet was cured.
    inline bool applyMedicine(Player &p, const ItemDef &d)
    {
        if (p.illnessId != d.w9)
            return false;
        p.illnessId = kHealthy;
        return true;
    }

    // Menu mode 0 ("use"). Removes the item itself when the original does.
    inline ItemUseResult useItem(Player &p, const ItemDef &d, ItemUseHost &host)
    {
        const std::string key = itemKey(d.index);
        if (p.itemCount(key) <= 0)
            return ItemUseResult::NotOwned;

        switch (static_cast<ItemType>(d.type))
        {
        case ItemType::Food:
            if (d.w9 == kFoodForMiniPets)
                return ItemUseResult::NeedsDialogue;
            applyFoodEffect(p, d, host);
            p.removeItem(key, 1);
            return ItemUseResult::Ate;

        case ItemType::Medicine:
            if (d.w9 == kMedicineNoIllness)
                return ItemUseResult::NeedsDialogue;
            p.removeItem(key, 1);
            return ItemUseResult::TookMedicine;

        case ItemType::Potion:
            return ItemUseResult::UsedPotion;

        case ItemType::TreasureMap:
            return ItemUseResult::NeedsDialogue; // text id is d.w9

        case ItemType::PickupItem:
            return ItemUseResult::Nothing;

        default:
            // Types 1-8 (wall..toy) open the place/decor dialogue.
            if (d.type >= 1 && d.type <= 8)
                return ItemUseResult::NeedsDialogue;
            return ItemUseResult::Nothing;
        }
    }
} // namespace gp
