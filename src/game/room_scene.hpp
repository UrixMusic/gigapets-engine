// room_scene.hpp - the player homes and hotels: the side-scrolling pet rooms (the "indoor" scenes).
//
// Pressing Z on a home or hotel door (ProcessStoryModeObjectInteraction 0x013546) plays sound 0x13, stores a location
// number in g_storyInteractionResultCode and calls SwitchGameState(MapLocationToGameState(location)); GameMainLoop sends
// those states (0xb-0xd, 0x15-0x18) to EnterActivityAndRunLoop (0x04ab19), the room loop. Leaving returns to the area
// loop, which puts the pet back at the door (RunPetRoomGameplayLoop 0x03c39c, by return state).
//
// Locations: 0, 1, 2 = the player homes (one per save slot); 4, 5, 6, 7 = the hotels. Location 3 is not used.
//
// A room is 36 x 8 tiles of 32 px, two background layers: the wall behind and the floor in front. Both layers come from
// theme tables, picked by the furnishing items equipped in inventory slot 0 (wall) and slot 1 (floor): the item's w9
// field is the theme index (GetAreaItemSpriteId1/2 0x04efc9, DrawStarterPetPreviewPanel 0x01da67). A hotel's furnishing
// is a fixed preset inventory in the ROM (0xf689...). Furniture and decorations are drawn on top from a decoration
// table (RenderSceneDecorationSprites 0x04b0eb); that table is built in RAM at boot and is not decoded yet.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include "rom.hpp"
#include "../content/records.hpp"

namespace gp
{

    constexpr int kRoomWidthTiles = 0x24; // every home and hotel: 36 tiles = 1152 px
    constexpr int kRoomHeightTiles = 8;   // 256 px
    constexpr int kRoomTilePx = 32;

    constexpr uint32_t kRoomWallThemes = 0xc485;  // 4-word records: width, map, tile frame, palette
    constexpr uint32_t kRoomFloorThemes = 0xc4b2; // the same array read one word in: map, tile frame, palette, (next width)
    constexpr int kRoomWallThemeCount = 11, kRoomFloorThemeCount = 5;
    constexpr uint32_t kHotelInventories = 0xf689; // 32 words each; location 4, 7, 5, 6 in that order
    constexpr int kInventoryWallSlot = 0, kInventoryFloorSlot = 1;

    // Pet placement in a room (EnterActivityAndRunLoop 0x04ab19, HandlePetStageEvents 0x01308a).
    constexpr int kRoomDoorX = 0x182, kRoomDoorY = 0x98; // where the pet comes in and goes out (activities 3 and 4)
    constexpr int kRoomFloorY = 0xba;                    // the pet walks along this line
    constexpr int kRoomMinX = 0x40;                      // the pet's x runs 0x40 .. width * 32 - 0x40
    constexpr int kRoomStepPx = 2;                       // pixels per tick, no vertical movement
    constexpr int kRoomScrollLead = 0x40, kRoomScrollTrail = 0x100; // the pet stays 0x40..0x100 px from the screen's left edge
    constexpr int kRoomEnterSound = 0x13;

    struct SceneLayer
    {
        uint32_t map = 0; // ROM word address of width * 8 tile ids, row-major
        int frame = 0;    // tile sheet (frame table record)
        int palette = 0;
    };

    struct RoomTheme
    {
        SceneLayer wall, floor;
    };

    inline SceneLayer roomWallTheme(const Rom &rom, int index)
    {
        const uint32_t a = kRoomWallThemes + uint32_t(index < 0 ? 0 : index) * 4;
        return {rom.u16(a + 1), int(rom.u16(a + 2)), int(rom.u16(a + 3))};
    }

    inline SceneLayer roomFloorTheme(const Rom &rom, int index)
    {
        const uint32_t a = kRoomFloorThemes + uint32_t(index < 0 ? 0 : index) * 4;
        return {rom.u16(a), int(rom.u16(a + 1)), int(rom.u16(a + 2))};
    }

    inline RoomTheme roomTheme(const Rom &rom, int wallIndex, int floorIndex)
    {
        return {roomWallTheme(rom, wallIndex), roomFloorTheme(rom, floorIndex)};
    }

