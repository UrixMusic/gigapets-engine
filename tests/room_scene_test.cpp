// tests/room_scene_test.cpp - homes and hotels: theme tables, hotel presets, return points, the scroll rule, the render.
// Usage: room_scene_test [path/to/rom.u7]   (the ROM checks are skipped if it can't be opened)
#include <cstdio>
#include <memory>
#include "../src/engine/room_render.hpp"
#include "../src/game/save_player.hpp"
#include "../src/loaders/text.hpp"
using namespace gp;

int main(int argc, char **argv)
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("rooms are 36 x 8 tiles of 32 px", kRoomWidthTiles == 36 && kRoomHeightTiles == 8 && kRoomTilePx == 32);
    check("locations 0-2 and 4-7 are rooms, 3 and 8 are not",
          isRoomLocation(0) && isRoomLocation(2) && isRoomLocation(4) && isRoomLocation(7) && !isRoomLocation(3) && !isRoomLocation(8));
    check("4..7 are hotels, homes are not", isHotel(4) && isHotel(7) && !isHotel(2) && !isHotel(3));
    check("hotel inventories in location order 4, 7, 5, 6",
          hotelInventoryIndex(4) == 0 && hotelInventoryIndex(7) == 1 && hotelInventoryIndex(5) == 2 && hotelInventoryIndex(6) == 3 &&
              hotelInventoryIndex(0) == -1);
    check("area exit points", roomReturnPoint(0).x == 0x826 && roomReturnPoint(7).y == 0x526 && roomReturnPoint(4).x == 0x1dc);

    check("pet x is clamped to 0x40..width-0x40", roomClampX(0) == 0x40 && roomClampX(5000) == 36 * 32 - 0x40 && roomClampX(300) == 300);
    check("scroll follows the pet left", roomScroll(500, 520) == 520 - kRoomScrollLead);
    check("scroll follows the pet right", roomScroll(0, 0x300) == 0x300 - 0x100);
    check("scroll stays when the pet is in the window", roomScroll(100, 100 + 0x80) == 100);
    check("scroll never leaves the room", roomScroll(0, 0x40) == 0 && roomScroll(2000, 36 * 32 - 0x40) == 36 * 32 - 320);
    check("entering from the door starts the scroll on the door", roomScroll(0, kRoomDoorX) == kRoomDoorX - kRoomScrollTrail);

    {
        // FindDecorationNearPetX over the real table shape: door 0, bed 1, bowl 4 have type 5; the rest are plain.
        std::vector<RoomDecoration> t = {{0x170, 0x8f, 0x95b, 0x435, 5, 1}, {0x39e, 0xa8, 0x927, 0x28c, 5, 0}, {0x1df, 0xad, 0x935, 0x28c, 0xffff, 0},
                                         {0x1df, 0xad, 0x961, 0x28c, 0xffff, 0}, {0x1df, 0xad, 0x936, 0x28c, 5, 0}, {0xc0, 0x70, 0x95c, 0x435, 0xffff, 1}};
        auto half = [](const RoomDecoration &) { return 20; };
        check("far from everything: nothing is near", nearbyDecoration(t, 0x100, half) == -1);
        check("at the door: 0", nearbyDecoration(t, 0x170, half) == 0 && nearbyDecoration(t, 0x170 + 25, half) == 0);
        check("the reach is half width + type (20 + 5)", nearbyDecoration(t, 0x170 + 26, half) == -1 && nearbyDecoration(t, 0x170 - 25, half) == 0);
        check("at the bed: 1", nearbyDecoration(t, 0x39e, half) == 1);
        check("at the bowl: 4 (the plain pieces on top of it never count)", nearbyDecoration(t, 0x1df, half) == 4);
        check("plain pieces (type 0xffff) are never near", nearbyDecoration(t, 0xc0, half) == -1);
        check("the last overlapping entry wins", [&]
              {
                  std::vector<RoomDecoration> o = {{100, 0, 1, 0, 5, 0}, {110, 0, 2, 0, 5, 0}};
                  return nearbyDecoration(o, 105, half) == 1;
              }());
        check("the arrow sits over the sprite", selectorPosition(t[0], 0x30).x == 0x170 && selectorPosition(t[0], 0x30).y == 0x8f - 0x30);
        check("but never above y 0x32", selectorPosition(t[5], 0x60).y == 0x32);
    }

    {
        check("bowl target: x 0x208, y 0xad; the panda's 0x1c3, 0xa1", bowlTarget(0).x == 0x208 && bowlTarget(1).y == 0xad && bowlTarget(2).x == 0x1c3 && bowlTarget(2).y == 0xa1);
        check("approach: 26 px past the bowl on the floor, the panda 50 px short of it",
              bowlApproachX(0, 0xba) == 0x208 + 26 && bowlApproachX(1, 0xba) == 0x208 + 26 && bowlApproachX(2, 0xba) == 0x1c3 - 50);
        check("the diagonal is 2 px of x per px of y", (bowlApproachX(0, 0xba) - bowlTarget(0).x) == 2 * (0xba - bowlTarget(0).y) &&
                                                            (bowlTarget(2).x - bowlApproachX(2, 0xba)) == 2 * (0xba - bowlTarget(2).y));
        check("bowl sprite by bites left: 3 -> 0x95e, 2 -> 0x963, 1 -> 0x961, otherwise unchanged",
              bowlSpriteForBites(3) == 0x95e && bowlSpriteForBites(2) == 0x963 && bowlSpriteForBites(1) == 0x961 && bowlSpriteForBites(0) == -1);
        check("the pause is 0.05, 0.10 or 0.15 s: 2, 3 or 5 steps of 1/30 s", bowlPauseTicks(1) == 2 && bowlPauseTicks(2) == 3 && bowlPauseTicks(3) == 5);
        check("the bed: the floor spot is 78 px short of it", bedApproachX(0xba) == 0x39e - 78 && kBedX == 0x39e && kBedY == 0x93);
        check("sleep goes on until 3 cycles and rested (> 0x5f)", !sleepOver(2, 100, false) && !sleepOver(0, 0x5f, false) && sleepOver(0, 0x60, false) && sleepOver(-2, 0x70, false));
        check("a rested pet still sleeps 3 cycles, an unrested one keeps going", !sleepOver(1, 0x64, false) && !sleepOver(-1, 30, false));
        check("quitting ends after 3 cycles whatever the stat", sleepOver(0, 0, true) && !sleepOver(1, 100, true));
        check("sleep cycle rate: 5 fps for the dog, 7 for the rest", sleepCycleFps(0) == 5 && sleepCycleFps(1) == 7 && sleepCycleFps(2) == 7);
        check("the visitor is near within 0x1e px of x 0x28a, strictly", nearVisitor(0x28a) && nearVisitor(0x28a + 0x1d) && nearVisitor(0x28a - 0x1d) &&
                                                                              !nearVisitor(0x28a + 0x1e) && !nearVisitor(0x28a - 0x1e));
        check("a visitor beats the table entries (index 5)", visitRules(1, 0x28a, true, false) == kDecorVisitor && visitRules(-1, 0x28a, true, false) == kDecorVisitor);
        check("no visitor: the table's answer stands", visitRules(4, 0x28a, false, false) == 4 && visitRules(-1, 0x28a, false, false) == -1);
        check("far from the visitor: the table's answer stands", visitRules(0, 0x170, true, false) == 0);
        check("for rent: only the door can be selected, and never the visitor", visitRules(0, 0x170, false, true) == 0 && visitRules(4, 0x208, false, true) == -1 &&
                                                                                    visitRules(1, 0x39e, false, true) == -1 && visitRules(-1, 0x28a, true, true) == -1);
        {
            ItemDef food;
            food.type = 9;
            food.maxQty = 7;
            ItemDef bed;
            bed.type = 4;
            bed.maxQty = 1;
            ItemDef pill;
            pill.type = 14;
            pill.maxQty = 7;
            check("an ordinary item can be given while they have room", canGiveItem(food, false, 0) && canGiveItem(food, false, 6) && !canGiveItem(food, false, 7));
            check("a placed bed or furniture can't be given, an unplaced one can", !canGiveItem(bed, true, 0) && canGiveItem(bed, false, 0));
            check("the mini-pet types never can", !canGiveItem(pill, false, 0));
            ItemDef halo;
            halo.type = 11;
            halo.maxQty = 0;
            check("an item with no room at all (max 0) never can", !canGiveItem(halo, false, 0));
            check("the picker's most: what you have or what fits, the smaller", giveMax(5, 7, 0) == 5 && giveMax(5, 7, 4) == 3 && giveMax(2, 7, 4) == 2 && giveMax(5, 7, 7) == 0);
            check("their count after a gift never passes the item's most", theirCountAfterGift(3, 2, 7) == 5 && theirCountAfterGift(6, 5, 7) == 7);
        }
        check("the bowl is entry 3 and starts full at 0x95d", kBowlEntry == 3 && kBowlFull == 0x95d && kBowlFoodItem == 0x1c);
    }

    std::unique_ptr<Rom> rom;
    try { rom.reset(new Rom(argc > 1 ? argv[1] : "rom.u7")); }
    catch (const std::exception &e) { std::printf("SKIP ROM checks (%s)\n", e.what()); return fails ? 1 : 0; }

    for (int loc : {4, 5, 6, 7})
    {
        const int w = hotelItemInSlot(*rom, loc, kInventoryWallSlot), f = hotelItemInSlot(*rom, loc, kInventoryFloorSlot);
        char b[64];
        std::snprintf(b, sizeof b, "hotel %d has wall and floor items (%d, %d)", loc, w, f);
        check(b, w > 0 && f > 0);
    }
    {
        // the tables seen in MAME: homes -> RAM 0x1eff, hotels -> 0x1f35 (door first, then bed, 3 pieces, 4 wall pieces)
        const auto home = roomDecorations(*rom, 0), hotel = roomDecorations(*rom, 7);
        check("9 entries each", home.size() == 9 && hotel.size() == 9);
        check("the door is entry 0 in both: sprite 0x95b at 0x170, 0x8f, type 5",
              home[0].sprite == 0x95b && home[0].x == 0x170 && home[0].y == 0x8f && home[0].type == 5 && hotel[0].sprite == 0x95b);
        check("homes' bed is 0x927 and hotels' is 0x92a", home[1].sprite == 0x927 && hotel[1].sprite == 0x92a);
        check("four wall pieces 0x95c at x 0xc0, 0x208, 0x302, 0x410, y 0x70",
              home[5].sprite == 0x95c && home[5].x == 0xc0 && home[6].x == 0x208 && home[7].x == 0x302 && home[8].x == 0x410 && home[8].y == 0x70);
        check("every home uses the same table, every hotel too",
              roomDecorations(*rom, 2)[1].sprite == 0x927 && roomDecorations(*rom, 4)[1].sprite == 0x92a && roomDecorations(*rom, 6)[4].sprite == 0x938);
        check("location 9 has none", roomDecorations(*rom, 9).empty());
    }
    {
        // composition: items from the ROM, then a hotel's preset and a new pet's home
        const std::vector<ItemDef> items = readItems(*rom);
        auto has = [](const std::vector<RoomSprite> &v, int sprite, int x, int y)
        {
            for (const RoomSprite &s : v)
                if (s.sprite == sprite && s.x == x && s.y == y)
                    return true;
            return false;
        };
        const RoomSprites h = roomSprites(*rom, roomDecorations(*rom, 7), hotelSlotItems(*rom, 7), items);
        check("hotel 7: the door and four wall hooks are behind everything",
              has(h.behind, 0x95b, 0x170, 0x8f) && has(h.behind, 0x95c, 0xc0, 0x70) && has(h.behind, 0x95c, 0x410, 0x70));
        check("hotel 7: bed 0x71 shows its decor sprite 0x92a behind, no front sprite", has(h.behind, 0x92a, 0x39e, 0xa8) && !has(h.beforePet, 0x928, 0x39e, 0xa8));
        check("hotel 7: the three wall items sit at slots 2..4 (0x957 at 0xc0, 0x208, 0x302; y 0x64)",
              has(h.items, 0x957, 0xc0, 0x64) && has(h.items, 0x957, 0x208, 0x64) && has(h.items, 0x957, 0x302, 0x64));
        check("hotel 7: furniture 0x6d draws 0x939 and 0x93a, and the literal 0x961, before the pet",
              has(h.beforePet, 0x939, 0x1df, 0xad) && has(h.beforePet, 0x93a, 0x1df, 0xad) && has(h.beforePet, 0x961, 0x1df, 0xad));
        check("the wall and floor (types 1, 2) are not drawn as items", !has(h.items, 0, 0x64, 0x4b));

        SlotItems home{};
        for (const auto &[slot, item] : starterEquip(0))
            home[size_t(slot)] = item;
        const RoomSprites m = roomSprites(*rom, roomDecorations(*rom, 0), home, items);
        check("a new pet's home: bed 0x6f has both sprites", has(m.behind, 0x927, 0x39e, 0xa8) && has(m.beforePet, 0x928, 0x39e, 0xa8));
        check("a new pet's home: furniture 0x6b is 0x935 and 0x936", has(m.beforePet, 0x935, 0x1df, 0xad) && has(m.beforePet, 0x936, 0x1df, 0xad));
        const RoomSprites e = roomSprites(*rom, roomDecorations(*rom, 0), SlotItems{}, items);
        check("an empty home: the door, hooks and literal 0x961 only; no bed or furniture",
              e.behind.size() == 5 && e.items.empty() && e.beforePet.size() == 1 && has(e.beforePet, 0x961, 0x1df, 0xad));
    }
    {
        ContentDB tdb;
        importWideStrings(tdb, *rom);
        check("display prompt", tdb.textOr("base:str/room_display_prompt") == "Display this in your room?");
        check("remove prompt", tdb.textOr("base:str/room_remove_prompt") == "Remove this item from your room?");
        check("already shown", tdb.textOr("base:str/room_already_shown") == "This item is already displayed in your room.");
        check("give prompt", tdb.textOr("base:str/give_prompt") == "How many do you want to give?");
        check("can't give the bed", tdb.textOr("base:str/give_no_bed") == "You can't give away the bed that's currently in your room.");
        check("can't give the dish", tdb.textOr("base:str/give_no_dish") == "You can't give away the food dish that's currently in your room.");
        check("removed notice", tdb.textOr("base:str/room_removed") == "This item has been removed from your room.");
        check("quit prompt", tdb.textOr("base:str/quit_prompt") == "Are you sure you want to quit the game?");
    }
    {
        check("not in the room: offered to display", furnishingPrompt(2, 0) == FurnishingPrompt::Display && furnishingPrompt(3, 0) == FurnishingPrompt::Display);
        check("a placed wall, floor or decoration offers to remove it",
              furnishingPrompt(1, 1) == FurnishingPrompt::Remove && furnishingPrompt(2, 1) == FurnishingPrompt::Remove && furnishingPrompt(8, 2) == FurnishingPrompt::Remove);
        check("a placed furniture or bed can't be removed", furnishingPrompt(3, 1) == FurnishingPrompt::AlreadyShown && furnishingPrompt(4, 1) == FurnishingPrompt::AlreadyShown);
        check("slots per type: wall 1, floor 1, furniture 1, bed 1, type 5 3, type 6 3, type 7 2, type 8 20",
              slotsForType(*rom, 1) == 1 && slotsForType(*rom, 2) == 1 && slotsForType(*rom, 3) == 1 && slotsForType(*rom, 4) == 1 &&
                  slotsForType(*rom, 5) == 3 && slotsForType(*rom, 6) == 3 && slotsForType(*rom, 7) == 2 && slotsForType(*rom, 8) == 20);
        check("own 3 decorations: keeps offering Display until all 3 are out",
              furnishingPrompt(8, 1, 3, 20) == FurnishingPrompt::Display && furnishingPrompt(8, 2, 3, 20) == FurnishingPrompt::Display &&
                  furnishingPrompt(8, 3, 3, 20) == FurnishingPrompt::Remove);
        check("the type's slots cap it: 5 owned, 3 slots", furnishingPrompt(5, 2, 5, 3) == FurnishingPrompt::Display && furnishingPrompt(5, 3, 5, 3) == FurnishingPrompt::Remove);
        check("a floor has one slot, so Remove right after, even if you own more", furnishingPrompt(2, 1, 3, 1) == FurnishingPrompt::Remove);
        check("a placed furniture or bed is always 'already displayed'", furnishingPrompt(3, 1, 4, 1) == FurnishingPrompt::AlreadyShown && furnishingPrompt(4, 1, 2, 1) == FurnishingPrompt::AlreadyShown);
        SlotItems r{};
        r[2] = r[3] = 0x73;
        r[1] = 0x9e;
        removeFromRoom(r, 0x73);
        check("remove clears every slot holding the item and no other", r[2] == 0 && r[3] == 0 && r[1] == 0x9e);
    }
    {
        // placing: slot categories, the cursor's start, and the neighbour graph
        check("types 1..4 place at once, 5..8 use the cursor", placesImmediately(1) && placesImmediately(4) && !placesImmediately(5) && isFurnishingType(8) && !isFurnishingType(9));
        check("slot categories: wall, floor, three wall pieces, furniture, bed",
              slotCategory(*rom, 0) == 1 && slotCategory(*rom, 1) == 2 && slotCategory(*rom, 2) == 5 && slotCategory(*rom, 4) == 5 &&
                  slotCategory(*rom, 0x1b) == 3 && slotCategory(*rom, 0x1c) == 4);
        check("default slots: wall 0, floor 1, furniture 0x1b, bed 0x1c, type 5 -> 3, 6 -> 0x1e, 7 -> 0x19, 8 -> 8",
              defaultSlotForType(*rom, 1) == 0 && defaultSlotForType(*rom, 2) == 1 && defaultSlotForType(*rom, 3) == 0x1b &&
                  defaultSlotForType(*rom, 4) == 0x1c && defaultSlotForType(*rom, 5) == 3 && defaultSlotForType(*rom, 6) == 0x1e &&
                  defaultSlotForType(*rom, 7) == 0x19 && defaultSlotForType(*rom, 8) == 8);
        check("no default slot for types without one", defaultSlotForType(*rom, 0) == -1 && defaultSlotForType(*rom, 9) == -1 && defaultSlotForType(*rom, 40) == -1);
        check("the default slot of each type takes that type", [&]
              {
                  for (int t = 1; t <= 8; ++t)
                      if (slotCategory(*rom, defaultSlotForType(*rom, t)) != t)
                          return false;
                  return true;
              }());
        check("neighbours: slot 2 right is 3, slot 5 left is 17, edges stay", neighbourSlot(*rom, 2, SlotDir::Right) == 3 && neighbourSlot(*rom, 5, SlotDir::Left) == 17 && neighbourSlot(*rom, 2, SlotDir::Left) == 2);
        check("single-slot types stay put in every direction", [&]
              {
                  for (int s : {0, 1, 27, 28})
                      for (SlotDir d : {SlotDir::Left, SlotDir::Right, SlotDir::Up, SlotDir::Down})
                          if (neighbourSlot(*rom, s, d) != s)
                              return false;
                  return true;
              }());
        // From a type's default slot the cursor can reach every slot of that type, and never a slot of another type.
        bool closed = true, complete = true;
        for (int t = 5; t <= 8; ++t)
        {
            std::vector<bool> seen(kInventorySlots, false);
            std::vector<int> todo{defaultSlotForType(*rom, t)};
            seen[size_t(todo[0])] = true;
            while (!todo.empty())
            {
                const int s = todo.back();
                todo.pop_back();
                for (SlotDir d : {SlotDir::Left, SlotDir::Right, SlotDir::Up, SlotDir::Down})
                {
                    const int n = neighbourSlot(*rom, s, d);
                    if (n < 0 || n >= kInventorySlots || slotCategory(*rom, n) != t)
                        closed = false;
                    else if (!seen[size_t(n)])
                    {
                        seen[size_t(n)] = true;
                        todo.push_back(n);
                    }
                }
            }
            for (int s = 0; s < kInventorySlots; ++s)
                if (slotCategory(*rom, s) == t && !seen[size_t(s)])
                    complete = false;
        }
        check("the cursor never leaves its category", closed);
        check("the cursor reaches every slot of its category", complete);
    }
    {
        Player p;
        p.species = 1;
        p.equip = {{0, 0x95}, {1, 0xa2}, {0x1c, 0x72}};
        SaveData s;
        savePlayer(p, s);
        Player q;
        loadPlayer(s, q);
        check("equipment survives a save", q.equip == p.equip);
        p.tutorialsShown = 0b1100;
        SaveData s2;
        savePlayer(p, s2);
        Player q2;
        loadPlayer(s2, q2);
        check("the shown tutorials survive a save", q2.tutorialsShown == 0b1100);
        Player q3;
        loadPlayer(SaveData{}, q3);
        check("an old save has shown none", q3.tutorialsShown == 0);
        SaveData old;
        old.setInt("species", 2);
        Player r;
        loadPlayer(old, r);
        check("a save without equipment gets the starter home", r.equip == starterEquip(2) && r.equip.at(0) == 0x9c);
    }
    for (int i = 0; i < kRoomWallThemeCount; ++i)
        if (roomWallTheme(*rom, i).map == 0)
            check("wall theme with a map", false);
    for (int i = 0; i < kRoomFloorThemeCount; ++i)
        if (roomFloorTheme(*rom, i).map == 0)
            check("floor theme with a map", false);

    ContentDB db;
    importRoomTilesets(*rom, db);
    const Image img = renderRoom(*rom, db, roomTheme(*rom, 0, 0));
    check("room image is 1152 x 256", img.w == 1152 && img.h == 256);
    int magenta = 0, opaque = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
    {
        const uint8_t *p = &img.rgba[i];
        if (p[3] == 255)
            ++opaque;
        if (p[0] == 255 && p[1] == 0 && p[2] == 255)
            ++magenta;
    }
    check("the render is mostly opaque", opaque > img.w * img.h / 2);
    check("no key-colour magenta left", magenta == 0);

    check("the same band, shifted right by half the extra width", roomScroll(0, 600, kRoomWidthTiles, 426) == 600 - (kRoomScrollTrail + 53));
    check("and the room's right end moves in by the extra width", roomScroll(0, 1100, kRoomWidthTiles, 426) == kRoomWidthTiles * kRoomTilePx - 426 &&
                                                                  roomScroll(0, 1100) == kRoomWidthTiles * kRoomTilePx - 320);
    check("never scrolls left of the room", roomScroll(0, 100, kRoomWidthTiles, 426) == 0);

    std::printf(fails ? "room_scene_test: %d failure(s)\n" : "room_scene_test: all passed\n", fails);
    return fails ? 1 : 0;
}
