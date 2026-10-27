// game/item_effects.hpp - what each base-game item does when used, as data. No Player, no SDL.
#pragma once
#include <string>
#include "item.hpp" // ItemType
#include "../content/records.hpp"

namespace gp
{
    constexpr int kFoodForMiniPets = 8;        // w9 == 8: "This food is for mini pets", no effect
    constexpr int kFoodSpecial = 7;            // w9 == 7: per-item overrides, ids 0x35..0x3c
    constexpr int kMedicineNoIllness = 0xffff; // w9 == -1 on medicine: opens an empty dialogue

    // Actions the base game uses. Params for "food" (all optional):
    //   num hunger / happiness / health : care stat deltas, applied in that order
    //   str stat                       : halos | pitchforks | stars | lightbulbs
    //   num raise / lower               : that stat goes up by at most `raise`, or down by at most `lower`
    // "medicine": num illness (the illness id it cures).
    // "potion", "dialogue" and "none" take no params.
    inline ItemUseDef legacyItemUse(const ItemDef &d)
    {
        ItemUseDef u;
        auto act = [&](const char *a)
        {
            u.action = a;
            return u;
        };
        switch (static_cast<ItemType>(d.type))
        {
        case ItemType::Food:
            if (d.w9 == kFoodForMiniPets)
                return act("dialogue");
            u.action = "food";
            switch (d.w9)
            {
            case 0:
                u.num = {{"hunger", 10}}; // ApplyItemCategoryEffect pushes (stat 0, amount 10) for the plain category
                break;
            case 1:
                u.num = {{"hunger", 10}, {"happiness", 6}, {"health", -6}};
                break;
            case 2:
                u.num = {{"hunger", 10}, {"happiness", 3}, {"health", -3}};
                break;
            case 3:
                u.num = {{"hunger", 3}, {"happiness", 6}};
                break;
            case 4:
                u.num = {{"hunger", 10}, {"happiness", 6}};
                break;
            case 5:
                u.num = {{"hunger", 10}, {"health", 6}};
                break;
            case 6:
                u.num = {{"health", 20}}; // AdjustCareStat(1, 0x14)
                break;
            case kFoodSpecial:
            {
                u.num["hunger"] = 3;
                // IDs 0x35..0x3a raise one personality stat (cap 20), 0x37, 0x38, 0x3b, 0x3c lower one (cap 10)
                struct Row
                {
                    int id;
                    const char *stat;
                    bool raise;
                };
                static const Row rows[] = {{0x35, "halos", true},       {0x36, "pitchforks", true},
                                           {0x39, "stars", true},       {0x3a, "lightbulbs", true},
                                           {0x3c, "halos", false},      {0x3b, "pitchforks", false},
                                           {0x38, "stars", false},      {0x37, "lightbulbs", false}};
                for (const Row &r : rows)
                    if (r.id == d.index)
                    {
                        u.str["stat"] = r.stat;
                        u.num[r.raise ? "raise" : "lower"] = r.raise ? 20 : 10;
                    }
                break;
            }
            default:
                break; // other categories do nothing
            }
            return u;
        case ItemType::Medicine:
            if (d.w9 == kMedicineNoIllness)
                return act("dialogue");
            u.action = "medicine";
            u.num["illness"] = d.w9;
            return u;
        case ItemType::Potion:
            return act("potion");
        case ItemType::TreasureMap:
            return act("dialogue"); // text id is d.w9
        case ItemType::PickupItem:
            return act("none");
        default:
            return act(d.type >= 1 && d.type <= 8 ? "dialogue" : "none"); // wall..toy open the place/decor dialogue
        }
    }
}