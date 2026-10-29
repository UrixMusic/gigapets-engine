// sale.hpp - selling items to Jeremy (the buyer in area 23, NPC 0x44).
//
// Jeremy's talk (ProcessStoryModeObjectInteraction): a Yes / No box "I like to buy all sorts of things. ..." whose Yes
// is ShowSellMenu (0x019072): the item screen in mode 1. Choosing an item asks "How many do you want to sell?" (a quantity
// from 1 up to how many you hold); the bed and the food dish that are in your room can't be sold (ItemMenuScreen mode 1).
// Item_ProcessSale (0x02e11f) pays int(quantity * price * 0.8) in single-precision floats; when an item's 0.8 * price is
// not above 1.0 each one still pays 1. Then Item_ApplyLimit takes the items and AdjustPlayerMoney pays.
#pragma once

namespace gp
{

    constexpr int kSellerNpc = 0x44; // Jeremy
    constexpr float kSellFactor = 0.8f;

    // Item_BuildFilteredList in mode 1: types 14 (treasure maps), 15 (special quest items, the king's and the garden's
    // included), 16 (potions) and 17 (the island map) never show up on the list.
    constexpr bool sellExcludedType(int itemType) { return itemType >= 14 && itemType <= 17; }

    inline int saleTotal(int price, int qty)
    {
        const float unit = float(price) * kSellFactor;
        const float total = unit > 1.0f ? float(qty) * unit : float(qty);
        return int(total);
    }

} // namespace gp
