// walk_items.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of ItemScreen.
// What they need from the rest of the walk session comes in through ItemScreenEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct ItemScreenEnv
{
    std::function<AnimRef(const std::string&)> animRefById;
    BarAnim&barAnim;
    int&barBusy;
    std::function<bool(const gp::ItemDef&, int)> categoryMatches;
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    gp::Dialogue &dialogue;
    std::function<void(RoomView &, int, int)> equipSlot;
    int&facing;
    int &giveLocation;
    bool &giveMode;
    std::map<int, int> &giveTheirs;
    ItemMenu&im;
    std::function<void()> importPetAnims;
    const std::vector<gp::ItemDef>&itemDefs;
    bool &itemLeave;
    bool&medStuck;
    int&medStuckLimit;
    int&medStuckTicks;
    int&money;
    gp::PendingList &pending;
    int &petPal;
    Pose&pose;
    std::function<void(std::function<void()>)> requestScreen;
    gp::Rom&rom;
    std::unique_ptr<RoomView> &room;
    int &roomMenuCategory;
    std::function<void()> &saveGame;
    gp::ScriptHost &scripts;
    bool &sellMode;
    std::function<void(int, int)> sfxFn;
    gp::ShopStock &shopStock;
    int &slot;
    int &sp;
    std::function<void(RoomView &, bool)> startEating;
    std::function<void()> startFeed;
    std::function<void(int)> startMedicine;
    std::function<void()> startPotionReaction;
    std::function<void(RoomView &, int)> startRoomMedicine;
    WalkServices &svc;
    int&walkTicks;
};

