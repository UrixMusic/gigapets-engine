#include <cassert>
#include <cstdio>
#include "../src/game/item_use.hpp"

using namespace gp;

static ItemDef food(int index, int w9)
{
    ItemDef d;
    d.index = index;
    d.id = legacyItemId(index);
    d.type = static_cast<int>(ItemType::Food);
    d.w9 = w9;
    return d;
}

int main()
{
    ItemUseHost host;

    { // category 3 (Banana Split): hunger +3, happiness +6, item consumed
        Player p;
        ItemDef d = food(0x20, 3);
        p.addItem(itemKey(d), 2);
        assert(useItem(p, d, host) == ItemUseResult::Ate);
        assert(p.care[Hunger] == 3 && p.care[Happiness] == 6);
        assert(p.itemCount(itemKey(d)) == 1);
    }
    { // category 1 junk food: health penalty is ignored until 4 quests are done
        Player p;
        p.care[Health] = 50;
        ItemDef d = food(0x21, 1);
        p.addItem(itemKey(d));
        useItem(p, d, host);
        assert(p.care[Health] == 50);
        p.questsCompleted = 4;
        p.addItem(itemKey(d));
        useItem(p, d, host);
        assert(p.care[Health] == 44);
    }
    { // mini-pet food: no effect, nothing consumed
        Player p;
        ItemDef d = food(0x22, kFoodForMiniPets);
        p.addItem(itemKey(d));
        assert(useItem(p, d, host) == ItemUseResult::NeedsDialogue);
        assert(p.itemCount(itemKey(d)) == 1 && p.care[Hunger] == 0);
    }
    { // special 0x35: halos +20, capped at 9999
        Player p;
        p.personality[Halos] = 9990;
        ItemDef d = food(0x35, kFoodSpecial);
        p.addItem(itemKey(d));
        useItem(p, d, host);
        assert(p.personality[Halos] == 9999);
        assert(p.care[Hunger] == 3); // special foods restore 3 hunger, not a full refill
    }
    { // categories 0 and 6 (read from the disassembly: the decompiler dropped their arguments)
        Player p;
        p.care[Health] = 10;
        ItemDef a = food(0x24, 0), b = food(0x25, 6);
        p.addItem(itemKey(a));
        p.addItem(itemKey(b));
        useItem(p, a, host);
        assert(p.care[Hunger] == 10 && p.care[Health] == 10);
        useItem(p, b, host);
        assert(p.care[Hunger] == 10 && p.care[Health] == 30);
    }
    { // illnessCureItem: the medicine whose record cures it, else the original table; a scripted medicine is ignored
        std::vector<ItemDef> items;
        ItemDef scripted;
        scripted.id = "m:item/restore";
        scripted.type = int(ItemType::Medicine);
        scripted.use.action = "none"; // w9 stays 0, which must not make it the cure for illness 0
        items.push_back(scripted);
        ItemDef cure;
        cure.id = "m:item/cure3";
        cure.type = int(ItemType::Medicine);
        cure.use.action = "medicine";
        cure.use.num["illness"] = 3;
        items.push_back(cure);
        assert(illnessCureItem(items, 0) == legacyIllnessCure(0));
        assert(illnessCureItem(items, 3) == "m:item/cure3");
        assert(illnessCureItem(items, 9) == legacyIllnessCure(9));
    }
    { // not owned
        Player p;
        assert(useItem(p, food(0x23, 3), host) == ItemUseResult::NotOwned);
    }
    std::puts("item_use_test ok");
    return 0;
}