    // A hotel's preset furnishing: the ROM inventory for this location, or -1 if it isn't a hotel.
    inline int hotelInventoryIndex(int location)
    {
        switch (location)
        {
        case 4:
            return 0;
        case 7:
            return 1;
        case 5:
            return 2;
        case 6:
            return 3;
        default:
            return -1;
        }
    }
    inline uint32_t hotelInventoryAddr(int location)
    {
        const int i = hotelInventoryIndex(location);
        return i < 0 ? 0 : kHotelInventories + uint32_t(i) * 32;
    }
    inline int hotelItemInSlot(const Rom &rom, int location, int slot)
    {
        const uint32_t a = hotelInventoryAddr(location);
        return a ? int(rom.u16(a + uint32_t(slot))) : 0;
    }

    // Where the room's door puts the pet back in the area it was entered from (RunPetRoomGameplayLoop, by return state).
    struct RoomExitPoint
    {
        int x, y;
    };
    inline RoomExitPoint roomReturnPoint(int location)
    {
        switch (location)
        {
        case 0:
            return {0x826, 0x38d}; // home, save slot 0 (area 0)
        case 1:
            return {0x7c7, 0x4cf}; // home, save slot 1 (area 0)
        case 2:
            return {0x6cc, 0x40d}; // home, save slot 2 (area 0)
        case 4:
            return {0x1dc, 0x32d}; // hotel (area 2)
        case 5:
            return {0x7da, 0x2ff}; // hotel (area 3)
        case 6:
            return {0x273, 0x467}; // hotel (area 6)
        case 7:
            return {0x661, 0x526}; // hotel (area 1)
        default:
            return {0x826, 0x38d};
        }
    }

    // ---- the decorations: the door, the bed, furniture and wall pieces ----
    // The scene table at 0xc43d has one 9-word record per location 0..7; the last word is the RAM address of that room's
    // decoration table: homes 0..2 -> 0x1eff, hotels 4..7 -> 0x1f35 (location 3 is not a room). Boot (BootInitVars2) copies
    // the initial tables from ROM 0x5a90b / 0x5a941 into RAM, so the ROM address is the RAM one plus a constant. An entry
    // is 6 words: x, y, sprite, palette, type, flag. Type 5 is probably "the pet can act on this" (door, bed, one piece of
    // furniture), 0xffff a plain decoration; the flag's meaning is not known. A table is 9 entries.
    constexpr uint32_t kRoomSceneTable = 0xc43d;
    constexpr int kRoomSceneRecordWords = 9, kRoomSceneDecorWord = 8;
    constexpr uint32_t kRoomDecorRomOffset = 0x5a90b - 0x1eff;
    constexpr int kRoomDecorCount = 9, kRoomDecorWords = 6;

    struct RoomDecoration
    {
        int x = 0, y = 0, sprite = 0, palette = 0, type = 0, flag = 0;
    };

    inline std::vector<RoomDecoration> roomDecorations(const Rom &rom, int location)
    {
        std::vector<RoomDecoration> out;
        if (location < 0 || location > 7)
            return out;
        const uint32_t ram = rom.u16(kRoomSceneTable + uint32_t(location) * kRoomSceneRecordWords + kRoomSceneDecorWord);
        const uint32_t a = ram + kRoomDecorRomOffset;
        for (int i = 0; i < kRoomDecorCount; ++i)
        {
            const uint32_t e = a + uint32_t(i) * kRoomDecorWords;
            out.push_back({int(rom.u16(e)), int(rom.u16(e + 1)), int(rom.u16(e + 2)), int(rom.u16(e + 3)), int(rom.u16(e + 4)),
                           int(rom.u16(e + 5))});
        }
        return out;
    }

