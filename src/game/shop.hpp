// shop.hpp - shop stock, pending world items, spawn on shop entry / consume on leaving.
// Ported from SpawnShopItemStock, SpawnItemAtRandomFreeSlot, ConsumeUnflaggedPendingEventsForArea.
#pragma once
#include "../content/records.hpp"
#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>

namespace gp
{

    // MapStoryStageToShopCatalogueIndex (019094). Keyed by legacy area id; -1 = not a shop.
    inline int shopRowForArea(int area)
    {
        static const int8_t rows[0x26] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
            -1, -1, 11, 12, 13, 14, -1, -1, -1, 15, 16, 17, 18, -1, 19, 21, 20};
        return (area >= 0 && area < 0x26) ? rows[area] : -1;
    }

    struct StockEntry
    {
        uint16_t w0 = 0, idx = 0; // w0 = (stock << 9) | item, idx = offer index
        int stock() const { return w0 >> 9; }
        int item() const { return w0 & 0x1FF; }
    };

    struct ShopStock
    {
        StockEntry rows[22][9];
        static StockEntry make(int item, int stock, int idx) { return {uint16_t((stock << 9) | item), uint16_t(idx)}; }
        // Test data only: row 1 (shop 0xB) as seen in MAME. Replace with the real initial table.
        void initRow1()
        {
            const int it[6] = {0x2d, 0x3d, 0x44, 0x33, 0x40, 0x45}, st[6] = {1, 2, 2, 2, 2, 1};
            for (int i = 0; i < 6; ++i)
                rows[1][i] = make(it[i], st[i], i + 1);
        }
        // Purchase: stock -= 1, and a zero stock clears both words. Returns true if the entry is now empty.
        bool buyOne(int row, int i)
        { // Item_TickDeliveryCountdown; true = entry emptied
            StockEntry &e = rows[row][i];
            if (e.stock() <= 0)
                return false;
            const int s = e.stock() - 1;
            if (s == 0)
            {
                e = {};
                return true;
            }
            e.w0 = uint16_t((s << 9) | e.item());
            return false;
        }
        int find(int row, int item) const
        {
            for (int i = 0; i < 9; ++i)
                if (rows[row][i].w0 && rows[row][i].item() == item)
                    return i;
            return -1;
        }
    };

    // Parses a MAME text dump (dump stock.bin,e6b,18c,2): lines like "000E6B:  022B 0001 0220 ...  ascii".
    inline bool loadStockDump(const char *path, ShopStock &s)
    {
        FILE *f = std::fopen(path, "rb");
        if (!f)
            return false;
        std::vector<uint16_t> w;
        char line[512];
        while (w.size() < 22 * 18 && std::fgets(line, sizeof line, f))
        {
            char *p = std::strchr(line, ':');
            if (!p)
                continue;
            ++p;
            for (int k = 0; k < 8 && w.size() < 22 * 18; ++k)
            { // at most 8 words per line, so the ASCII column is never read
                char *end = nullptr;
                const unsigned long v = std::strtoul(p, &end, 16);
                if (end - p < 4)
                    break; // a word token includes its leading space, so this is > 4 chars when valid
                w.push_back(uint16_t(v));
                p = end;
            }
        }
        std::fclose(f);
        if (w.size() != 22 * 18)
            return false;
        for (int r = 0; r < 22; ++r)
            for (int i = 0; i < 9; ++i)
                s.rows[r][i] = {w[size_t(r) * 18 + size_t(i) * 2], w[size_t(r) * 18 + size_t(i) * 2 + 1]};
        return true;
    }

    struct ShopEvent
    { // trimmed: category/eventId are always 0 / none for shop items
        int area, slot, item, x, y, type;
        bool keep;
    };

    class PendingList
    {
    public:
        static constexpr size_t kMax = 56;
        std::vector<ShopEvent> v;

        bool slotOccupied(int area, int slot) const
        { // APPROXIMATION of 03fe81 + 03febf
            for (const ShopEvent &e : v)
                if (e.area == area && e.slot == slot)
                    return true;
            return false;
        }
        void spawn(const ShopEvent &e)
        {
            if (v.size() >= kMax)
                v.erase(v.begin()); // real eviction rules not ported
            v.push_back(e);
        }
        // ConsumeUnflaggedPendingEventsForArea: walks from the end, removes keep == 0 entries of that area.
        void consumeUnflaggedForArea(int area)
        {
            for (int i = int(v.size()) - 1; i >= 0; --i)
                if (v[size_t(i)].area == area && !v[size_t(i)].keep)
                    v.erase(v.begin() + i);
        }
        bool removeShopItem(int area, int item)
        { // Event_FindPendingTrigger(area, item), assumed to remove it
            for (size_t k = 0; k < v.size(); ++k)
                if (v[k].area == area && v[k].type == 3 && v[k].item == item)
                {
                    v.erase(v.begin() + k);
                    return true;
                }
            return false;
        }
    };

    // rnd(n) must return 0..n-1 (RandRange(0, max) has an exclusive max). Same call order as the original:
    // one roll, then one re-roll per occupied hit, giving up after 200 retries.
    template <class Rnd>
    bool spawnAtRandomFreeSlot(PendingList &p, int area, const std::vector<IPoint> &slots,
                               int item, int type, bool keep, Rnd &&rnd)
    {
        const int max = int(slots.size());
        if (max <= 0)
            return false;
        int slot = rnd(max);
        for (int tries = 0; p.slotOccupied(area, slot);)
        {
            if (tries++ > 200)
                return false;
            slot = rnd(max);
        }
        p.spawn({area, slot, item, slots[size_t(slot)].x, slots[size_t(slot)].y, type, keep});
        return true;
    }

    enum class BuyOutcome
    {
        AtCap,
        CantAfford,
        Pick
    };
    struct BuyQuote
    {
        BuyOutcome outcome;
        int maxQty;
    }; // maxQty = picker maximum (or the cap for AtCap)

    // Item_ShowPurchaseDialogue's decision part. itemMax = (1 << maxQtyShift) - 1 = ItemDef::maxQty.
    inline BuyQuote quotePurchase(int stock, int money, int price, int held, int itemMax)
    {
        if (held >= itemMax)
            return {BuyOutcome::AtCap, itemMax};
        int afford = price > 0 ? money / price : stock; // original divides; guard the zero price
        afford = std::min(stock, afford);
        if (afford < 1)
            return {BuyOutcome::CantAfford, 0};
        return {BuyOutcome::Pick, std::min(afford, itemMax - held)};
    }

    // DeliverPurchasedQuantity. `give` = Item_IncrementOwnedAndSave. Money is the caller's job: money -= qty * price.
    template <class Give, class SoldOut>
    void deliverPurchase(ShopStock &s, int row, int item, int qty, Give &&give, SoldOut &&soldOut)
    {
        const int slot = s.find(row, item);
        if (slot < 0)
            return;
        for (int n = 0; n < qty; ++n)
        {
            if (s.buyOne(row, slot))
                soldOut(item); // Item_TickDeliveryCountdown
            give(item);        // Item_IncrementOwnedAndSave
        }
    }

    // SpawnShopItemStock: one spawn per stocked entry, in stock order, stopping at the slot count.
    template <class Rnd>
    int spawnShopItemStock(PendingList &p, const ShopStock &s, int area, int row,
                           const std::vector<IPoint> &slots, Rnd &&rnd)
    {
        int n = 0;
        for (int i = 0; i < 9 && n < int(slots.size()); ++i)
        {
            const StockEntry &e = s.rows[row][i];
            if (e.w0 == 0)
                continue;
            ++n;
            spawnAtRandomFreeSlot(p, area, slots, e.item(), 3, false, rnd);
        }
        return n;
    }

} // namespace gp