class ItemScreen : public ItemScreenEnv
{
public:
    explicit ItemScreen(ItemScreenEnv env) : ItemScreenEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }

    auto ownedCount(int id)
    {
        if (id < 0 || id >= int(itemDefs.size()))
            return 0;
        return player.itemCount(itemDefs[size_t(id)].id);
    }

    auto itemName(int id)
    { return db.textOr(itemDefs[id].name, "?"); }

    auto buildFiltered(int category)
    {
        std::vector<int> out;
        for (const gp::ItemDef &d : itemDefs)
        {
            if (ownedCount(d.index) <= 0 || !categoryMatches(d, category))
                continue;
            if (sellMode && gp::sellExcludedType(d.type)) // mode 1 hides maps, quest and special items, potions
                continue;
            if (giveMode)
            {
                bool placed = false; // Inventory_FindSlotByItemId: is it one of my room's furnishings?
                for (const auto &[s, item] : player.equip)
                    placed = placed || item == d.index;
                if (!gp::canGiveItem(d, placed, giveTheirs.count(d.index) ? giveTheirs.at(d.index) : 0))
                    continue;
            }
            out.push_back(d.index);
        }
        return out;
    }

    auto itemWindow()
    {
        std::vector<int> w;
        for (size_t i = size_t(im.firstRow - 1) * 6; i < im.filtered.size() && w.size() < 18; ++i)
            w.push_back(im.filtered[i]);
        return w;
    }

    auto itemRows()
    { return std::max(1, (int(im.filtered.size()) - 1) / 6 + 1); }

    auto tabAnimId(bool in)
    { return gp::Id(std::string("base:anim/") + std::to_string(in ? gp::kTabAnimIn : gp::kTabAnimOut) + "@28c"); }

    auto itemMenuTick(uint8_t pressed, bool xPress, bool menuPress, bool zPress, bool zRelease)
    {
        gp::ItemTabs &tb = im.tabs;
        if (tb.sliding())
        {
            tb.tick(int(animRefById(tabAnimId(tb.sub == gp::MenuSub::ToTabs)).size()));
            return;
        }
        if (tb.inTabs())
        {
            if (pressed & gp::DirUp)
                tb.press(gp::ItemTabs::Up, im.category);
            if (pressed & gp::DirDown)
                tb.press(gp::ItemTabs::Down, im.category);
            if (zPress)
                tb.press(gp::ItemTabs::Select, im.category);
            if (xPress)
                tb.press(gp::ItemTabs::Cancel, im.category);
            else if (menuPress)
                tb.press(gp::ItemTabs::Menu, im.category);
            int cat = im.category;
            if (zRelease && tb.release(cat))
            {
                im.category = cat;
                im.filtered = buildFiltered(cat);
                im.selRow = im.selCol = im.firstRow = 1;
            }
            return;
        }
        if (menuPress && tb.openList(im.category))
            return;
        auto cellValid = [&](int row, int col)
        { return size_t((row - 1) * 6 + col - 1) < im.filtered.size(); };
        int r = im.selRow, c = im.selCol;
        if (pressed & gp::DirLeft)
            c = std::max(1, c - 1);
        if (pressed & gp::DirRight)
            c = std::min(6, c + 1);
        if (pressed & gp::DirUp)
            r = std::max(1, r - 1);
        if (pressed & gp::DirDown)
            r = std::min(itemRows(), r + 1);
        if (cellValid(r, c))
        {
            im.selRow = r;
            im.selCol = c;
        }
        if (im.selRow < im.firstRow)
            im.firstRow = im.selRow;
        if (im.selRow > im.firstRow + 2)
            im.firstRow = im.selRow - 2;
        if (xPress)
            itemLeave = true; // (the caller fades out, then closes the screen)
    }

    // ItemMenuScreen, furnishings (confirmed in MAME): "Display this in your room?" for an item that isn't in the room
    // (Yes starts placing it), "Remove this item from your room?" for one that is (Yes clears every slot holding it),
    // and a furniture or bed that is already shown can only be replaced. Nothing changes how many you own.
    auto useFurnishing(const gp::ItemDef &d)
    {
        using P = RoomView::Phase;
        if (!room || room->location != slot) // only in your own home
            return;
        RoomView &r = *room;
        if (r.phase == P::Entering || r.phase == P::Leaving || r.phase == P::Placing || r.phase == P::Placed || r.phase == P::Eating)
            return;
        int held = 0;
        for (int s : r.slots)
            held += s == d.index;
        // As many copies as you own (up to the item's max quantity) can be out, if there are slots for them.
        const int owned = d.maxQty > 0 ? std::min(player.itemCount(d.id), d.maxQty) : player.itemCount(d.id);
        sfx(gp::kPlaceSound);
        // The item screen stays up behind the box and only closes when placing starts.
        auto ask = [&](const char *key, std::function<void()> yes)
        {
            gp::DialogueBox b = gp::messageBox(db.textOr(std::string("base:str/") + key));
            b.kind = gp::BoxKind::Confirm;
            b.onYes = std::move(yes);
            b.onNo = [] {};
            dialogue.show(std::move(b));
        };
        const int item = d.index, type = d.type;
        switch (gp::furnishingPrompt(type, held, owned, gp::slotsForType(rom, type)))
        {
        case gp::FurnishingPrompt::Display:
            ask("room_display_prompt", [&, item, type]
                {
                    if (!room)
                        return;
                    const int s = gp::defaultSlotForType(rom, type);
                    if (s < 0)
                        return;
                    roomMenuCategory = im.category;
                    im.open = false; // Yes: the item screen fades out and the room comes back to place it
                    barBusy = 0;
                    barAnim.visible = false;
                    RoomView &rv = *room;
                    const gp::SlotPos p = gp::slotPosition(rom, s);
                    rv.placeItem = item;
                    rv.placeSlot = s;
                    rv.curX = p.x;
                    rv.curY = p.y;
                    rv.placeConfirmed = gp::placesImmediately(type); // wall, floor, furniture, bed: no choice to make
                    if (rv.placeConfirmed)
                        sfx(gp::kPlaceSound);
                    rv.phase = P::Placing; });
            break;
        case gp::FurnishingPrompt::Remove:
            ask("room_remove_prompt", [&, item]
                {
                    if (!room)
                        return;
                    for (int s = 0; s < gp::kInventorySlots; ++s)
                        if (room->slots[size_t(s)] == item)
                            equipSlot(*room, s, 0);
                    dialogue.show(gp::messageBox(db.textOr("base:str/room_removed"))); });
            break;
        case gp::FurnishingPrompt::AlreadyShown:
            dialogue.show(gp::messageBox(db.textOr("base:str/room_already_shown")));
            break;
        }
    }

    // An item chosen in give mode: the picker ("How many do you want to give?"), then GiveItemToOtherSave: their count goes
    // up (never past the item's most) and is written to their save, and mine goes down.
    auto giveSelected(const gp::ItemDef &d)
    {
        bool placed = false;
        for (const auto &[s, item] : player.equip)
            placed = placed || item == d.index;
        if (placed && (d.type == 3 || d.type == 4)) // the list hides these; the ROM has the wording anyway
        {
            dialogue.show(gp::messageBox(db.textOr(d.type == 4 ? "base:str/give_no_bed" : "base:str/give_no_dish")));
            return;
        }
        const int theirs = giveTheirs.count(d.index) ? giveTheirs.at(d.index) : 0;
        const int most = gp::giveMax(player.itemCount(d.id), d.maxQty, theirs);
        if (most < 1)
            return;
        gp::DialogueBox b = gp::messageBox(db.textOr("base:str/give_prompt"));
        b.kind = gp::BoxKind::Quantity;
        b.qty = 1;
        b.qtyMax = most;
        b.onQuantity = [&, idx = d.index](int qty)
        {
            const gp::ItemDef &g = itemDefs[size_t(idx)];
            gp::SaveData s;
            if (!gp::loadFromFile(gp::slotPath(giveLocation), s))
                return;
            std::map<std::string, int> ids;
            if (s.has("itemIds"))
                ids = s.getIdMap("itemIds");
            else
                for (const auto &[n, c] : s.getMap("inventory"))
                    if (n >= 0 && n < int(itemDefs.size()))
                        ids[itemDefs[size_t(n)].id] = c;
            const int now = gp::theirCountAfterGift(ids.count(g.id) ? ids.at(g.id) : 0, qty, g.maxQty);
            ids[g.id] = now;
            s.setIdMap("itemIds", ids);
            gp::saveToFile(gp::slotPath(giveLocation), s);
            giveTheirs[idx] = now;
            player.removeItem(g.id, qty);
            saveGame();
            std::printf("gave %d of item %d (they have %d)\n", qty, idx, now);
            // The list shrinks as things are given; keep the cursor on a real cell, or leave when nothing is left.
            im.filtered = buildFiltered(im.category);
            if (im.filtered.empty())
            {
                im.open = false;
                barBusy = 0;
                barAnim.visible = false;
                return;
            }
            int i = std::min((im.selRow - 1) * 6 + im.selCol - 1, int(im.filtered.size()) - 1);
            im.selRow = i / 6 + 1;
            im.selCol = i % 6 + 1;
            im.firstRow = std::max(1, std::min(im.firstRow, im.selRow));
            if (im.selRow > im.firstRow + 2)
                im.firstRow = im.selRow - 2;
        };
        dialogue.show(std::move(b));
    }

    // ItemMenuScreen mode 1: the bed and food dish in your room can't be sold; otherwise ask how many (1 up to what you
    // hold), then Item_ProcessSale: the items go and int(qty * price * 0.8) Bucks come in.
    auto sellSelected(const gp::ItemDef &d)
    {
        bool placed = false;
        for (const auto &[s, item] : player.equip)
            placed = placed || item == d.index;
        if (placed && (d.type == 3 || d.type == 4))
        {
            dialogue.show(gp::messageBox(db.textOr(d.type == 4 ? "base:str/sell_no_bed" : "base:str/sell_no_dish")));
            return;
        }
        const int held = player.itemCount(d.id);
        if (held < 1)
            return;
        gp::DialogueBox b = gp::messageBox(db.textOr("base:str/sell_prompt"));
        b.kind = gp::BoxKind::Quantity;
        b.qty = 1;
        b.qtyMax = held;
        b.onQuantity = [&, idx = d.index](int qty)
        {
            const gp::ItemDef &g = itemDefs[size_t(idx)];
            const int pay = gp::saleTotal(g.price, qty);
            player.removeItem(g.id, qty);
            player.adjustMoney(pay);
            saveGame();
            std::printf("sold %d of item %d for %d\n", qty, idx, pay);
            im.filtered = buildFiltered(im.category); // the list shrinks; keep the cursor on a real cell, or leave when empty
            if (im.filtered.empty())
            {
                im.open = false;
                barBusy = 0;
                barAnim.visible = false;
                return;
            }
            int i = std::min((im.selRow - 1) * 6 + im.selCol - 1, int(im.filtered.size()) - 1);
            im.selRow = i / 6 + 1;
            im.selCol = i % 6 + 1;
            im.firstRow = std::max(1, std::min(im.firstRow, im.selRow));
            if (im.selRow > im.firstRow + 2)
                im.firstRow = im.selRow - 2;
        };
        dialogue.show(std::move(b));
    }

    auto useSelectedItem()
    {
        const size_t idx = size_t((im.selRow - 1) * 6 + im.selCol - 1);
        if (idx >= im.filtered.size())
            return;
        const gp::ItemDef &d = itemDefs[im.filtered[idx]];

        if (sellMode) // Jeremy's item screen: choosing an item offers to sell it
        {
            sellSelected(d);
            return;
        }

        if (giveMode) // the item screen opened from the pet in this home: choosing an item gives it, it isn't "used"
        {
            giveSelected(d);
            return;
        }

        // Item.hooks(id).onItemUse: a script that returns false takes the whole use over (no eating, no medicine,
        // nothing consumed unless the script takes it). The menu closes so it is rebuilt from the new inventory.
        if (scripts.fireBeforeItemUse(d.id).suppress || scripts.fireItemUse(d.id).suppress) // Game.hooks() (any item), then Item.hooks(id)
        {
            im.open = false;
            barBusy = 0;
            barAnim.visible = false;
            return;
        }

        // the engine system "item.use": what using the item does (eating, medicine, furnishing, map, potion...); a mod may cancel, replace or follow it
        const auto nativeUse = [&]
        {
        if (gp::isFurnishingType(d.type) || d.type == int(gp::ItemType::Food) || d.type == int(gp::ItemType::Medicine))
            sfx(0x59, 0xbe); // ItemMenuScreen: PlaySoundEffect(0x59, 0xbe) for these

        // Wall, floor, furniture, bed and decorations (types 1..8): asked about in the player's own home only, as in
        // ItemMenuScreen (IsCurrentSaveSlot); nowhere else do they do anything.
        if (gp::isFurnishingType(d.type))
        {
            useFurnishing(d);
            return;
        }
        // Food from the item screen inside a room: its own stat changes as usual, then the pet goes to the bowl, which is
        // filled, and eats (ItemMenuScreen result 2 -> EnterActivityAndRunLoop starts activity 6 at the bowl).
        if (room && d.type == int(gp::ItemType::Food) && d.w9 != gp::kFoodForMiniPets)
        {
            gp::applyFoodEffect(player, d, itemHost);
            std::printf("ate item %d (cat %d) at the bowl\n", d.index, d.w9);
            player.removeItem(d.id, 1);
            im.open = false;
            barBusy = 0;
            barAnim.visible = false;
            startEating(*room, false);
            return;
        }
        // Medicine inside a room: used up either way; a match cures the pet and plays the happy hop with the bubbles, a
        // wrong one does nothing (HandleCureIllnessEncounter returns at once).
        if (room && d.type == int(gp::ItemType::Medicine))
        {
            if (d.w9 == gp::kMedicineNoIllness)
                return;
            const int illnessBefore = player.illnessId;
            const bool cured = gp::applyMedicine(player, d);
            player.removeItem(d.id, 1);
            im.open = false;
            barBusy = 0;
            barAnim.visible = false;
            if (cured)
                startRoomMedicine(*room, illnessBefore);
            return;
        }
        // A treasure map (type 14): the item screen opens a plain text box with the item's text, id w9 (ItemMenuScreen,
        // the 0xe branch). It stays on the item screen.
        if (d.type == int(gp::ItemType::TreasureMap))
        {
            dialogue.show(gp::messageBox(db.textOr("base:text/" + std::to_string(d.w9))));
            return;
        }
        // The island map (type 17): the picture over the item screen, which is still there after it.
        if (d.type == int(gp::ItemType::IslandMap))
        {
            svc.showMap();
            return;
        }
        // A colour potion (type 16): the pet takes the colour for its species at once and, in the world, is startled. It is not
        // used up (see potion.hpp).
        if (d.type == int(gp::ItemType::Potion))
        {
            const int pal = gp::potionPalette(sp, d.w9);
            if (pal < 0)
                return;
            player.appearance = pal;
            petPal = pal;
            importPetAnims();
            saveGame();
            std::printf("potion %d: palette 0x%x\n", d.index, pal);
            if (room)
            {
                im.open = false;
                barBusy = 0;
                barAnim.visible = false;
            }
            else // the item screen fades out, then the world comes back with the pet's reaction
                requestScreen([&]
                              { im.open = false; barBusy = 0; barAnim.visible = false; startPotionReaction(); });
            return;
        }
        if (room) // the rest animate the world pet, which isn't shown in a room
        {
            std::printf("item %d type %d: using it inside a room isn't built yet\n", d.index, d.type);
            return;
        }

        if (d.type == int(gp::ItemType::Medicine))
        {
            if (d.w9 == gp::kMedicineNoIllness)
                return;
            const bool cured = gp::applyMedicine(player, d);
            player.removeItem(d.id, 1);
            requestScreen([&, cured] // the item screen fades out first
                          {
                im.open = false;
                barBusy = 0;
                const int illnessBefore = player.illnessId;
                barAnim.visible = false;
                if (cured)
                    startMedicine(illnessBefore);
                else
                {
                    facing = 1; // wrong medicine: the vanilla "walk in place" bug
                    pose = Pose::Move;
                    walkTicks = 0;
                    medStuck = true;
                    medStuckTicks = 0;
                    medStuckLimit = 90 + std::rand() % 91; // same 3-6 s range as the idle timer
                } });
            return;
        }

        // Type 3 (FoodDish) is a placed dish, not something you eat from the menu.
        if (d.type != int(gp::ItemType::Food))
        {
            std::printf("item %d type %d: use not ported\n", d.index, d.type);
            return;
        }
        if (d.w9 == gp::kFoodForMiniPets)
        {
            std::printf("item %d: 'for mini pets', no effect\n", d.index);
            return;
        }

        gp::applyFoodEffect(player, d, itemHost);
        std::printf("ate item %d (cat %d)\n", d.index, d.w9);

        player.removeItem(d.id, 1);

        // Menu result 2: fade out of the menu and play the eat animation.
        requestScreen([&]
                      { im.open = false; barBusy = 0; barAnim.visible = false; startFeed(); });
        };
        gp::SystemCtx ic;
        ic.set("item", d.id).set("type", d.type).set("room", room ? 1 : 0);
        scripts.system("item.use", ic, nativeUse);
    }

    auto buyFromShop(const gp::ShopEvent &pe)
    {
        const int item = pe.item, area = pe.area, slot = pe.slot;
        const gp::Id shopArea = cur->def->id;
        const gp::Id itemId = itemDefs[size_t(item)].id;
        const int si = shopStock.find(shopArea, itemId);
        if (si < 0)
            return;
        const gp::ItemDef &d = itemDefs[item];
        if (scripts.fireBeforeBuy(cur->def->id, d.id, d.price).suppress)
            return; // a mod called the sale off
        const std::string name = itemName(item);
        const gp::BuyQuote q = gp::quotePurchase((*shopStock.rowOf(shopArea))[size_t(si)].stock, money, d.price, ownedCount(item), d.maxQty);

        if (q.outcome == gp::BuyOutcome::AtCap)
        { // string at 0xb61d: "You can only carry %d %s."
            const std::string n = d.maxQty == 1       ? name
                                  : !d.plural.empty() ? db.textOr(d.plural, name)
                                                      : name + "s"; // suffix string at 0xb61b: verify
            dialogue.show(gp::messageBox("You can only carry " + std::to_string(d.maxQty) + " " + n + "."));
            return;
        }
        if (q.outcome == gp::BuyOutcome::CantAfford)
        { // string at 0xb669 (truncated in the decompile)
            dialogue.show(gp::messageBox(name + "\n" + std::to_string(d.price) + " Bucks each.\nYou can't afford that.")); // 0xb669
            return;
        }
        // Pick: the real game opens a quantity picker here (string at 0xb637). STOPGAP until dialogue.hpp has one:
        // a Yes/No box that buys exactly 1.
        gp::DialogueBox b = gp::messageBox(name + "\n" + std::to_string(d.price) + " Bucks each.\nHow many would you like to buy?"); // 0xb637
        b.kind = gp::BoxKind::Quantity;
        b.qty = 1;
        b.qtyMax = q.maxQty;
        b.onQuantity = [&, item, area, shopArea, itemId](int qty)
        {
            gp::deliverPurchase(shopStock, shopArea, itemId, qty, [&](const gp::Id &id)
                                { if (player.itemCount(id) < db.item(id)->maxQty) player.addItem(id, 1); }, // CheckQuestItemObjectivesComplete not ported
                                [&](const gp::Id &)
                                { pending.removeShopItem(area, item); });
            player.adjustMoney(-qty * itemDefs[item].price); // Item_ProcessPurchase: AdjustPlayerMoney(-qty * price)
            scripts.fireAfterBuy(cur->def->id, itemDefs[item].id, itemDefs[item].price, qty);
        };
        dialogue.show(std::move(b));
    }
};
