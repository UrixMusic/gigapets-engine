// shops.hpp - the ROM's shop stock tables as records (ShopDef), so a mod can add to a shop, change one or open its own.
//
// The tables are at word 0xb303, 0x24 words a shop (the ROM's "catalogue index", see shopRowForArea), 12 entries of three words: item, the most of it a
// shop offers at once, weight. A zero item ends the list. (PickWeightedRandomItem 0x2ddfe sums the weights; ClampInventorySlotQuantities 0x2e381 reads the cap.)
#pragma once
#include "../content/db.hpp"
#include "../game/shop.hpp"
#include "rom.hpp"

namespace gp
{
    inline std::vector<ShopDef> baseShops(const Rom &rom)
    {
        std::vector<ShopDef> out;
        for (int area = 0; area < 0x26; ++area)
        {
            const int row = shopRowForArea(area);
            if (row < 0)
                continue;
            ShopDef s;
            s.id = "base:shop/" + std::to_string(area);
            s.area = "base:area/" + std::to_string(area);
            for (int k = 0; k < 12; ++k)
            {
                const uint32_t w = 0xb303u + uint32_t(row) * 0x24u + uint32_t(k) * 3u;
                const int item = int(rom.u16(w));
                if (item == 0)
                    break;
                s.items.push_back({legacyItemId(item), int(rom.u16(w + 2)), int(rom.u16(w + 1))});
            }
            out.push_back(std::move(s));
        }
        return out;
    }

    // The ROM's stock injections (ClaimLRUCacheSlot callers): rows are shops by their area number (row 0 = area 10, 2 = 12, 7 = 17, 8 = 18, 9 = 19, 11 = 23, 14 = 26).
    inline std::vector<StockRuleDef> baseStockRules()
    {
        std::vector<StockRuleDef> out;
        auto shop = [](int area) { return "base:shop/" + std::to_string(area); };
        auto item = [](int n) { return "base:item/" + std::to_string(n); };
        auto add = [&](const std::string &name, int shopArea, const std::string &on) -> StockRuleDef & {
            StockRuleDef r;
            r.id = "base:stock/" + name;
            r.shop = shop(shopArea);
            r.on = on;
            out.push_back(std::move(r));
            return out.back();
        };
        // Game_ProcessTick, on entering an area: item 193 in Gigatown's... (area 1) and item 176 (area 7), unless the pet already has them
        { StockRuleDef &r = add("193", 12, "area"); r.area = "base:area/1"; r.item = item(193); r.unlessHeld = true; }
        { StockRuleDef &r = add("176", 23, "area"); r.area = "base:area/7"; r.item = item(176); r.unlessHeld = true; r.unlessOffered = true; }
        // ProcessQuestTurnIn, the story quests (mode 5) by number
        for (int q : {20, 45, 68})
        {
            StockRuleDef &r = add("quest" + std::to_string(q), 19, "quest");
            r.quest = q;
            r.modes = {5};
            r.from = "first_objective";
        }
        { StockRuleDef &r = add("quest55", 26, "quest"); r.quest = 55; r.modes = {5}; r.item = item(247); }
        for (int q : {63, 66})
        {
            StockRuleDef &r = add("quest" + std::to_string(q), 23, "quest");
            r.quest = q;
            r.modes = {5};
            r.from = "first_objective";
        }
        { StockRuleDef &r = add("quest89a", 12, "quest"); r.quest = 89; r.modes = {5}; r.item = item(35); }
        { StockRuleDef &r = add("quest89b", 17, "quest"); r.quest = 89; r.modes = {5}; r.item = item(31); }
        { StockRuleDef &r = add("quest89c", 12, "quest"); r.quest = 89; r.modes = {5}; r.item = item(25); }
        // ... and the quest types: mode 2 stocks the first item the quest scattered, modes 4 and 6 every item it asked for (item 90 goes to other shops)
        { StockRuleDef &r = add("spawn", 19, "type"); r.modes = {2}; r.from = "first_spawn"; }
        for (int s : {10, 17, 18})
        {
            StockRuleDef &r = add("objective" + std::to_string(s), s, "type");
            r.modes = {4, 6};
            r.from = "each_objective";
            r.exceptItem = item(90);
        }
        for (int s : {12, 19})
        {
            StockRuleDef &r = add("objective90_" + std::to_string(s), s, "type");
            r.modes = {4, 6};
            r.from = "each_objective";
            r.onlyItem = item(90);
        }
        // SetActiveIllness: the cure goes to the pharmacy
        add("cure", 19, "illness").from = "cure";
        return out;
    }
} // namespace gp
