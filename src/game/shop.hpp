// shop.hpp - shop stock, pending world items, spawn on shop entry / consume on leaving.
// Ported from SpawnShopItemStock, SpawnItemAtRandomFreeSlot, ConsumeUnflaggedWorldItemsForArea.
#pragma once
#include "../content/records.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <vector>

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

    // One entry of a shop's stock: an item and how many are on offer. (The ROM packs these into two words, `item | stock << 9` and an order
    // number, so an item could be 9 bits; here an entry holds the item's id, so a mod's items fit.) `idx` is the order the entries came in.
    struct StockEntry
    {
        Id item;
        int stock = 0;
        int idx = 0;
        bool present() const { return !item.empty() && stock > 0; }
    };

    // One weighted pick from a shop's table (PickWeightedRandomItem 0x2ddfe): the weights are summed, rnd(total) (0..total-1) is rolled, and the entries
    // are walked from the last one back until the running total passes it. Empty if the shop lists nothing with a weight.
    template <class Rnd>
    Id pickWeightedItem(const ShopDef &shop, Rnd &&rnd)
    {
        long total = 0;
        for (const ShopItem &i : shop.items)
            total += std::max(0, i.weight);
        if (total <= 0)
            return {};
        const long r = rnd(int(std::min<long>(total, 0x7fffffff)));
        long acc = 0;
        for (size_t k = shop.items.size(); k-- > 0;)
        {
            acc += std::max(0, shop.items[k].weight);
            if (acc > r)
                return shop.items[k].item;
        }
        return shop.items.front().item;
    }

    // The stock of every shop, by the shop's area. Nine entries a shop. Made when the game starts (InitZoneItemPools 0x2e2f3: 10 weighted picks a
    // shop) and topped up by a timer (RollShopItemStock 0x2e339: one pick a shop); a purchase takes from it; it is not saved.
    class ShopStock
    {
    public:
        static constexpr int kSlots = 9;
        using Row = std::array<StockEntry, kSlots>;

        void clear() { rows_.clear(); }
        Row &row(const Id &area) { return rows_[area]; }
        const Row *rowOf(const Id &area) const
        {
            const auto it = rows_.find(area);
            return it == rows_.end() ? nullptr : &it->second;
        }

        // InsertOrIncrementPoolSlot (0x2db08): an item already there gets one more; else it takes the first empty slot, numbered one past the highest
        // order number in the row. False when the row is full of other items.
        bool insert(const Id &area, const Id &item)
        {
            if (item.empty())
                return false;
            Row &r = rows_[area];
            int firstEmpty = -1, highest = 0;
            for (int i = 0; i < kSlots; ++i)
            {
                if (r[size_t(i)].item == item)
                {
                    ++r[size_t(i)].stock;
                    return true;
                }
                if (r[size_t(i)].item.empty())
                {
                    if (firstEmpty < 0)
                        firstEmpty = i;
                }
                else
                    highest = std::max(highest, r[size_t(i)].idx);
            }
            if (firstEmpty < 0)
                return false;
            r[size_t(firstEmpty)] = {item, 1, highest + 1};
            return true;
        }

        template <class Rnd>
        void rollOne(const ShopDef &shop, Rnd &&rnd)
        {
            insert(shop.area, pickWeightedItem(shop, rnd));
        }
        // The shops as they are when the game starts.
        template <class Rnd>
        void init(const std::vector<ShopDef> &shops, Rnd &&rnd)
        {
            rows_.clear();
            for (const ShopDef &s : shops)
            {
                rows_[s.area] = Row{};
                for (int n = 0; n < s.rolls; ++n)
                    rollOne(s, rnd);
            }
        }
        // The timer's top-up: one more pick for every shop.
        template <class Rnd>
        void restock(const std::vector<ShopDef> &shops, Rnd &&rnd)
        {
            for (const ShopDef &s : shops)
                rollOne(s, rnd);
        }

        // ClampInventorySlotQuantities (0x2e381), run as the pet walks into a shop: no entry offers more than the shop's cap for it, or more than the pet
        // can still carry (held(item), maxQty(item)); an entry that comes to nothing is emptied. An item the shop's table does not list has no cap.
        template <class Held, class Max>
        void clamp(const ShopDef &shop, Held &&held, Max &&maxQty)
        {
            auto it = rows_.find(shop.area);
            if (it == rows_.end())
                return;
            for (StockEntry &e : it->second)
            {
                if (e.item.empty())
                    continue;
                int allowed = std::max(0, maxQty(e.item) - held(e.item));
                for (const ShopItem &i : shop.items)
                    if (i.item == e.item)
                    {
                        allowed = std::min(allowed, i.cap);
                        break;
                    }
                e.stock = std::min(e.stock, allowed);
                if (e.stock <= 0)
                    e = StockEntry{};
            }
        }

        // The slot of an item in a shop, or -1.
        int find(const Id &area, const Id &item) const
        {
            const Row *r = rowOf(area);
            if (!r)
                return -1;
            for (int i = 0; i < kSlots; ++i)
                if (!(*r)[size_t(i)].item.empty() && (*r)[size_t(i)].item == item)
                    return i;
            return -1;
        }
        // Purchase: stock -= 1, and a zero stock clears the entry. Returns true if the entry is now empty (Item_TickDeliveryCountdown).
        bool buyOne(const Id &area, int slot)
        {
            StockEntry &e = rows_[area][size_t(slot)];
            if (e.stock <= 0)
                return false;
            if (--e.stock == 0)
            {
                e = StockEntry{};
                return true;
            }
            return false;
        }

    private:
        std::map<Id, Row> rows_;
    };

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
        // ConsumeUnflaggedWorldItemsForArea: walks from the end, removes keep == 0 entries of that area.
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
    void deliverPurchase(ShopStock &s, const Id &area, const Id &item, int qty, Give &&give, SoldOut &&soldOut)
    {
        const int slot = s.find(area, item);
        if (slot < 0)
            return;
        for (int n = 0; n < qty; ++n)
        {
            if (s.buyOne(area, slot))
                soldOut(item); // Item_TickDeliveryCountdown
            give(item);        // Item_IncrementOwnedAndSave
        }
    }

    // SpawnShopItemStock: one spawn per stocked entry, in stock order, stopping at the slot count. `number` turns an item id into the number the world
    // items carry (the db's item index), or -1 for an item the game does not know (skipped).
    template <class Rnd, class Number>
    int spawnShopItemStock(PendingList &p, const ShopStock &s, int area, const Id &areaId, const std::vector<IPoint> &slots, Rnd &&rnd, Number &&number)
    {
        const ShopStock::Row *row = s.rowOf(areaId);
        if (!row)
            return 0;
        int n = 0;
        for (const StockEntry &e : *row)
        {
            if (n >= int(slots.size()))
                break;
            if (!e.present())
                continue;
            const int it = number(e.item);
            if (it < 0)
                continue;
            ++n;
            spawnAtRandomFreeSlot(p, area, slots, it, 3, false, rnd);
        }
        return n;
    }

} // namespace gp
