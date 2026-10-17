#include <cassert>
#include <cstdio>
#include "../src/game/item_use.hpp"

using namespace gp;

static ItemDef food(int index, int w9)
{
    ItemDef d;
    d.index = index;
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
        p.addItem(itemKey(d.index), 2);
        assert(useItem(p, d, host) == ItemUseResult::Ate);
        assert(p.care[Hunger] == 3 && p.care[Happiness] == 6);
        assert(p.itemCount(itemKey(d.index)) == 1);
    }
    { // category 1 junk food: health penalty is ignored until 4 quests are done
        Player p;
        p.care[Health] = 50;
        ItemDef d = food(0x21, 1);
        p.addItem(itemKey(d.index));
        useItem(p, d, host);
        assert(p.care[Health] == 50);
        p.questsCompleted = 4;
        p.addItem(itemKey(d.index));
        useItem(p, d, host);
        assert(p.care[Health] == 44);
    }
    { // mini-pet food: no effect, nothing consumed
        Player p;
        ItemDef d = food(0x22, kFoodForMiniPets);
        p.addItem(itemKey(d.index));
        assert(useItem(p, d, host) == ItemUseResult::NeedsDialogue);
        assert(p.itemCount(itemKey(d.index)) == 1 && p.care[Hunger] == 0);
    }
    { // special 0x35: halos +20, capped at 9999
        Player p;
        p.personality[Halos] = 9990;
        ItemDef d = food(0x35, kFoodSpecial);
        p.addItem(itemKey(d.index));
        useItem(p, d, host);
        assert(p.personality[Halos] == 9999);
    }
    { // not owned
        Player p;
        assert(useItem(p, food(0x23, 3), host) == ItemUseResult::NotOwned);
    }
    std::puts("item_use_test ok");
    return 0;
}