    // ---- which decoration the pet is next to (FindDecorationNearPetX 0x04b217) ----
    // Entries with a type above zero can be acted on: 0 the door, 1 the bed, 4 the food bowl. One is "near" when the pet's x
    // is within the sprite's half width plus the type value (5) of the entry's x. Later entries win. `halfWidth` is how far
    // the sprite reaches left of its x (the ROM's GetTileDimensions); the ROM reads it from the sprite.
    constexpr int kDecorDoor = 0, kDecorBed = 1, kDecorBowl = 4;
    inline int nearbyDecoration(const std::vector<RoomDecoration> &table, int petX,
                                const std::function<int(const RoomDecoration &)> &halfWidth)
    {
        int found = -1;
        for (size_t i = 0; i < table.size(); ++i)
        {
            const RoomDecoration &e = table[i];
            if (static_cast<int16_t>(e.type) <= 0)
                continue;
            const int reach = halfWidth(e) + e.type;
            if (petX >= e.x - reach && petX <= e.x + reach)
                found = int(i);
        }
        return found;
    }
    // ---- eating at the food bowl (HandlePetStageEvents, Pet_RepeatedTrickWithDialogue 0x015ab1) ----
    // Z at the bowl applies item 0x1c's food effect at once and starts activity 6: the pet walks along the floor to a point
    // short of the bowl, then up the diagonal to it, takes 2 or 3 bites (each: lean in, chew 2..4 times, a short pause) while
    // the bowl's food sprite (table entry 3) empties, walks back down, turns to the front and does the happy reaction (+6
    // Happiness while it is below 0x1a). The bowl is at x 0x208, y 0xad (the panda: 0x1c3, 0xa1, approached from the left).
    constexpr int kBowlFoodItem = 0x1c;
    constexpr int kBowlEntry = 3; // table entry whose sprite is the food in the bowl
    constexpr int kBowlFull = 0x95d;
    struct BowlTarget
    {
        int x, y;
    };
    constexpr BowlTarget bowlTarget(int species) { return species == 2 ? BowlTarget{0x1c3, 0xa1} : BowlTarget{0x208, 0xad}; }
    // Where the pet first walks to along the floor: the foot of the diagonal that ends on the bowl (2 px in x per 1 in y).
    constexpr int bowlApproachX(int species, int petY)
    {
        return (petY - bowlTarget(species).y) * (species == 2 ? -2 : 2) + bowlTarget(species).x;
    }
    // The bowl's sprite while a bite is eaten, by the bites left (g_petRepeatCount); -1 = leave it as it is.
    constexpr int bowlSpriteForBites(int bitesLeft)
    {
        return bitesLeft == 3 ? 0x95e : bitesLeft == 2 ? 0x963 : bitesLeft == 1 ? 0x961 : -1;
    }
    constexpr int kHappinessCap = 0x1a, kBowlHappiness = 6;
    // The pause after the chews (TickActor_RepeatAnimThenPause 0x01ea5a, read off the listing, not the decompile): the ROM
    // waits until (ticks elapsed / 59.94) >= RandRange(1,4) / 20.0 seconds, so 0.05, 0.10 or 0.15 s. In 30 Hz steps, rounded up.
    constexpr int bowlPauseTicks(int unit) { return (unit * 30 + 19) / 20; } // unit = 1, 2 or 3

    // ---- sleeping in the bed (Pet_RepeatedTrickWithOddsCheck 0x01585e = activity 5; TickIdleTimeoutSleepSequence 0x0159df
    // = activity 0x25, the quit) ----
    // Z at the bed, or the bar's sleep button: a yawn (the bored reaction), a walk along the floor to 78 px short of the bed
    // and up the diagonal to it, getting into bed, then sleep cycles. Each cycle gives +10 Tiredness; after the third it stops
    // as soon as Tiredness is above 0x5f. Then the pet gets out, walks back down, turns to the front, does the happy hop and
    // gets the same +10 Happiness top-up as a wash. The quit version is the same walk and three cycles with no stat change,
    // and then the game stops (the ROM halts; here the game saves and returns to the menu).
    constexpr int kBedX = 0x39e, kBedY = 0x93;
    constexpr int kSleepCycles = 3, kSleepTiredness = 10, kRestedTiredness = 0x5f;
    constexpr int bedApproachX(int petY) { return kBedX + (petY - kBedY) * -2; }
    // After a cycle: `cyclesLeft` is what remains once this one is counted. True when the sleep is over.
    constexpr bool sleepOver(int cyclesLeft, int tiredness, bool quitting)
    {
        return cyclesLeft < 1 && (quitting || tiredness > kRestedTiredness);
    }
    // The sleep cycle animation runs at 5 fps for the dog and 7 for the others.
    constexpr int sleepCycleFps(int species) { return species == 0 ? 5 : 7; }

