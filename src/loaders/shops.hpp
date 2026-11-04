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
} // namespace gp
