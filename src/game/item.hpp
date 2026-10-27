#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "rom.hpp"
#include "../content/records.hpp"

namespace gp
{
    constexpr int kItemTable = 0xa5df; // word address
    constexpr int kItemWords = 10;
    constexpr int kItemCount = 269;    // (0xb061 - 0xa5df) / 10; use GigaEditr's ITEM_N if it differs

    enum class ItemType : int
    {
        Placeholder = 0, Wall = 1, Floor = 2, FoodDish = 3, Bed = 4, Window = 5, HangingDecor = 6,
        PlacedDecor = 8, Food = 9, Medicine = 10, PickupItem = 11, GenericQuestItem = 12, MoneyBag = 13,
        TreasureMap = 14, SpecialQuestItem = 15, Potion = 16, IslandMap = 17
    };

    inline std::vector<ItemDef> readItems(const Rom &rom)
    {
        std::vector<ItemDef> out;
        for (int i = 0; i < kItemCount; ++i)
        {
            const int b = kItemTable + i * kItemWords;
            ItemDef d;
            d.index = i;
            d.type = rom.u16(b + 0);
            d.nameText = rom.u16(b + 1);
            d.pluralText = rom.u16(b + 2);
            d.maxQty = (1 << (rom.u16(b + 3) & 15)) - 1;
            d.price = rom.u16(b + 4);
            d.decorSprite = rom.u16(b + 5);
            d.worldSprite = rom.u16(b + 6);
            d.w7 = rom.u16(b + 7);
            d.iconSprite = rom.u16(b + 8);
            d.w9 = rom.u16(b + 9);
            out.push_back(d);
        }
        return out;
    }
}