    // ---- another save slot's home (EnterActivityAndRunLoop 0x04ab19) ----
    // Entering home 0..2 that isn't your own: if that slot has a save, its pet is there at (0x28a, 0xb0) in the indoor
    // settled-idle pose (species and colour read from that save), and standing within 0x1e px of it makes it the nearby
    // thing (index 5, which wins over the table entries). Z on it opens the item screen in mode 2 to give it items. An unused
    // slot is "FOR RENT": an empty house with no pet, and the door is the only thing that can be selected.
    constexpr int kVisitorX = 0x28a, kVisitorY = 0xb0, kVisitorReach = 0x1e, kDecorVisitor = 5;
    constexpr bool nearVisitor(int petX) { return petX > kVisitorX - kVisitorReach && petX < kVisitorX + kVisitorReach; }
    constexpr int visitRules(int nearby, int petX, bool hasVisitor, bool forRent)
    {
        if (forRent)
            return nearby == kDecorDoor ? nearby : -1;
        return hasVisitor && nearVisitor(petX) ? kDecorVisitor : nearby;
    }

    // ---- giving items to the pet whose home you are in (item screen mode 2: Item_HasCarryRoom 0x02d3ed,
    // ItemMenuScreen, GiveItemToOtherSave 0x02e189) ----
    // The list only shows what can be given: not the mini-pet types (12, 14, 15, 16, 17), not a furniture or bed that is placed
    // in your room, and nothing the other pet already has the most of. The picker's most is the smaller of what you have and
    // what the other pet has room for; confirming moves that many into their save, capped at the item's most.
    constexpr bool giveExcludedType(int itemType) { return itemType == 12 || (itemType >= 14 && itemType <= 17); }
    inline bool canGiveItem(const ItemDef &d, bool placedInMyRoom, int theirCount)
    {
        if (giveExcludedType(d.type))
            return false;
        if ((d.type == 3 || d.type == 4) && placedInMyRoom)
            return false;
        return theirCount < d.maxQty;
    }
    constexpr int giveMax(int mine, int maxQty, int theirs)
    {
        const int room = maxQty - theirs;
        const int m = mine < room ? mine : room;
        return m < 0 ? 0 : m;
    }
    constexpr int theirCountAfterGift(int theirs, int qty, int maxQty) { return theirs + qty > maxQty ? maxQty : theirs + qty; }

    // Where the selector arrow goes for an entry: over the sprite's top, but never above y = 0x32.
    constexpr int kSelectorMinY = 0x32;
    struct SelectorPos
    {
        int x, y;
    };
    constexpr SelectorPos selectorPosition(const RoomDecoration &e, int spriteHeight)
    {
        const int y = e.y - spriteHeight;
        return {e.x, y < kSelectorMinY ? kSelectorMinY : y};
    }

    // ---- what a room shows: the table above plus the 32 equipped inventory slots ----
    // EnterActivityAndRunLoop draws, back to front: the background, the table entries flagged 1 (the door, the wall hooks,
    // and the bed's decor sprite), every equipped item of type 5 or more at its slot's position (RenderWorldItemSprites),
    // the table entries flagged 0 that are above the pet (the bed's w9 sprite, the furniture's decor and w9 sprites, the
    // literal piece), the pet, and then the flag-0 entries below it. Types 1..4 (wall, floor, furniture, bed) are not
    // drawn as items: the wall and floor are the background themes and the furniture and bed are table entries 1, 2, 4.
    constexpr int kInventorySlots = 32;
    constexpr uint32_t kSlotPositions = 0xf895; // word pairs (x, y) per slot
    constexpr int kFurnitureSlot = 0x1b, kBedSlot = 0x1c;
    constexpr int kNoSprite = 0xffff;
    using SlotItems = std::array<int, kInventorySlots>; // item number per slot, 0 = empty

    struct SlotPos
    {
        int x, y;
    };
    inline SlotPos slotPosition(const Rom &rom, int slot)
    {
        return {int(rom.u16(kSlotPositions + uint32_t(slot) * 2)), int(rom.u16(kSlotPositions + uint32_t(slot) * 2 + 1))};
    }

