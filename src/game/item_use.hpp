// game/item_use.hpp
// Port of the "use an item from the menu" logic: ItemMenuScreen (mode 0, food/medicine/potion
// branches) and ApplyItemCategoryEffect (0x02dc3e). Pure state change on Player, no SDL, no UI.
// Dialogue, sounds, saving and the potion recolor lookup stay with the caller.
//
// What an item does is data (ItemDef::use). The action name picks a handler from itemActions();
// a mod registers its own with itemActions().add("mymod:heal", fn) and refers to it from an item record.
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include "item.hpp" // ItemDef / ItemType (adjust to wherever your item header lives)
#include "item_effects.hpp"
#include "player.hpp"

namespace gp
{
    // ItemDef::index -> the string id used in Player::inventory. Change this one function if
    // your ContentDB names items differently.
    inline const Id &itemKey(const ItemDef &d) { return d.id; }

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
        TookMedicine,  // one removed (menu result 3); illness id is the "illness" param (w9 for base items)
        UsedPotion,    // not consumed here (menu result 4); caller does the 0xb061 recolor lookup
        Nothing        // PickupBag and unhandled types
    };

    namespace detail
    {
        inline int personalityCounter(PersonalityStat s) { return 2 + 4 * static_cast<int>(s); }

        inline int param(const ItemUseDef &u, const char *k, int fallback = 0)
        {
            auto i = u.num.find(k);
            return i == u.num.end() ? fallback : i->second;
        }
        inline bool has(const ItemUseDef &u, const char *k) { return u.num.count(k) != 0; }

        inline bool statByName(const std::string &n, PersonalityStat &out)
        {
            if (n == "halos")
                out = Halos;
            else if (n == "pitchforks")
                out = Pitchforks;
            else if (n == "stars")
                out = Stars;
            else if (n == "lightbulbs")
                out = Lightbulbs;
            else
                return false;
            return true;
        }

        // The "food" action's effect, no removal. Order matches the original: fill, care stats, personality.
        inline void applyFoodParams(Player &p, const ItemUseDef &u, ItemUseHost &host)
        {
            if (has(u, "hunger"))
                p.adjustCare(Hunger, param(u, "hunger"));
            if (has(u, "happiness"))
                p.adjustCare(Happiness, param(u, "happiness"));
            if (has(u, "health"))
                p.adjustCare(Health, param(u, "health"));

            // The stat index is inferred from the AdjustPersonalityStat arguments (the decompiler dropped
            // GetPersonalityStat's parameter, so it is assumed to be the same index).
            PersonalityStat stat;
            auto s = u.str.find("stat");
            if (s == u.str.end() || !statByName(s->second, stat))
                return;
            const int cur = p.personality[stat];
            int delta = 0;
            if (has(u, "raise"))
                delta = std::min(param(u, "raise"), kMaxPersonality - cur);
            else if (has(u, "lower"))
                delta = -std::min(cur, param(u, "lower"));
            else
                return;
            p.adjustPersonality(stat, delta);
            host.addStatCounter(personalityCounter(stat), delta);
        }
    } // namespace detail

    // The effective use of an item: its own record, or the one derived from the legacy fields.
    inline ItemUseDef effectiveUse(const ItemDef &d) { return d.use.action.empty() ? legacyItemUse(d) : d.use; }

    // ApplyItemCategoryEffect, kept as an entry point for callers and tests.
    inline void applyFoodEffect(Player &p, const ItemDef &d, ItemUseHost &host)
    {
        detail::applyFoodParams(p, effectiveUse(d), host);
    }

    // GetIllnessItemCure (decompile traced by hand): illness id -> base item id. The fallback for illnessCureItem.
    inline Id legacyIllnessCure(int illness)
    {
        switch (illness)
        {
        case 0:
            return legacyItemId(0x57);
        case 1:
            return legacyItemId(0x51);
        case 3:
            return legacyItemId(0x4e);
        case 4:
            return legacyItemId(0x59);
        case 5:
            return legacyItemId(0x4f);
        case 6:
            return legacyItemId(0x54);
        case 7:
            return legacyItemId(0x4c); // Aspirin, Brain Ache
        case 8:
            return legacyItemId(0x56);
        case 9:
            return legacyItemId(0x50);
        default:
            return legacyItemId(0x58); // illness 2 (Panda Pox), and also 0xa; the original has no explicit case for it
        }
    }

    // The medicine whose record cures `illness` (the first in db order, so the base game wins over mods), the same
    // rule applyMedicine uses. No such medicine = the original table.
    inline Id illnessCureItem(const std::vector<ItemDef> &items, int illness)
    {
        for (const ItemDef &d : items)
            if (d.type == static_cast<int>(ItemType::Medicine) &&
                detail::param(effectiveUse(d), "illness", d.w9) == illness)
                return d.id;
        return legacyIllnessCure(illness);
    }

    // Cures when the medicine matches the current illness. Returns true if the pet was cured.
    inline bool applyMedicine(Player &p, const ItemDef &d)
    {
        if (p.illnessId != detail::param(effectiveUse(d), "illness", d.w9))
            return false;
        p.illnessId = kHealthy;
        return true;
    }

    // A handler returns what the menu should do next. It removes the item itself when the original does.
    using ItemActionFn = std::function<ItemUseResult(Player &, const ItemDef &, const ItemUseDef &, ItemUseHost &)>;

    class ItemActions
    {
    public:
        void add(const std::string &name, ItemActionFn fn) { fns_[name] = std::move(fn); } // same name replaces
        const ItemActionFn *find(const std::string &name) const
        {
            auto i = fns_.find(name);
            return i == fns_.end() ? nullptr : &i->second;
        }

    private:
        std::map<std::string, ItemActionFn> fns_;
    };

    inline ItemActions makeBaseItemActions()
    {
        ItemActions a;
        a.add("food", [](Player &p, const ItemDef &d, const ItemUseDef &u, ItemUseHost &host)
              {
                  detail::applyFoodParams(p, u, host);
                  p.removeItem(itemKey(d), 1);
                  return ItemUseResult::Ate; });
        a.add("medicine", [](Player &p, const ItemDef &d, const ItemUseDef &, ItemUseHost &)
              {
                  p.removeItem(itemKey(d), 1);
                  return ItemUseResult::TookMedicine; });
        a.add("potion", [](Player &, const ItemDef &, const ItemUseDef &, ItemUseHost &)
              { return ItemUseResult::UsedPotion; });
        a.add("dialogue", [](Player &, const ItemDef &, const ItemUseDef &, ItemUseHost &)
              { return ItemUseResult::NeedsDialogue; });
        a.add("none", [](Player &, const ItemDef &, const ItemUseDef &, ItemUseHost &)
              { return ItemUseResult::Nothing; });
        return a;
    }

    // The registry the game uses. Mods add handlers here at load time.
    inline ItemActions &itemActions()
    {
        static ItemActions a = makeBaseItemActions();
        return a;
    }

    // Menu mode 0 ("use").
    inline ItemUseResult useItem(Player &p, const ItemDef &d, ItemUseHost &host)
    {
        if (p.itemCount(itemKey(d)) <= 0)
            return ItemUseResult::NotOwned;
        const ItemUseDef u = effectiveUse(d);
        const ItemActionFn *fn = itemActions().find(u.action);
        return fn ? (*fn)(p, d, u, host) : ItemUseResult::Nothing;
    }
} // namespace gp