    inline SlotItems hotelSlotItems(const Rom &rom, int location)
    {
        SlotItems s{};
        const uint32_t a = hotelInventoryAddr(location);
        if (a)
            for (int i = 0; i < kInventorySlots; ++i)
                s[size_t(i)] = rom.u16(a + uint32_t(i));
        return s;
    }

    struct RoomSprite
    {
        int x, y, sprite, palette;
    };
    struct RoomSprites
    {
        std::vector<RoomSprite> behind;    // under everything else (and the pet)
        std::vector<RoomSprite> items;     // the equipped items of type 5 or more
        std::vector<RoomSprite> beforePet; // table entries above the pet's floor line
        std::vector<RoomSprite> afterPet;  // below it (none in the base tables)
    };

    inline RoomSprites roomSprites(const Rom &rom, const std::vector<RoomDecoration> &table, const SlotItems &slots,
                                   const std::vector<ItemDef> &items)
    {
        RoomSprites out;
        auto item = [&](int slot) -> const ItemDef *
        {
            const int id = slots[size_t(slot)];
            return id > 0 && id < int(items.size()) ? &items[size_t(id)] : nullptr;
        };
        auto push = [&](std::vector<RoomSprite> &layer, const RoomDecoration &e, int sprite, int palette)
        {
            if (sprite != kNoSprite)
                (e.y > kRoomFloorY ? out.afterPet : layer).push_back({e.x, e.y, sprite, palette});
        };
        for (size_t i = 0; i < table.size(); ++i) // flag 1: behind everything; the bed's decor sprite is always here
        {
            const RoomDecoration &e = table[i];
            if (i == 1)
            {
                if (const ItemDef *b = item(kBedSlot))
                    push(out.behind, e, b->decorSprite, b->w7);
            }
            else if (e.flag == 1)
                out.behind.push_back({e.x, e.y, e.sprite, e.palette});
        }
        for (int slot = 0; slot < kInventorySlots; ++slot)
            if (const ItemDef *it = item(slot); it && (it->type < 1 || it->type > 4) && it->decorSprite != kNoSprite)
                out.items.push_back({int(rom.u16(kSlotPositions + uint32_t(slot) * 2)),
                                     int(rom.u16(kSlotPositions + uint32_t(slot) * 2 + 1)), it->decorSprite, it->w7});
        for (size_t i = 0; i < table.size(); ++i) // flag 0: the bed's w9 sprite, the furniture's two, the literal piece
        {
            const RoomDecoration &e = table[i];
            if (i == 1)
            {
                if (const ItemDef *b = item(kBedSlot))
                    push(out.beforePet, e, b->w9, b->w7);
            }
            else if (i == 2 || i == 4)
            {
                if (const ItemDef *f = item(kFurnitureSlot))
                    push(out.beforePet, e, i == 2 ? f->decorSprite : f->w9, f->w7);
            }
            else if (e.flag == 0)
                push(out.beforePet, e, e.sprite, e.palette);
        }
        return out;
    }

    // ---- placing a furnishing (InitPetWalkToItem 0x04eaee, HandleMapNavigationInput 0x04eb37, UpdatePetWalkToItem 0x04ec08) ----
    // Used from the item menu inside the player's own home; the room is shown without the pet. The item's type decides
    // which slots can take it (slot category, below). Types 1..4 (wall, floor, furniture, bed) have exactly one slot and
    // are applied at once (sound 0x59). Types 5..8 start a cursor on the type's default slot; Left/Right/Up/Down move it
    // along the neighbour table, Z (event 4) confirms, X (event 5) cancels. The marker flies to the slot's position at 4 px
    // per tick in x and y; when it arrives SetInventorySlot(slot, item) runs and the menu comes back.
    constexpr uint32_t kSlotCategories = 0xf548;     // item type each slot takes
    constexpr uint32_t kDefaultSlotByType = 0xf537;  // by item type: the slot the cursor starts on (0x20 = none)
    constexpr uint32_t kSlotNeighbours = 0xf568;     // 4 words per slot: left, right, up, down
    constexpr int kPlaceFlightStep = 4, kPlaceSound = 0x59;
    // After the item is put down the finished room stays up for a moment, then fades back to the item screen (the ROM
    // switches game state straight after SetInventorySlot). The hold is by eye: "maybe a second".
    constexpr int kPlacedHoldTicks = 30;
    enum class SlotDir
    {
        Left,
        Right,
        Up,
        Down
    };

    constexpr bool placesImmediately(int itemType) { return itemType >= 1 && itemType <= 4; }
    constexpr bool isFurnishingType(int itemType) { return itemType >= 1 && itemType <= 8; }
    // What using a furnishing asks (ItemMenuScreen, confirmed in MAME): an item that isn't in the room is offered ("Display
    // this in your room?", Yes starts the placement above); one that is gets "Remove this item from your room?" and Yes
    // clears it from every slot (Inventory_RemoveAllOfItem); a furniture or bed that is already placed can only be
    // replaced, so it just says it is displayed. Neither changes how many of the item you own.
    enum class FurnishingPrompt
    {
        Display,
        Remove,
        AlreadyShown
    };
    // `owned` is how many of the item the player has (never more than its max quantity) and `categorySlots` how many slots
    // take this type. A decoration keeps offering "Display" until that many copies are out, and only then "Remove".
    constexpr FurnishingPrompt furnishingPrompt(int itemType, int slotsHoldingIt, int owned = 1, int categorySlots = 1)
    {
        if (slotsHoldingIt <= 0)
            return FurnishingPrompt::Display;
        if (itemType == 3 || itemType == 4)
            return FurnishingPrompt::AlreadyShown;
        const int limit = owned < categorySlots ? owned : categorySlots;
        return slotsHoldingIt < (limit < 1 ? 1 : limit) ? FurnishingPrompt::Display : FurnishingPrompt::Remove;
    }
    // Remove: the item goes from every slot that holds it.
    inline void removeFromRoom(SlotItems &slots, int item)
    {
        for (int &s : slots)
            if (s == item)
                s = 0;
    }

    inline int slotCategory(const Rom &rom, int slot) { return int(rom.u16(kSlotCategories + uint32_t(slot))); }
    inline int slotsForType(const Rom &rom, int itemType)
    {
        int n = 0;
        for (int s = 0; s < kInventorySlots; ++s)
            n += slotCategory(rom, s) == itemType;
        return n;
    }
    inline int defaultSlotForType(const Rom &rom, int itemType)
    {
        if (itemType < 0 || itemType > 16)
            return -1;
        const int s = int(rom.u16(kDefaultSlotByType + uint32_t(itemType)));
        return s < kInventorySlots ? s : -1;
    }
    inline int neighbourSlot(const Rom &rom, int slot, SlotDir d)
    {
        return int(rom.u16(kSlotNeighbours + uint32_t(slot) * 4 + uint32_t(d)));
    }

    // MapLocationToGameState (the bar's "go home" mapping, RemapZoneIndexToAudioId 0x04b282).
    constexpr bool isRoomLocation(int location)
    {
        return (location >= 0 && location <= 2) || (location >= 4 && location <= 7);
    }
    constexpr bool isHotel(int location) { return location >= 4 && location <= 7; }

    // The pet's x range, and the scroll rule that follows it (DrawStarterPetPreviewPanel).
    constexpr int roomMaxX(int widthTiles = kRoomWidthTiles) { return widthTiles * kRoomTilePx - 0x40; }
    inline int roomClampX(int x, int widthTiles = kRoomWidthTiles)
    {
        return x < kRoomMinX ? kRoomMinX : x > roomMaxX(widthTiles) ? roomMaxX(widthTiles) : x;
    }
    // The background scroll for a pet at `petX`: it keeps the pet 0x40..0x100 px from the left edge, within the room. In a view wider
    // than 320 (16:9) the same band moves right by half the extra width, so the pet stays in the same place relative to the middle.
    inline int roomScroll(int scroll, int petX, int widthTiles = kRoomWidthTiles, int viewW = 320)
    {
        const int shift = (viewW - 320) / 2;
        if (petX < scroll + kRoomScrollLead + shift)
            scroll = petX - kRoomScrollLead - shift;
        else if (scroll + kRoomScrollTrail + shift < petX)
            scroll = petX - kRoomScrollTrail - shift;
        const int maxScroll = widthTiles * kRoomTilePx - viewW;
        return scroll < 0 ? 0 : scroll > maxScroll ? maxScroll : scroll;
    }

} // namespace gp
