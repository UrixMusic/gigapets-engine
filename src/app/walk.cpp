// walk.cpp - walk around an area, take exits between areas, animated pet sprite, NPCs, dialogue box.
// Usage: walk <rom> <area> [--collision]     (--collision tints blocked ground red, objects blue)
//
// Areas, objects, exits and NPCs now come from the ContentDB (filled once by importRom).
// The ROM is only opened once, by importRom. Nothing else in the game reads it.
//
// --smooth: optional sub-pixel camera. The sim still ticks at 30 Hz on whole pixels; the renderer interpolates
// the pet / NPCs / camera between ticks and draws the world at float positions. F5 toggles it while playing.
// With it off, every float is a whole number and the output is identical to the old integer renderer.
//
// Pet keys: E = eat (29), B = bark (47), S = sit down (49) then sit idle (45), F = toggle flip formula,
//           P = dump the current animation's frames, 1-5 / J L I K = anchor experiments.
// Z = use an exit / talk to the nearest NPC (and close a dialogue box).
#include "../engine/tuning.hpp"
#include <SDL2/SDL.h>
#include "../loaders/import.hpp"
#include "../engine/collision.hpp"
#include "../engine/registry.hpp"
#include "../game/pet_colour_save.hpp"
#include "../engine/player_input.hpp"
#include "../engine/ground.hpp"
#include "../engine/area_runtime.hpp"
#include "../engine/render_content.hpp"
#include "../game/npc_actor.hpp"
#include "../game/player.hpp"
#include "../game/dialogue.hpp"
#include "../game/befriend.hpp"
#include "../game/encounter.hpp"
#include "../game/encounter_sequence.hpp"
#include "../game/text_draw.hpp"
#include "../game/shop.hpp" // with the other includes
#include "../engine/render.hpp"
#include "../engine/sprite_cache.hpp" // AnimRef, SpriteCache
#include "../game/menu_draw.hpp"
#include "../game/item_tabs.hpp"
#include "../game/story_blast.hpp"
#include "../game/menu_bar.hpp"
#include "../game/item_use.hpp" // at the top with the other includes
#include "../game/pending_event.hpp"
#include "../game/fixed_spawn.hpp"
#include "../loaders/fixed_spots.hpp"
#include "../game/quest_flow.hpp"
#include "../game/item.hpp"
#include "../game/progress_bits.hpp"
#include "../game/offerings.hpp"
#include "../game/resume_area.hpp"
#include "../game/save.hpp"
#include "../game/save_player.hpp"
#include "../game/profile.hpp"
#include "../game/debug_mode.hpp"
#include "../game/settings.hpp"
#include "../game/thought_bubble.hpp"
#include "../game/pet_voice.hpp"
#include "../game/trick_panel.hpp"
#include "../game/quest_nudge.hpp"
#include "../game/potion.hpp"
#include "../game/stat_gate.hpp"
#include "../game/color_machine.hpp"
#include "../game/world_map.hpp"
#include "../game/npc_stink.hpp"
#include "../game/sale.hpp"
#include "../engine/audio.hpp"
#include "../engine/display.hpp"
#include "../engine/keybinds.hpp"
#include "../game/stat_notify.hpp"
#include "../game/swift_pick.hpp"
#include "../mods/mod_loader.hpp"
#include "../mods/mod_list.hpp"
#include "../game/three_in_a_row.hpp"
#include "../game/memory_match.hpp"
#include "../game/trick_showcase.hpp"
#include "../game/pet_sit.hpp"
#include "../game/pet_wash.hpp"
#include "../game/selector_icon.hpp"
#include "../game/room_scene.hpp"
#include "../engine/room_render.hpp"
#include "../game/item_pools.hpp"
#include "../game/interactions.hpp"
#include "../mods/script_host.hpp"
#include "../mods/script_check.hpp" // hook target / hook name validation
#include "app_context.hpp"
#include "../game/minigame_ui.hpp" // AnimRef, readRomString, Minigames (swift pick, wells, tents)

#include <random>
#include <set>
#include <functional>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
//../game/walk/
#include "../game/walk/walk_state.hpp" // pet/world/flow/ui state of a run (needs the game headers above)
#include "../game/walk/walk_services.hpp"  // shared services for free functions
#include "../game/walk/walk_script_bindings.hpp" // ScriptHost <-> game
#include "../game/walk/walk_npc_talk.hpp" // talking to NPCs, friend greetings, onTalk seam
#include "../game/walk/walk_interactions.hpp" // Z on an object: actions, minigame starters, onInteract seam

constexpr int kProbeDY = -4;      // collision probe sits 4px above pos (game's coords)
constexpr int kCamY = 145;        // game camera: cam.y = pos.y - 145
constexpr int kStatsState = 0x0f; // PLACEHOLDER: press the first bar button, read "bar: SwitchGameState(0x..)", put that number here
constexpr int kBubbleFps = 6;

bool showStats = false;
bool smoothCam = false; // --smooth / F5: sub-pixel camera + interpolation between 30 Hz ticks (not reset per run)

gp::Player player;

class AreaWorld : public gp::IWorld
{
public:
    AreaWorld(const gp::GroundField &g, const gp::AreaRuntime &o, const gp::Room &room)
        : g_(g), o_(o), room_(room) {}
    bool tileWalkable(int x, int y) const override { return g_.walkable(x, y + gp::tuning().probeDY); }
    bool objectBlocks(int x, int y) const override { return o_.blocks(x, y + gp::tuning().probeDY); }
    bool inRoom(int x, int y) const override { return gp::inRoom(room_, x, y + gp::tuning().probeDY); }

private:
    const gp::GroundField &g_;
    const gp::AreaRuntime &o_;
    gp::Room room_;
};

struct WalkItemHost : gp::ItemUseHost
{
    std::function<void(int, int)> sink; // AddToStatCounter: set by runWalk() to the stat-change panel
    void addStatCounter(int counter, int amount) override
    {
        if (sink)
            sink(counter, amount);
    }
} itemHost;

static int clampCam(int raw, int world, int view)
{
    if (view > 320 && world < view)
        return (world - view) / 2; // a small area on a wide screen sits in the middle
    return std::min(std::max(raw, 0), world - view); // same order as the game
}

static float clampCamF(float raw, int world, int view)
{
    if (view > 320 && world < view)
        return float(world - view) / 2.f;
    return std::min(std::max(raw, 0.f), float(world - view));
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// once, at startup (after rom is loaded)
struct FrameRec
{
    int cols, rows, attr, base;
};
inline FrameRec readFrameRec(const gp::Rom &rom, int a)
{
    const uint32_t b = 0x91ab + uint32_t(a) * 4;
    return {rom.u16(b), rom.u16(b + 1), rom.u16(b + 2), rom.u16(b + 3)};
}

gp::Image makeScreenBg(const gp::Rom &rom, int a, int palId)
{
    const FrameRec f = readFrameRec(rom, a);
    gp::TileSheet sh;
    sh.tw = 8 << ((f.attr >> 4) & 3);
    sh.th = 8 << ((f.attr >> 6) & 3);
    sh.bpp = 2 * ((f.attr & 3) + 1);
    sh.tileWords = sh.tw * sh.th * sh.bpp / 16;
    sh.base = unsigned(f.base) * 0x40u;
    sh.ok = true;

    gp::Palette pal;
    const bool has = gp::readPalette(rom, palId, pal);
    gp::Image img(f.cols * sh.tw, f.rows * sh.th);
    for (int r = 0; r < f.rows; ++r)
        for (int c = 0; c < f.cols; ++c)
        {
            std::vector<uint8_t> px;
            if (!sh.decode(rom, r * f.cols + c + 1, px))
                continue;
            std::vector<uint8_t> cell(sh.tw * sh.th * 4);
            for (int i = 0; i < sh.tw * sh.th; ++i)
                gp::indexToRGBA(has ? &pal : nullptr, px[i], sh.bpp, false, &cell[i * 4]);
            gp::blit(img, cell.data(), sh.tw, sh.th, c * sh.tw, r * sh.th);
        }
    return img;
}

// Keep on the heap and never copy: members hold references into each other and into the ContentDB
// (the db must outlive every Loaded and must not be modified while one exists).
struct Loaded
{
    const gp::AreaDef *def = nullptr;
    std::unique_ptr<gp::GroundField> ground;
    std::unique_ptr<gp::AreaRuntime> runtime;
    gp::Image img;
    gp::Room room;
};

static std::unique_ptr<Loaded> loadArea(const gp::ContentDB &db, const gp::Id &id, bool showCollision)
{
    const gp::AreaDef *a = db.area(id);
    const gp::CollisionSet *cs = a ? db.collision(a->collision) : nullptr;
    if (!a || !cs)
        return nullptr;
    auto L = std::make_unique<Loaded>();
    L->def = a;
    L->ground = std::make_unique<gp::GroundField>(*a, *cs);
    L->runtime = std::make_unique<gp::AreaRuntime>(db, *a);
    L->room = {a->widthPx(), a->heightPx(), a->marginL, a->marginR, a->marginT, a->marginB};
    L->img = gp::renderBackground(db, *a);
    if (showCollision)
    {
        gp::overlayGroundCollision(L->img, *L->ground);
        gp::overlayObjectCollision(L->img, db, *a);
    }
    return L;
}

// What the pet is doing. Move/Wait show the walk animation (frame 0 while waiting);
// the rest play the animation selected by animId.
// Pose and AnimStep live in walk_state.hpp

// CreateNewPet, the stat part: care stats, starting money and personality.
// startStat = GetDefaultStatValue(0..3) = g_starterHaloStat[0..3]: the four numbers the starter-select screen showed.
// startSick = GetDefaultStatValue(4): the flag the starter-select pet carried (the front end stores it as startStat4).

// RollRandomIllnessId(species): RandRange(1, 0xb); 1..6 -> 4..9, 7..8 -> 3, 9..11 -> the species' own illness.
static int rollRandomIllnessId(const gp::SpeciesDef &sp)
{
    const int r = 1 + std::rand() % 11;
    if (r <= 6)
        return r + 3;
    if (r <= 8)
        return 3;
    return sp.ownIllness;
}

// CreateNewPet, the stat part. All the numbers now come from the SpeciesDef (see importSpecies in import.hpp).
static void applyNewPetDefaults(gp::Player &p, const int (&startStat)[4], int startSick, const gp::SpeciesDef &sp)
{
    p.adjustCare(gp::Health, sp.startHealth);
    p.adjustCare(gp::Hunger, sp.startHunger);
    p.adjustCare(gp::Happiness, sp.startHappiness);
    p.adjustCare(gp::Cleanliness, sp.startCleanliness);
    p.adjustCare(gp::Tiredness, sp.startTiredness);
    p.money = 0;
    p.adjustMoney(sp.startMoneyMin + std::rand() % std::max(1, sp.startMoneyMax - sp.startMoneyMin + 1));
    for (int i = 0; i < 4; ++i)
        p.adjustPersonality(gp::PersonalityStat(i), startStat[i]);
    if (startSick != 0) // GetDefaultStatValue(4) != 0 -> RollRandomIllnessId(species) -> SetActiveIllness
        p.illnessId = rollRandomIllnessId(sp);
}

// CreateNewPet's GrantItemOrEvent(item, count, 0) calls. The list lives in SpeciesDef::starterItems.
// Counts are clamped to the item's max quantity.
static void grantStarterItems(gp::Player &p, const std::vector<gp::ItemDef> &defs, const gp::SpeciesDef &sp)
{
    for (const gp::StarterItem &e : sp.starterItems)
    {
        const gp::ItemDef *d = nullptr;
        for (const gp::ItemDef &c : defs)
            if (c.id == e.item)
            {
                d = &c;
                break;
            }
        if (!d)
            continue;
        int &have = p.inventory[d->id];
        have = std::min(have + e.count, std::max(1, int(d->maxQty)));
    }
}
// Returns gp::kRunQuit (window closed), gp::kRunToMenu (F10) or gp::kRunError.
#include "../game/walk/walk_room.hpp"
#include "../game/walk/walk_world_actions.hpp"
#include "../game/walk/walk_draw.hpp"
#include "../game/walk/walk_items.hpp"
#include "../game/walk/walk_pet_activities.hpp" // the player homes and hotels (RoomController, RoomView): needs the types above
#include "../game/walk/walk_quest_bindings.hpp"

#include "../game/walk/walk_bar_gfx.hpp" // the menu bar host and the two Gfx adapters
#include "../game/walk/walk_frame_draw.hpp"
#include "../game/walk/walk_world_step.hpp"
#include "../game/walk/walk_encounter_host.hpp"
#include "../game/walk/walk_area_flow.hpp"
int runWalk(gp::AppContext &ctx, int slot, int startArea, bool showCollision)
{
    // globals from the standalone days: reset so a second run (after going back to the menu) starts clean
    showStats = false;
    player = gp::Player{};
    const std::string savePath = gp::slotPath(slot);
    gp::SaveData sd;
    const bool haveSave = gp::loadFromFile(savePath, sd);
    int playerSpecies = haveSave ? std::clamp(sd.getInt("species", 0), 0, 2) : 0; // a mod's species is found once the mods are loaded
    player.money = 500;        // stand-in until Player has money (near `inventory`)
    int &money = player.money; // replaces: int money = 500;
    bool statsOpen = false;

    uint16_t kingFlags = haveSave ? uint16_t(sd.getInt("kingFlags") & 0xfff) : 0;
    uint16_t gardenFlags = haveSave ? uint16_t(sd.getInt("gardenFlags") & 0xfff) : 0;
    std::function<void()> refreshBits, saveGame;
    uint32_t lastSaveTick = 0;
    int sp = playerSpecies;
    try
    {
        // ---- 1. LOAD ----
        WalkState S; // plain-data state of this run (walk_state.hpp); the locals below are aliases into it
        gp::Rom &rom = *ctx.rom; // owned by the AppHost now
        for (int i = 0x1a4f; i <= 0x1a57; ++i)
            std::printf("btn word 0x%x = 0x%x\n", i, rom.u16(i));
        gp::ContentDB db;
        gp::importRom(rom, db, playerSpecies); // line 290

        gp::ModReport rep = gp::importMods(db, "mods"); // <- new, after all base content
        gp::ScriptHost scripts;
        gp::EngineRegistry engineValues; // what engine.get / engine.set reach; filled in below, once the variables exist
        scripts.setRegistry(&engineValues);
        scripts.loadModSave(sd.kv); // what the mods kept in this slot (save.get)
        scripts.loadProfile(gp::loadProfileFile()); // and for the whole game (profile.get)
        if (haveSave && sd.has("mods")) // a save made with other mods: say so (it still loads)
            for (const std::string &c : gp::modListChanges(sd.kv["mods"], gp::modListString(rep.loaded, rep.versions)))
                std::fprintf(stderr, "mod warning: %s\n", c.c_str());
        for (const auto &w : rep.warnings)
            std::fprintf(stderr, "mod warning: %s\n", w.c_str());
        for (const auto &e : rep.errors)
            std::fprintf(stderr, "mod error: %s\n", e.c_str());
        for (const auto &id : rep.loaded)
            std::printf("loaded mod %s%s\n", id.c_str(), rep.trusted.count(id) ? "  (TRUSTED: its scripts run without the sandbox)" : "");
        if (!rep.ok())
            return 1;
        if (haveSave) // the saved species may be a mod's: it exists now
        {
            const int real = gp::loadSpecies(sd);
            if (real != playerSpecies)
            {
                playerSpecies = sp = real;
                std::fprintf(stderr, "species of this pet: %s (%d), %d npcs redrawn\n", gp::artOf(real).id.c_str(), real, gp::retargetPlayerSpeciesNpcs(rom, db, real));
            }
        }
        for (const gp::Id &bad : gp::resolveModPalettes(rom, db))
            std::fprintf(stderr, "mod palette %s: its ROM palette does not exist, so it is left out\n", bad.c_str());
        if (const int n = gp::applyNpcSpecies(rom, db))
            std::printf("%d npcs drawn with a species' art\n", n);
        if (const int n = gp::applyNpcPalettes(rom, db, playerSpecies))
        {
            std::printf("recoloured %d pet npcs\n", n);
        }
        // Scripts load now, and every hook must point at something that exists and use a real hook name, or it would silently never fire.
        for (const std::string &p : gp::checkModScripts(db, rep, scripts, "mods"))
            std::fprintf(stderr, "script error: %s\n", p.c_str());
        if (!rep.ok())
            return 1;
        // The pet's species and palette come from the db now: the colour picked in the front end, else the species default.
        const gp::SpeciesDef *speciesDef = db.speciesByLegacy(playerSpecies);
        if (!speciesDef)
        {
            std::printf("species %d not found in the content db\n", playerSpecies);
            return gp::kRunError;
        }
        const auto slotInfo = gp::peekSlot(slot);
        // a mod's colour comes back from its palette id (the number it had last session means nothing now)
        gp::loadColourIds(sd, [&](const gp::Id &id)
                          { const gp::PaletteDef *p = db.palette(id); return p && gp::modPalettes().count(p->number) ? p->number : -1; });
        const int savedAppearance = sd.getIntStrict("appearance", sd.getIntStrict("colour", -1));
        int petPal = (slotInfo.full && savedAppearance >= 0) ? savedAppearance : speciesDef->defaultPalette; // washing can change it
        std::printf("pet palette 0x%x (%s)\n", petPal, (slotInfo.full && savedAppearance >= 0) ? "from save" : "species default");
        // find record tables by content: sprite, pal, x, y

        // Items come from the db now (base game first, then mods): itemDefs[i].index == i. A reference, not a copy:
        // the db must not gain items while the game runs.
        const std::vector<gp::ItemDef> &itemDefs = db.items();
        // player.inventory (item id -> count) is the only inventory now.
        for (const gp::ItemDef &d : itemDefs)
            if (d.type == int(gp::ItemType::Food) || d.type == int(gp::ItemType::Medicine) || d.type == int(gp::ItemType::FoodDish))
                player.inventory[d.id] = 1 + d.index % 4;
        for (int ill = 0; ill < 10; ++ill)
        {
            const gp::Id cure = gp::illnessCureItem(itemDefs, ill);
            if (cure != gp::legacyIllnessCure(ill))
                std::printf("cure table mismatch: illness %d -> %s (table says %s)\n", ill, cure.c_str(),
                            gp::legacyIllnessCure(ill).c_str());
        }
        if (haveSave)
        {
            player.inventory.clear();

            auto convert = [&](const char *key, const char *setId)
            {
                const gp::OfferSetDef *s = db.offerSet(setId);
                if (!s || !sd.kv.count(key))
                    return;
                const unsigned bits = unsigned(sd.getInt(key)) & 0xfff;
                for (int i = 0; i < 12 && i < int(s->items.size()); ++i)
                    if (bits >> i & 1)
                        player.setFlag(gp::offeredKey(s->items[size_t(i)])); // OR-merge: never clears
            };
            convert("kingFlags", "base:offers/king");
            convert("gardenFlags", "base:offers/gardenia");
            if (sd.has("itemIds") && !sd.getStr("itemIds").empty()) // new format
            {
                for (const auto &[id, n] : sd.getIdMap("itemIds"))
                    if (n > 0) // ids the db doesn't know (an uninstalled mod) are kept, so they survive the next save
                        player.inventory[id] = n;
            }
            else // old saves: item numbers
            {
                for (const auto &[it, n] : sd.getMap("inventory"))
                    if (it >= 0 && it < int(itemDefs.size()) && n > 0)
                        player.inventory[gp::legacyItemId(it)] = n;
            }
        }
        // The shops' stock: rolled from the shop records (the ROM's weight tables, and whatever the mods add) as the ROM does at start, and topped up on a timer.
        gp::ShopStock shopStock;
        int shopRestockAccum = 0; // ROM ticks since the shops were last topped up
        auto shopRnd = [](int n)
        { return std::rand() % n; };
        shopStock.init(db.shops(), shopRnd);
        for (const gp::ShopDef &sd : db.shops())
        {
            std::printf("shop %s:", sd.id.c_str());
            if (const auto *row = shopStock.rowOf(sd.area))
                for (const gp::StockEntry &e : *row)
                    if (e.present())
                        if (const gp::ItemDef *id = db.item(e.item))
                            std::printf(" %dx%s", e.stock, db.textOr(id->name, "?").c_str());
            std::printf("\n");
        }
        auto itemNumberOf = [&](const gp::Id &item) -> int
        { const gp::ItemDef *d = db.item(item); return d ? d->index : -1; };
        gp::PendingList pending;
        auto enterShopStock = [&](const gp::AreaDef &a) { // call when the destination is a shop
            const gp::ShopDef *shop = db.shopForArea(a.id);
            if (!shop)
                return;
            // ClampInventorySlotQuantities: nothing is offered that the pet cannot carry or that is over the shop's cap
            shopStock.clamp(*shop, [&](const gp::Id &i) { return player.itemCount(i); },
                            [&](const gp::Id &i) { const gp::ItemDef *d = db.item(i); return d ? d->maxQty : 0; });
            gp::spawnShopItemStock(pending, shopStock, a.legacyId, a.id, a.spawnPoints, shopRnd, itemNumberOf);
        };

        // The stock rules (what the ROM's ClaimLRUCacheSlot calls do, as records): something happened that may put an item into a shop.
        auto applyStock = [&](const gp::StockEvent &ev)
        {
            gp::applyStockRules(db.stockRules(), shopStock, ev,
                                [&](const gp::Id &shop) -> gp::Id { const gp::ShopDef *s = db.shop(shop); return s ? s->area : gp::Id(); },
                                [&](const gp::Id &i) { return player.itemCount(i) > 0; },
                                [&](const gp::Id &i) { return player.hasFlag(gp::offeredKey(i)); });
        };

        auto petFellIll = [&] // SetActiveIllness: the cure goes to the pharmacy
        {
            if (player.illnessId < 0 || player.illnessId > 9)
                return;
            gp::StockEvent ev;
            ev.kind = gp::StockEvent::Illness;
            ev.cure = gp::illnessCureItem(itemDefs, player.illnessId);
            applyStock(ev);
        };

        int itemSlot = 0x1e; // GUESS: tune with [ and ] while the menu is open
        constexpr int kMaxSlotProbe = 16;

        {
            // Ground sprites. Mod items bring their own art (already in the db), so only ROM sprite numbers are imported here.
            std::set<int> done;
            for (const gp::ItemDef &d : itemDefs)
                if (d.worldSprite != 0xffff && !gp::isModSprite(d.worldSprite) && done.insert(d.worldSprite).second)
                    try
                    {
                        gp::importUiSprite(db, rom, d.worldSprite, gp::kMenuPalette);
                    }
                    catch (const std::exception &)
                    {
                    }
        }

        struct CollectGfx : gp::Gfx
        {

            std::set<std::pair<int, int>> pairs;
            void drawTile(int s, int p, int, int, int, int) override { pairs.insert({s, p}); }
            void drawTextBox(int, int, int, int, const std::string &, int, int, int) override {}
        };
        {
            CollectGfx col;
            col.pairs.insert({0x1e, 0x5dc});
            for (int tab = 0; tab < 5; ++tab)
                for (auto sub : {gp::MenuSub::Grid, gp::MenuSub::ToTabs, gp::MenuSub::Tabs, gp::MenuSub::ToGrid})
                    for (bool pr : {false, true})
                        gp::drawTabMarker(col, rom, tab, pr, sub, 0);
            for (int s = 0; s < kMaxSlotProbe; ++s)
                gp::drawSlotScene(col, rom, s);
            gp::drawSlotScene(col, rom, 0x1e);
            for (int s : {0x400, 0x40a, 0x3f4, 0x3f2, 0xacb, 0x478}) // 0x478: Three in a Row's "not selected" reel frame
                col.pairs.insert({s, gp::kMenuPalette});
            for (int i = 0; i < 20; ++i)
                std::printf("%04x ", rom.u16(gp::kSlotTable + i));
            gp::StatsView dummy;
            dummy.healthy = false;
            dummy.questText = "x";
            for (const gp::SpeciesDef *spd : db.allSpecies())
            {
                if (spd->legacyIndex < 0) // mod species have no ROM-side pet-stats art
                    continue;
                dummy.species = spd->legacyIndex;
                dummy.petPalette = spd->defaultPalette;
                gp::drawPetStats(col, rom, dummy, "");
            }
            dummy.species = sp; // and this pet's own colour
            dummy.petPalette = petPal;
            gp::drawPetStats(col, rom, dummy, "");
            for (int s = 0; s < 0x80; ++s)
            {
                const int b = gp::kSlotTable + s * 9;
                std::printf("slot 0x%x: pal 0x%x bgSprite 0x%x\n", s, rom.u16(b), rom.u16(b + 1));
            }
            // Which slot draws records on the 6x3 grid positions?
            for (int s = 0; s < 0x80; ++s)
            {
                const int b = gp::kSlotTable + s * 9;
                const int recs = rom.u16(b + 2), list = rom.u16(b + 3), n = rom.u16(b + 4);
                if (n < 1 || n > 80)
                    continue;
                int hits = 0;
                std::string sprites;
                for (int i = 0; i < n; ++i)
                {
                    const int idx = rom.u16(list + i);
                    if (idx == 0xffff)
                        continue;
                    const int r = recs + idx * 4;
                    const int x = rom.u16(r + 2), y = rom.u16(r + 3);
                    bool on = false;
                    for (int c = 0; c < 6 && !on; ++c)
                        for (int rw = 0; rw < 3 && !on; ++rw)
                            on = std::abs(x - (0x0f + 0x32 * c)) <= 8 && std::abs(y - (0x2f + 0x32 * rw)) <= 8;
                    if (on)
                    {
                        ++hits;
                        char t[16];
                        std::snprintf(t, sizeof t, " %x", rom.u16(r));
                        sprites += t;
                    }
                }
                if (hits >= 6)
                    std::printf("GRID-LIKE slot 0x%x: %d records on cell positions, sprites:%s\n", s, hits, sprites.c_str());
            }
            for (const gp::ItemDef &d : itemDefs)
                if ((d.index < 0x5d || d.index > 0x5f) && !gp::isModSprite(d.iconSprite)) // starter-palette items skipped for now
                    col.pairs.insert({d.iconSprite, gp::kMenuPalette});
            for (auto [s, p] : col.pairs)
                gp::importUiSprite(db, rom, s, p);
        }

        for (const gp::ShopDef &sd : db.shops())
            for (const gp::ShopItem &si : sd.items)
                if (const gp::ItemDef *d = db.item(si.item))
                    if (!gp::isModSprite(d->worldSprite))
                        try
                        {
                            gp::importUiSprite(db, rom, d->worldSprite, gp::kMenuPalette);
                        }
                        catch (const std::exception &)
                        {
                        }

        const gp::TrickLayout trickLay = gp::TrickLayout::from(db.minigameOr("base:minigame/trick_panel")); // the tricks panel's numbers (data: a mod's minigame patch changes them)
        for (int a : {0xe, 0x10, 0x11, 0x12, 0x16, 0x17, 0x18, gp::kTabAnimIn, gp::kTabAnimOut, gp::kBlastAnim, gp::kStinkAnim, gp::kExclaimIntroAnim, gp::kExclaimOutroAnim, trickLay.questionBubble, trickLay.answerBubble, gp::kHaloBubble, gp::kPitchforkBubble, gp::kStarBubble}) // 0xe: the wash bubble; arrow, dig bubble; 0x17/0x18: the thought bubble
            try
            {
                gp::importAnim(rom, db, a, 0x28c);
            }
            catch (const std::exception &)
            {
            }
        for (int s = gp::kTrickIconFirst; s <= gp::kTrickIconLast; ++s) // the tricks panel's icons and placeholders
            try
            {
                gp::importUiSprite(db, rom, s, 0x28c);
            }
            catch (const std::exception &)
            {
            }
        for (int s : {gp::kMapMarkerTile, 0x9c1, 0x9c2, gp::kThoughtHungerIcon, gp::kThoughtTiredIcon, gp::kThoughtSickIcon, gp::kThoughtMoodIcon}) // flower, dug hole, the thought bubble icons
            try
            {
                gp::importUiSprite(db, rom, s, 0x28c);
            }
            catch (const std::exception &e)
            {
                std::printf("flower/hole sprite 0x%x import failed: %s\n", s, e.what());
            }
        for (const gp::StatNoticeSlot &sl : gp::kStatNoticeSlots) // the stat-change panel's icons and backdrop
            try
            {
                gp::importUiSprite(db, rom, sl.tile, sl.palette);
                gp::importUiSprite(db, rom, gp::kStatNoticeBg, 0x28c);
            }
            catch (const std::exception &)
            {
            }
        try
        {
            gp::importUiSprite(db, rom, gp::kMapTile, gp::kMapPalette); // the island map, full screen
        }
        catch (const std::exception &e)
        {
            std::printf("map sprite import failed: %s\n", e.what());
        }
        for (int s = 0x994; s <= 0x9ab; ++s) // DrawShadow blobs: 12 normal + 12 for species 2
            try
            {
                gp::importUiSprite(db, rom, s, 0x28c);
            }
            catch (const std::exception &e)
            {
                std::printf("shadow sprite 0x%x import failed: %s\n", s, e.what());
            }

        // Memory Match: card back / face / cursor are palette 0x28c (DrawTile(0x471, 0x28c, ...)); item icons use the
        // menu palette and are already imported by the iconSprite loop.
        for (int s : {gp::MemoryMatch::kSpriteBack, gp::MemoryMatch::kSpriteFace, gp::MemoryMatch::kSpriteCursor})
            try
            {
                gp::importUiSprite(db, rom, s, 0x28c);
            }
            catch (const std::exception &e)
            {
                std::printf("memory match sprite 0x%x import failed: %s\n", s, e.what());
            }
        try
        {
            gp::importAnim(rom, db, gp::MemoryMatch::kFlipAnim, gp::MemoryMatch::kFlipAnimPal);
        }
        catch (const std::exception &)
        {
        }

        gp::importRoomTilesets(rom, db); // the wall and floor tilesets of the home and hotel rooms
        {
            // The room's own pieces (door, hooks, ...) and the sprites of every furnishing item: sprite ids, not anim ids.
            auto importDecor = [&](int sprite, int palette)
            {
                if (sprite <= 0 || sprite == gp::kNoSprite || gp::isModSprite(sprite)) // (a mod item's picture is already registered)
                    return;
                try
                {
                    gp::importUiSprite(db, rom, sprite, palette);
                }
                catch (const std::exception &e)
                {
                    std::printf("room sprite 0x%x import failed: %s\n", sprite, e.what());
                }
            };
            for (int loc : {0, 4})
                for (const gp::RoomDecoration &d : gp::roomDecorations(rom, loc))
                    importDecor(d.sprite, d.palette);
            for (int loc = 0; loc <= 7; ++loc) // what a mod's `room` patch put in the tables
                if (db.room(loc))
                    for (const gp::RoomDecoration &d : gp::roomDecorations(rom, loc, db.room(loc)))
                        importDecor(d.sprite, d.palette);
            for (int marker : {0x33d, 0x3b0, 0x319}) // the "put it here" markers while placing a furnishing
                importDecor(marker, 0x28c);
            for (int bowl : {0x95d, 0x95e, 0x963, 0x961}) // the food in the bowl, full to empty
                importDecor(bowl, 0x28c);
            for (const gp::ItemDef &it : itemDefs)
                if (it.type >= 3 && it.type <= 8)
                {
                    importDecor(it.decorSprite, it.w7);
                    importDecor(it.w9, it.w7);
                }
        }

        // Trick Teacher game: slot frame and cursor (0x478, 0x400) are already in; add the check and the cross.
        for (int s : {gp::TrickGuess::kSpriteSlot, gp::TrickGuess::kSpriteCursor, gp::TrickGuess::kSpriteRight, gp::TrickGuess::kSpriteWrong})
            try
            {
                gp::importUiSprite(db, rom, s, gp::TrickGuess::kSpritePal);
            }
            catch (const std::exception &e)
            {
                std::printf("trick guess sprite 0x%x import failed: %s\n", s, e.what());
            }

        // Bar cursor sprites: 8 words per button (normal rec, pressed rec), 4 words each: sprite, pal, x, y
        for (bool house : {false, true})
        {
            const int base = house ? gp::kBarCursorHouse : gp::kBarCursorOutside;
            const int buttons = house ? 8 : 7;
            for (int b = 0; b < buttons; ++b)
                for (int rec = 0; rec < 2; ++rec)
                {
                    const int w = base + b * 8 + rec * 4;
                    int pal = rom.u16(w + 1);
                    if (pal == 0xffff)
                        pal = gp::kMenuPalette;
                    gp::importUiSprite(db, rom, rom.u16(w), pal);
                }
        }

        {
            const int b = gp::kSlotTable + 0x1e * 9;
            const int defPal = rom.u16(b), recs = rom.u16(b + 2), list = rom.u16(b + 3), n = rom.u16(b + 4);
            std::printf("slot 0x1e: defPal 0x%x bg 0x%x shift 0x%x count %d\n", defPal, rom.u16(b + 1), rom.u16(b + 5), n);
            for (int i = n - 1; i >= 0; --i)
            {
                const int idx = rom.u16(list + i);
                if (idx == 0xffff)
                    continue;
                const int r = recs + idx * 4;
                std::printf("  rec %d: sprite 0x%x pal 0x%x at (%d,%d) layer 0x%x\n",
                            idx, rom.u16(r), rom.u16(r + 1), rom.u16(r + 2), rom.u16(r + 3), gp::slotLayer(idx));
            }
        }
        player.questsCompleted = 1; // g_totalQuestsCompleted starts at 1
        if (haveSave)
            gp::loadPlayer(sd, player);
        const gp::Settings settings = gp::loadSettings(); // the options screen's choices; tutorials decide the first-time messages
        {
            // A pet that was created but never played has no stats yet: run CreateNewPet's stat part once.
            // "statsInit" is written by every later save; the all-zero check keeps older test saves untouched.
            auto allZero = [&]
            {
                for (auto c : {gp::Hunger, gp::Health, gp::Unknown2, gp::Happiness, gp::Tiredness, gp::Cleanliness})
                    if (player.care[c])
                        return false;
                for (auto s : {gp::Halos, gp::Pitchforks, gp::Stars, gp::Lightbulbs})
                    if (player.personality[s])
                        return false;
                return true;
            };
            if (!haveSave || (sd.getInt("statsInit", 0) == 0 && allZero()))
            {
                int startStat[4];
                for (int j = 0; j < 4; ++j)
                    startStat[j] = sd.getInt("startStat" + std::to_string(j), 0); // written by the front end
                const int startSick = sd.getInt("startStat4", 0);                 // written by the front end: the selector pet was sick
                applyNewPetDefaults(player, startStat, startSick, *speciesDef);
                grantStarterItems(player, itemDefs, *speciesDef);
                std::printf("new pet: defaults applied (money %d, illness %d, personality %d/%d/%d/%d)\n", player.money, player.illnessId,
                            player.personality[gp::Halos], player.personality[gp::Pitchforks],
                            player.personality[gp::Stars], player.personality[gp::Lightbulbs]);
            }
        }
        const int id = startArea;
        // A save made inside a mod area resumes there, at the same spot (the normal start only: --area N still wins).
        gp::ResumeSpot resume;
        const gp::AreaDef *startDef = nullptr;
        if (startArea == 0 && haveSave && gp::loadResume(sd, resume))
        {
            startDef = db.area(resume.area);
            if (!startDef)
                std::printf("saved in '%s', which is not loaded (its mod is gone?): starting at the usual place\n", resume.area.c_str());
            else if (!startDef->resume)
            {
                std::printf("saved in '%s', which no longer asks to be resumed: starting at the usual place\n", resume.area.c_str());
                startDef = nullptr;
            }
        }
        const bool resuming = startDef != nullptr;
        if (!startDef)
            startDef = db.areaByLegacy(id);
        if (!startDef)
        {
            std::printf("area %d not found\n", id);
            return gp::kRunError;
        }
        std::unique_ptr<Loaded> cur = loadArea(db, startDef->id, showCollision);
        if (!cur)
        {
            std::printf("area %d is invalid (no tileset/collision)\n", id);
            return gp::kRunError;
        }
        enterShopStock(*cur->def);
        std::unique_ptr<AreaWorld> world = std::make_unique<AreaWorld>(*cur->ground, *cur->runtime, cur->room);

        // ---- start position: the free spot closest to the middle of the room ----
        // ---- start position ----
        // Fresh load: the original spawns the pet at a fixed spot per save slot
        // (RunPetRoomGameplayLoop, slot = house). Otherwise: free spot closest to the middle of the room.
        static const int kSlotSpawn[3][2] = {{0x826, 0x38d}, {0x7c7, 0x4cf}, {0x6cc, 0x40d}};

        gp::Point &pos = S.world.pos;
        pos = {cur->room.width / 2, cur->room.height / 2};
        long best = -1;
        bool slotSpawned = false;
        if (resuming)
        {
            if (world->tileWalkable(resume.x, resume.y) && world->inRoom(resume.x, resume.y))
            {
                pos = {resume.x, resume.y};
                slotSpawned = true; // (skips the search for the middle of the room)
            }
            else
                std::printf("warning: the saved spot (%d,%d) in %s is not walkable any more, using the centre search\n", resume.x, resume.y, resume.area.c_str());
        }
        else if (slot >= 0 && slot < 3)
        {
            const int sx = kSlotSpawn[slot][0], sy = kSlotSpawn[slot][1];
            if (world->tileWalkable(sx, sy) && world->inRoom(sx, sy))
            {
                pos = {sx, sy};
                slotSpawned = true;
            }
            else
                std::printf("warning: slot %d spawn (0x%x,0x%x) is not walkable here, using the centre search\n",
                            slot, sx, sy);
        }
        if (!slotSpawned)
        {
            for (int y = 0; y < cur->room.height; y += 4)
                for (int x = 0; x < cur->room.width; x += 4)
                {
                    if (!world->tileWalkable(x, y) || !world->inRoom(x, y))
                        continue;
                    const long dx = x - cur->room.width / 2, dy = y - cur->room.height / 2;
                    const long d = dx * dx + dy * dy;
                    if (best < 0 || d < best)
                    {
                        best = d;
                        pos = {x, y};
                    }
                }
            if (best < 0)
                std::printf("warning: no walkable spot found, starting at the center\n");
        }

        gp::Point &prevPos = S.world.prevPos; // where the pet was before the latest tick (smooth camera interpolates from here)
        prevPos = pos;

        // ---- 2. WINDOW ----
        const int W = gp::logicalW(), H = 240; // 320, or 426 in 16:9 (the world fills it; the menus sit in the middle)
        unsigned statsBase = 0xD8DC;
        SDL_Renderer *ren = ctx.ren;                     // window + renderer belong to the AppHost
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest (SDL's default): keeps pixel art crisp at fractional positions
        SDL_RenderSetLogicalSize(ren, W, H);
        SDL_SetWindowTitle(ctx.win, "walk");
        SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cur->img.w, cur->img.h);
        SDL_UpdateTexture(tex, nullptr, cur->img.rgba.data(), cur->img.w * 4);

        auto makeBgTex = [&](int a, int pal)
        {
            gp::Image im = makeScreenBg(rom, a, pal);
            SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, im.w, im.h);
            SDL_UpdateTexture(t, nullptr, im.rgba.data(), im.w * 4);
            return t;
        };
        SDL_Texture *bgTex = makeBgTex(0x1e, 0x5dc);      // item menu
        SDL_Texture *statsBgTex = makeBgTex(0x20, 0x5de); // pet stats
        SDL_Texture *swiftBgTex = makeBgTex(0, 0x5c3);    // Swift Pick (same as the intro)

        // ---- 3. ANIMATIONS (pet and NPCs): coloured frames come from the db, textures are built on first use ----
        SpriteCache sprites(ren, db); // anim and object textures, built on first use and freed when this scope ends
        auto animTextures = [&](const gp::Id &animId)
        { return sprites.animTextures(animId); };
        // petPal is defined at the top of runWalk (saved colour, else the species default)

        std::vector<int> petIds;
        for (int id : gp::artOf(sp).all()) // every role of this species' art
            petIds.push_back(id);
        // (the reactions, tricks, the chain of the panda's level 3 trick and the dig are in all() above)
        std::sort(petIds.begin(), petIds.end());
        petIds.erase(std::unique(petIds.begin(), petIds.end()), petIds.end());
        auto importPetAnims = [&] // under the current petPal; again after a wash changes the colour
        {
            for (int id : petIds)
            {
                try
                {
                    gp::importAnim(rom, db, id, petPal);
                }
                catch (const std::exception &e)
                {
                    // std::printf("pet anim %d: %s\n", id, e.what());
                }
            }
        };
        importPetAnims();

        auto petAnimId = [&](int n) -> gp::Id
        {
            if (gp::isModAnim(n)) // a mod's own frames; drawn with a game palette they are recoloured with the pet's
            {
                const gp::Id key = gp::importAnim(rom, db, n, petPal);
                return key.empty() ? gp::modAnimName(n) : key;
            }
            gp::Id withPal = gp::animKey(n, petPal);
            return db.anim(withPal) ? withPal : gp::Id("base:anim/" + std::to_string(n));
        };
        auto getAnim = [&](int n) -> AnimRef
        {
            const gp::Id id = petAnimId(n);
            const gp::Anim *a = db.anim(id);
            if (!a)
            {
                std::printf("anim %d: not in the content db (%s)\n", n, id.c_str());
                return {};
            }
            return {a, animTextures(id)};
        };
        const int kWalkAnim[3] = {gp::artOf(sp).walkUp, gp::artOf(sp).walkDown, gp::artOf(sp).walkSide}; // 0 = up, 1 = down, 2 = sideways

        std::unique_ptr<WorldDraw> worldDrawCtl; // built below, once everything it takes is declared (walk_draw.hpp)
        auto drawObject = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawObject(std::forward<decltype(a)>(a)...); };

        // ---- NPCs: records come from the db ----
        std::vector<gp::NpcActor> &npcs = S.world.npcs;
        gp::Rng &npcRng = S.world.npcRng;
        npcRng.state = SDL_GetTicks() | 1u; // must be non-zero
        // ---- smooth camera: positions between the last two 30 Hz ticks ----
        std::vector<std::pair<int, int>> &npcPrev = S.world.npcPrev; // each NPC's x,y before the latest tick (same order as npcs)
        float &renderAlpha = S.world.renderAlpha;                  // how far through the current tick this frame is (0..1), set every frame
        auto smoothAt = [&](int prev, int now) -> float
        {
            if (!smoothCam || std::abs(now - prev) > 16) // a jump (door, snap, respawn) is never glided over
                return float(now);
            return lerpf(float(prev), float(now), renderAlpha);
        };
        auto npcPosF = [&](const gp::NpcActor &n) -> std::pair<float, float>
        {
            const size_t i = size_t(&n - npcs.data());
            if (npcPrev.size() == npcs.size() && i < npcPrev.size())
                return {smoothAt(npcPrev[i].first, n.x()), smoothAt(npcPrev[i].second, n.y())};
            return {float(n.x()), float(n.y())};
        };
        ScriptNpcState scriptNpc;
        constexpr size_t kMaxNpcs = 64;
        auto loadNpcs = [&]()
        {
            npcs.clear();
            npcPrev.clear();
            if (scriptNpc.area != cur->def->id) // a new area: what scripts did in the last one is forgotten
            {
                scriptNpc.area = cur->def->id;
                scriptNpc.removed.clear();
                scriptNpc.moved.clear();
                scriptNpc.spawned.clear();
            }
            scriptNpc.defs.clear();
            scriptNpc.pending.clear(); // (the rebuild below already has them)
            npcs.reserve(kMaxNpcs);    // spawning later must not move the actors: other code holds pointers to them
            const int area = cur->def->legacyId;
            auto withPos = [&](const gp::NpcDef &d, gp::IPoint p) -> const gp::NpcDef &
            {
                gp::NpcDef c = d;
                c.x = p.x;
                c.y = p.y;
                return scriptNpc.defs.emplace_back(std::move(c));
            };
            for (const gp::NpcDef *d : db.npcsInArea(area))
            {
                if (!d->availableAt(player.questsCompleted)) // the base game's area 3 pets wait for quest 17; a mod's npc can set quest_min / quest_max
                    continue;
                if (scriptNpc.removed.count(d->id))
                    continue;
                const auto m = scriptNpc.moved.find(d->id);
                npcs.emplace_back(m != scriptNpc.moved.end() ? withPos(*d, m->second) : *d);
            }
            for (const auto &[id, p] : scriptNpc.spawned)
                if (const gp::NpcDef *d = db.npc(id))
                {
                    gp::NpcDef c = *d;
                    c.area = area;
                    c.x = p.x;
                    c.y = p.y;
                    npcs.emplace_back(scriptNpc.defs.emplace_back(std::move(c)));
                }
            std::printf("%zu NPCs in area %d\n", npcs.size(), area);
        };
        loadNpcs();
        auto animRefById = [&](const gp::Id &id) -> AnimRef
        { return sprites.animRef(id); };
        // The three special stuffed animals (items 0x5d..0x5f) are drawn in the colour the pet started with (g_starterExtraField)
        // wherever their record says 0x28c: icon, world and furnishing sprites. Imports that colour the first time it is needed.
        auto uiIdP = [&](int sprite, int pal) -> gp::Id
        {
            if (pal == gp::kMenuPalette && !gp::isModSprite(sprite))
                for (int i = 0x5d; i <= 0x5f; ++i)
                    if (sprite == itemDefs[size_t(i)].iconSprite || sprite == itemDefs[size_t(i)].worldSprite || sprite == itemDefs[size_t(i)].decorSprite)
                    {
                        pal = player.baseColour;
                        const gp::Id id = gp::uiSpriteId(sprite, pal);
                        if (!db.anim(id))
                            try
                            {
                                gp::importUiSprite(db, rom, sprite, pal);
                            }
                            catch (const std::exception &)
                            {
                            }
                        return id;
                    }
            return gp::uiSpriteId(sprite, pal);
        };

        auto reactAnimId = [](int id, int pal)
        {
            return gp::Id(gp::animKey(id, pal));
        };

        std::unique_ptr<AreaFlow> areaFlowCtl; // built below, once everything it takes is declared (walk_area_flow.hpp)
        auto npcAnim = [&](const gp::NpcActor &n, int key) -> decltype(auto) { return (*areaFlowCtl).npcAnim(n, key); };
        auto updateNpcAnim = [&](gp::NpcActor &n) -> decltype(auto) { return (*areaFlowCtl).updateNpcAnim(n); };
        int stinkTicks = 0; // the stink cloud's animation clock (the pet's and the NPCs')
        gp::StatNotice statNotice; // the panel that drops from the top right when money or a stat changes
        player.pendingMoney = 0;   // what was set up before the world started (starting money, a load) is not news
        itemHost.sink = [&](int counter, int amount)
        { statNotice.add(counter, amount); };
        int holdTicks = 0;                // a quest reward activity is holding the pet (sim steps left), 0 = none
        gp::NpcActor *holdNpc = nullptr;  // the NPC held with it (SetAnimalEncounterLockFlag), released when the hold ends
        std::function<void()> holdDone;   // what runs when the hold ends
        gp::StinkTransfer stinkXfer; // how long the pet has stood beside a stinky NPC / been stinky beside a clean one
        auto drawNpc = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawNpc(std::forward<decltype(a)>(a)...); };

        for (int a = 0; a < 4; ++a)
            try
            {
                gp::importAnim(rom, db, a, 0x28c);
            }
            catch (const std::exception &e)
            {
                const gp::Anim *an = db.anim("base:anim/" + std::to_string(a) + "@28c");
                std::printf("bar anim %d: %zu frames\n", a, an ? an->frames.size() : 0);
            }

        auto barAnimRef = [&](int n) -> AnimRef
        {
            char b[48];
            std::snprintf(b, sizeof b, "base:anim/%d@%x", n, 0x28c);
            AnimRef A = animRefById(b);
            return A.size() ? A : animRefById("base:anim/" + std::to_string(n));
        };
        for (int base : {0xf5e8, 0xf628})
        {
            std::printf("BAR %04x:", base);
            for (int i = 0; i < 16; ++i)
                std::printf(" %04x", rom.u16(base + i));
            std::printf("\n");
        }

        for (int a = 137; a <= 149; ++a)
            try
            {
                gp::importAnim(rom, db, a, 0x28c);
            }
            catch (const std::exception &)
            {
            }

        // ---- dialogue: one box at a time; while it is open the world is frozen ----
        gp::Dialogue dialogue;
        gp::QuestRuntime quests;
        uint32_t &gameTicks = S.world.gameTicks; // the game's tick counter: +2 per 30 Hz tick
        gp::QuestNudge nudge;           // "Go see Mayor Peter!" (quest_nudge.hpp)
        nudge.restart(long(gameTicks));
        TextDrawer signText(ren, db.font(gp::fontId(1, 0x11f))); // sign text: font 1, palette 0x11f
        TextDrawer text(ren, db.font(gp::fontId(0)));            // dialogue text: font 0
        TextDrawer numbers(ren, db.font(gp::fontId(2)));         // stat numbers: font 2
        TextDrawer badge(ren, db.font(gp::fontId(7)));
        std::unique_ptr<gp::EncounterSequence> &enc = S.world.enc;
        gp::NpcActor *&encNpc = S.world.encNpc;
        // The Trick Teacher shows the trick off before offering the lesson (trick_showcase.hpp). The world keeps running;
        // the pet is held and talking is off until it is over.
        gp::TrickShowcase showcase;
        gp::NpcActor *showNpc = nullptr;
        bool showStarted = false; // false while the teacher is still settling into place
        int showLevel = 1;
        std::function<void()> showDone;
        // (RoomView, the view of a player home or hotel, is in game/walk/walk_room.hpp)
        std::unique_ptr<RoomView> room;
        std::function<bool(int)> roomActivity; // the house bar's activities that are built for rooms (assigned below)
        std::function<void(int)> goHome;       // the outdoor bar's last button: to the door of this save's house (assigned below)
        std::function<void()> roomQuit;        // the quit button's Yes while in a room: sleep, then leave the game
        int roomMenuCategory = 0;              // the item screen's tab when a furnishing was chosen, to come back to it
        auto nearestNpc = [&]() -> decltype(auto) { return (*areaFlowCtl).nearestNpc(); };
        // The 12 signs the original builds at runtime with SprintF. Tried before the table lookup.

        // Minigame starters now live in the ContentDB as InteractionDefs (importInteractions in import.hpp).

        auto nearestShopItem = [&]() -> decltype(auto) { return (*areaFlowCtl).nearestShopItem(); };

        auto drawUi = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawUi(std::forward<decltype(a)>(a)...); };

        auto drawUiF = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawUiF(std::forward<decltype(a)>(a)...); };
        auto drawUiId = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawUiId(std::forward<decltype(a)>(a)...); };
        auto drawIcon = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawIcon(std::forward<decltype(a)>(a)...); };


        auto drawDialogue = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawDialogue(std::forward<decltype(a)>(a)...); };

        BarAnim &barAnim = S.ui.barAnim;
        int &barBusy = S.ui.barBusy; // g_isPetBusy
        bool &barInHouse = S.ui.barInHouse;
        const int kBarRate = 12;
        auto playBarAnim = [&](int id)
        { barAnim = {id, 0, 0, false, true}; };

        WalkBarHost barHost(barBusy, barInHouse, slot, playBarAnim);
        menubar::MenuBar bar(barHost);

        WalkGfx barGfx;
        barGfx.tile = [&](int sprite, int pal, int x, int y)
        {
            const gp::Id uid = gp::uiSpriteId(sprite, pal);
            const AnimRef A = animRefById(uid);
            static std::set<gp::Id> warned;
            if (A.size() == 0)
            {
                if (warned.insert(uid).second)
                    std::printf("bar tile missing: %s\n", uid.c_str());
                return;
            }
            const gp::SpriteFrame &s = A.anim->frames[0];
            SDL_Rect r{x + s.dx, y + s.dy, s.w, s.h};
            SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
        };
        std::unique_ptr<ItemScreen> itemScreenCtl; // built below, once everything it takes is declared (walk_items.hpp)
        auto ownedCount = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).ownedCount(std::forward<decltype(a)>(a)...); };
        auto itemName = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).itemName(std::forward<decltype(a)>(a)...); };
        const std::string emptyText = gp::readWideText(rom, 0xb2e4); // "Buy medicines at the pharmacy."

        // ---- pending events: dig spots, hidden quest items, ground items ----
        std::mt19937 digGen(SDL_GetTicks());
        const gp::RandFn rnd = gp::makeRandFn(digGen);
        gp::WorldItems events;
        {
            // the random-find table: the base game's rows plus whatever the mods changed (find records)
            std::vector<gp::WeightedItem> finds;
            for (const gp::FindWeightDef *f : db.findWeights())
                if (const gp::ItemDef *d = db.item(f->item); d && f->weight > 0)
                    finds.push_back({d->index, f->weight});
            long long totalWeight = 0;
            for (const gp::WeightedItem &w : finds)
                totalWeight += w.weight;
            std::printf("random finds: %zu rows, total weight %lld\n", finds.size(), totalWeight);
            events.setFindTable(std::move(finds));
            const gp::MinigameDef &pp = db.minigameOr("base:minigame/prize_pools");
            {
                const gp::MinigameDef &fp = db.minigameOr("base:minigame/friend_pools"), &cm = db.minigameOr("base:minigame/colour_machine");
                std::printf("friend pools: gifts %zu, advice %zu, default lines %zu; colour presets %zu\n", fp.list("gifts").size(), fp.idList("advice").size(),
                            fp.idList("defaultLines").size(), cm.list("presets").size());
            }
            std::printf("prize pools: first %zu, general %zu, prize tiers %zu/%zu/%zu/%zu, special %d\n", pp.list("first").size(), pp.list("general").size(),
                        pp.list("prizeA").size(), pp.list("prizeB").size(), pp.list("prizeC").size(), pp.list("prizeD").size(), pp.get("special"));
        }
        // Shared services for the free functions in walk_npc_talk.hpp / walk_script_bindings.hpp.
        WalkServices svc{db, scripts, player, dialogue, quests, S, events, refreshBits, saveGame,
                         [&]
                         { return cur->def->legacyId; },
                         {}, {}, {}, {}, {}, {}, {}, {}}; // dropGift, startRandomGame, startTrickShowcase, startTrickGame, enterRoom, startColorMachine, showMap, startSell: assigned later

        gp::FixedSpawnEnv spawnEnv = gp::playerSpawnEnv(player);
        spawnEnv.itemNumber = [&](const gp::Id &id) // db lookup, so mod items work
        {
            const gp::ItemDef *d = db.item(id);
            return d ? d->index : -1;
        };
        auto enterAreaEvents = [&](const gp::AreaDef &a, bool newZone)
        {
            gp::audio().stopSfx(); // ExitCurrentArea: Sound_StopAllChannels
            {
                gp::StockEvent ev; // Game_ProcessTick: some areas add to a shop's stock when the pet arrives
                ev.kind = gp::StockEvent::Area;
                ev.area = a.id;
                applyStock(ev);
            }
            if (gp::QuestNudge::applies(player.questsCompleted, quests.q.phase))
                nudge.restart(long(gameTicks)); // Game_ProcessTick: an area change restarts the reminder
            if (const int song = a.music >= 0 ? a.music : gp::rau::zoneSong(a.legacyId); song >= 0)
                gp::audio().playMusic(song); // Sound_LookupAndStart via RemapZoneIndexToAudioId; other areas keep the current song
            if (db.shopForArea(a.id))
                return; // shops use the old PendingList
            if (newZone)
            {
                events.clearNonPersistent();
                if (a.maxEvents > 0)
                    events.spawnRandom(a, gp::tuning().randomSpawnBatch, rnd);
            }
            gp::spawnFixedSpots(events, a.legacyId, db.fixedSpotsIn(a.legacyId), spawnEnv);
        };

        auto randIn = [&](int lo, int hi)
        { return std::uniform_int_distribution<int>(lo, hi)(digGen); };
        auto sfx = [](int id, int vol = 255)
        { gp::audio().playSfx(id, vol); };
        // UpdateStatChangeNotification: the world's loop and the rooms' loop both call it every tick
        auto tickStatNotice = [&]
        {
            if (player.pendingMoney != 0)
            {
                statNotice.add(0, player.pendingMoney);
                player.pendingMoney = 0;
            }
            if (const int snd = statNotice.tick([&](int i) -> long
                                                { return i == 0 ? player.money : player.personality[size_t(i - 1)]; });
                snd >= 0)
                sfx(snd);
        }; // engine/audio.hpp: audio/sfx/<id>.wav, silent if it is missing

        auto isSpecialItem = [](int i)
        { return (i >= 2 && i <= 17) || (i >= 0x109 && i <= 0x10b); };

        std::unique_ptr<WorldActions> worldActionsCtl; // built below, once everything it takes is declared (walk_world_actions.hpp)
        auto personalityDelta = [&](int stat, int delta) -> decltype(auto) { return (*worldActionsCtl).personalityDelta(stat, delta); };

        auto grantSpecial = [&](int id, bool sound) -> decltype(auto) { return (*worldActionsCtl).grantSpecial(id, sound); };

        // The first-time messages in the overworld (settings.hpp). tutStep: 0 none, 1 the pet turns to the front, 2 it waits
        // 0.25 s, then the message opens. afterPickup asks for the "see all your STUFF" message (15) once the pet is free.
        int tutStep = 0, tutEvent = -1, tutTicks = 0;
        bool tutNudge = false; // the "Go see Mayor Peter!" reminder instead of a tutorial message
        bool afterPickup = false;
        gp::ThoughtTimer hungerTimer, tiredTimer{-gp::kThoughtRetryTicks, gp::kTiredLimit}; // the hungry and tired bubbles (thought_bubble.hpp)
        gp::ThoughtTimer sickTimer{-gp::kThoughtRetryTicks, gp::kSickLimit}, moodTimer{-gp::kThoughtRetryTicks, gp::kMoodLimit};
        int barkStep = 0; // X while standing: activity 0xc, TickActor_EncounterReactionSequence
        std::unique_ptr<gp::ReactionSeq> barkSeq;
        // the tricks panel (trick_panel.hpp): barBusy 5 = opening, 4 = open; the pet then performs the trick or wonders
        int trickStep = 0, trickLevel = 0, questionStep = 0, questionTicks = 0, questionBubble = 0;
        gp::TrickShowcase petTrick;
        gp::CameraShake cameraShake; // the panda's belly flop
        int scriptLock = 0;          // what a script holds the pet back from (player.lock): 1 = walking, 2 = the action buttons
        int shakeDy = 0;
        BarAnim panelAnim;
        bool panelClosing = false;
        int potionStep = 0, exclaimTicks = -1; // a colour potion's reaction; the exclamation bubble's clock (-1 = none)
        int colorStep = 0, colorIdx = 0, colorDir = 0, colorTicks = 0; // the spaceship colour machine
        // What the machine cycles through for the pet's species: the db's presets (a mod can add to them), a mod palette only for the species it names.
        auto machineColours = [&]
        {
            return gp::machineColours(sp, db.minigameOr("base:minigame/colour_machine").list("presets"), [&](int n)
                                      { const gp::PaletteDef *p = db.paletteByNumber(n); return p ? p->species : -1; });
        };
        std::unique_ptr<gp::ReactionSeq> potionSeq;
        long tiredDecayAccum = 0, hungerDecayAccum = 0; // the stat decay counts (thought_bubble.hpp)
        int thoughtIcon = -1, thoughtTicks = 0; // the bubble up over the pet (icon tile), -1 = none
        std::function<bool(const std::string &)> scriptThink; // player.think(kind), set where the bubble code is

        auto tryPickup = [&](int idx) -> decltype(auto) { return (*worldActionsCtl).tryPickup(idx); };

        // The map-marker arrows (anims 0x10 / 0x11 / 0x12, 18 fps) are in icon_selector.hpp.
        constexpr int kEvPal = 0x28c, kFlowerSprite = 0x9c1, kHoleSprite = 0x9c2;

        auto drawWorldSprite = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawWorldSprite(std::forward<decltype(a)>(a)...); };
        int kHoleDY = 8; // tune by eye

        // Object type id -> shadow ("marker") sprite, -1 = none. Resolved the first time a type is drawn.
        std::map<gp::Id, int> objShadow;
        auto shadowFor = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).shadowFor(std::forward<decltype(a)>(a)...); };

        constexpr Uint8 kShadowAlpha = 175; // tune by eye: 255 = opaque
        auto drawShadowSprite = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawShadowSprite(std::forward<decltype(a)>(a)...); };

        auto drawShadow = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawShadow(std::forward<decltype(a)>(a)...); };
        auto drawNpcShadow = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawNpcShadow(std::forward<decltype(a)>(a)...); };
        auto drawItemShadow = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawItemShadow(std::forward<decltype(a)>(a)...); };

        auto drawGroundSprite = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawGroundSprite(std::forward<decltype(a)>(a)...); };
        auto drawEvent = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawEvent(std::forward<decltype(a)>(a)...); };

        auto categoryMatches = [](const gp::ItemDef &d, int category) -> bool
        {
            switch (category)
            {
            case 0: // all
                return true;
            // Item_BuildFilteredList's category tests: food is type 9, medicine 10, "quest" items 12, 14 and 15, and the room
            // tab is everything the other tabs don't take (types 9..12, 14..17 are left out).
            case 1: // food
                return d.type == 9;
            case 2: // medicine
                return d.type == 10;
            case 3: // quest items
                return d.type == 12 || d.type == 14 || d.type == 15;
            case 4: // room items
                return !(d.type == 9 || d.type == 10 || d.type == 11 || d.type == 12 || (d.type >= 14 && d.type <= 17));
            default:
                return false;
            }
        };

        // Giving items to the pet whose home you are visiting (the item screen's mode 2, game/room_scene.hpp): the list only
        // holds what they have room for, and giveTheirs is how many of each they have (read from their save on entering).
        bool sellMode = false; // the item screen opened by Jeremy (mode 1): choosing an item sells it (game/sale.hpp)
        bool giveMode = false;
        int giveLocation = 0;
        std::map<int, int> giveTheirs;

        auto buildFiltered = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).buildFiltered(std::forward<decltype(a)>(a)...); };

        ItemMenu &im = S.ui.im;

        auto itemWindow = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).itemWindow(std::forward<decltype(a)>(a)...); };
        auto itemRows = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).itemRows(std::forward<decltype(a)>(a)...); };

        auto tabAnimId = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).tabAnimId(std::forward<decltype(a)>(a)...); };
        // ItemMenuScreen's input: the grid (Menu opens the tab list), the tab list (Up/Down, Select on release, Cancel/Menu back), and
        // nothing while the list slides.
        bool itemLeave = false;
        auto itemMenuTick = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).itemMenuTick(std::forward<decltype(a)>(a)...); };

        Fade &fade = S.flow.fade; // starts black and fades in
        std::optional<gp::ExitDef> &pendingExit = S.flow.pendingExit;

        std::function<void()> &pendingScreen = S.flow.pendingScreen; // runs while the screen is fully black
        auto requestScreen = [&](std::function<void()> sw)
        {
            if (pendingScreen || pendingExit)
                return;
            pendingScreen = std::move(sw);
            fade.go(1.0, 8.0 / 30);
        };

        bool sceneActive = false; // a mod's scene (Scene.hooks) is on screen: the world is frozen behind it, like a minigame
        auto openBar = [&](bool house) -> decltype(auto) { return (*areaFlowCtl).openBar(house); };

        // A bar button that leaves for another screen: the bar plays its closing animation first, then the screen fades out
        // (the state change waits for the bar to be gone: HandleMenuBarInput sets the loop state to 4, which only becomes 3 once
        // g_isPetBusy is back to 0). Go Home is the exception: it sets the state to 3 at once and does not wait.
        int pendingBarState = -1;
        auto runBarState = [&](int s) -> decltype(auto) { return (*areaFlowCtl).runBarState(s); };
        barHost.onState = [&](int s)
        {
            if (s >= 0xb && s <= 0xd)
                runBarState(s);
            else
                pendingBarState = s;
        };

        // Queue draw calls, then sort by layer
        std::vector<Cmd> &menuCmds = S.ui.menuCmds;
        ItemGfx itemGfx;
        itemGfx.tile = [&](int sprite, int pal, int x, int y, int layer)
        {
            static std::set<gp::Id> warned;
            const gp::Id uid = uiIdP(sprite, pal);
            if (!animRefById(uid).size() && !gp::isModSprite(sprite)) // a palette not seen yet (potion, wash, player-colour plush)
                try
                {
                    gp::importUiSprite(db, rom, sprite, pal);
                }
                catch (const std::exception &)
                {
                }
            if (!animRefById(uid).size() && warned.insert(uid).second)
                std::printf("item menu tile missing: %s (at %d,%d layer %x)\n", uid.c_str(), x, y, layer);
            menuCmds.push_back({layer, [=, &animRefById]
                                {
                                    const AnimRef A = animRefById(uiIdP(sprite, pal));
                                    if (!A.size())
                                        return;
                                    const gp::SpriteFrame &s = A.anim->frames[0];
                                    SDL_Rect r{x - s.anchorX + s.dx, y - s.anchorY + s.dy + 1, s.w, s.h};
                                    SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
                                }});
        };
        itemGfx.box = [&](int style, int x, int y, const std::string &t, int w, int mode, int layer)
        {
            menuCmds.push_back({layer, [=, &text, &numbers, &badge]
                                {
                                    const gp::Align al = mode == 1   ? gp::Align::Center
                                                         : mode == 2 ? gp::Align::Right
                                                                     : gp::Align::Left;
                                    (style == 7 ? badge : style == 2 ? numbers
                                                                     : text)
                                        .draw(t, x, y, w, al);
                                }});
        };
        itemGfx.spriteW = [&](int s)
        {
            const AnimRef A = animRefById(gp::uiSpriteId(s));
            return A.size() ? A.anim->frames[0].h : 0;
        };

        // Swift Pick, wishing wells (Three in a Row) and Memory Match / game tents live in minigame_ui.hpp.
        TextDrawer swiftTitle(ren, db.font(gp::fontId(3, 0x104))); // minigame titles: font 3, palette 0x104
        MinigameEnv mgEnv{ren, rom, db, player, itemDefs, text, swiftTitle, swiftBgTex};
        mgEnv.randIn = randIn;
        mgEnv.sfx = sfx;
        mgEnv.animRefById = animRefById;
        mgEnv.requestScreen = requestScreen;
        mgEnv.showBox = [&](gp::DialogueBox b)
        { dialogue.show(std::move(b)); };
        mgEnv.drawSlotScene = [&] // DrawActiveSlotSprites(): same call the item menu makes
        {
            menuCmds.clear();
            gp::drawSlotScene(itemGfx, rom, itemSlot);
            std::stable_sort(menuCmds.begin(), menuCmds.end(),
                             [](const Cmd &a, const Cmd &b)
                             { return a.layer < b.layer; });
            for (Cmd &c : menuCmds)
                c.draw();
        };
        // mgEnv.startItemDrop is set where startItemDrop is defined (below).
        Minigames minigames(mgEnv);
        svc.startTrickGame = [&](const gp::NpcDef &d, int level, const std::string &trickName, std::function<void(bool)> done)
        {
            minigames.openTrick({level, trickName, d.portraitSprite, d.portraitPalette}, std::move(done));
        };
        svc.startTrickShowcase = [&](gp::NpcActor &n, int level, std::function<void()> done)
        {
            if (n.walking()) // the same hold the befriend encounter puts on an NPC (host.npcLock)
                n.startActivity(gp::kActivityTalkedTo, npcRng);
            n.locked = true;
            n.body.facing = n.x() < pos.x ? gp::FaceRight : gp::FaceLeft;
            showNpc = &n;
            showStarted = false;
            showLevel = level;
            showDone = std::move(done);
        };

        int &facing = S.pet.facing;        // start facing down
        bool &faceLeft = S.pet.faceLeft; // the game's facing word: 0 = left, 1 = right (2 = up, 3 = down); persists while turning
        int &speedMul = S.pet.speedMul;
        int &walkTicks = S.pet.walkTicks;
        uint8_t &prevDirs = S.pet.prevDirs; // to detect new key presses
        const int kFrameRate = 14;          // the rate the game passes for walking; frames per second at 30 ticks/sec
        int &anchorMode = S.pet.anchorMode, &offX = S.pet.offX, &offY = S.pet.offY;
        int &flipMode = S.pet.flipMode; // 0 = old mirror formula, 1 = hypothesis using record word 2 (toggle with F)

        // ---- pet animation state ----
        Pose &pose = S.pet.pose; // Move with walkTicks == 0 shows walk frame 0
        int &settleTicks = S.pet.settleTicks;
        const int kSettleTicks = 5; // the game waits 0.15 s with no direction held (2 ticks of delta per 30 Hz tick)
        int &animId = S.pet.animId, &animFrame = S.pet.animFrame, &animTicks = S.pet.animTicks, &animRate = S.pet.animRate;
        bool &animLoop = S.pet.animLoop, &animDone = S.pet.animDone;
        std::vector<AnimStep> &queue = S.pet.queue; // what plays after the current one-shot finishes
        int &idleTicks = S.pet.idleTicks, &idleThreshold = S.pet.idleThreshold; // base idle (34) -> after 3.0-6.0 s the game runs action 7 (46, then 45)
        bool &animReverse = S.pet.animReverse; // move this up, before playAni
        int &feedStep = S.pet.feedStep, &feedRepeats = S.pet.feedRepeats, &feedCount = S.pet.feedCount, &feedHappy = S.pet.feedHappy;

        bool &medStuck = S.pet.medStuck; // wrong medicine: walking in place until another animation plays
        int &medStuckTicks = S.pet.medStuckTicks, &medStuckLimit = S.pet.medStuckLimit;
        int &overId = S.pet.overId, &overTicks = S.pet.overTicks; // overhead medicine animation (-1 = none)
        bool &feedFood = S.pet.feedFood; // the +6 Happiness bonus is food only

        // The standing idle by how the pet is (TickPlayerActorIdleBehavior): sick -> FlagB, unhappy -> FlagA, else the base one.
        auto restIdleId = [&]
        {
            return !player.isHealthy()                                    ? gp::artOf(sp).idleFlagB
                   : player.care[gp::Happiness] < gp::kUnhappyBelow ? gp::artOf(sp).idleFlagA
                                                                       : gp::artOf(sp).idleBase;
        };
        auto restIdleRate = [&] { return restIdleId() == gp::artOf(sp).idleBase ? 14 : restIdleId() == gp::artOf(sp).idleFlagB ? gp::artOf(sp).idleFlagBRate : 7; };
        auto isRestIdle = [&](int aid) { return aid == gp::artOf(sp).idleBase || aid == gp::artOf(sp).idleFlagA || aid == gp::artOf(sp).idleFlagB; };

        auto playAnim = [&](int aid, bool loop, int rate, Pose p,
                            std::vector<AnimStep> next = {})
        {
            medStuck = false;    // any new animation ends the stuck walk
            animReverse = false; // add this line
            animId = aid;
            animFrame = 0;
            animTicks = 0;
            animRate = rate;
            animLoop = loop;
            animDone = false;
            pose = p;
            queue = std::move(next);
            if (isRestIdle(aid) && p == Pose::Idle)
            {
                idleTicks = 0;
                idleThreshold = 90 + std::rand() % 91; // RandRange(30,60) / 10 seconds at 30 ticks/sec
            }
            // std::printf("play anim %d (%s, rate %d)\n", aid, loop ? "loop" : "once", rate);
        };

        gp::EncounterHost host;
        std::unique_ptr<EncounterHostBindings> encounterHostBindingsCtl; // built below, once everything it takes is declared (walk_encounter_host.hpp)
        host.classify = [&](int petDelta, int npcDelta, int &playerCode, int &animalCode) { return (*encounterHostBindingsCtl).host_classifyRun(petDelta, npcDelta, playerCode, animalCode); };
        auto walkAxis = [&](int target, bool xAxis) -> decltype(auto) { return (*encounterHostBindingsCtl).walkAxis(target, xAxis); };
        host.petX = [&]() { return (*encounterHostBindingsCtl).host_petXRun(); };
        host.npcX = [&]() { return (*encounterHostBindingsCtl).host_npcXRun(); };
        host.npcY = [&]() { return (*encounterHostBindingsCtl).host_npcYRun(); };
        host.petWalkToX = [&](int x) { return (*encounterHostBindingsCtl).host_petWalkToXRun(x); };
        host.petWalkToY = [&](int y) { return (*encounterHostBindingsCtl).host_petWalkToYRun(y); };
        host.npcLock = [&]() { return (*encounterHostBindingsCtl).host_npcLockRun(); };
        host.npcSettled = [&]() { return (*encounterHostBindingsCtl).host_npcSettledRun(); };
        host.npcUnlock = [&]() { return (*encounterHostBindingsCtl).host_npcUnlockRun(); };
        host.markFriended = [&]() { return (*encounterHostBindingsCtl).host_markFriendedRun(); };
        host.event = [&](const char *k, int v) { return (*encounterHostBindingsCtl).host_eventRun(k, v); };
        host.petY = [&]() { return (*encounterHostBindingsCtl).host_petYRun(); };
        host.gifts = [&](int friends, int &drop, int &milestone) { return (*encounterHostBindingsCtl).host_giftsRun(friends, drop, milestone); };
        host.spawnItem = [&](int item, int x, int y) { return (*encounterHostBindingsCtl).host_spawnItemRun(item, x, y); };

        host.pet.play = [&](gp::ReactAnim a, int v) { return (*encounterHostBindingsCtl).host_pet_playRun(a, v); };

        host.pet.busy = [&]() { return (*encounterHostBindingsCtl).host_pet_busyRun(); };
        host.pet.sound = [&](int c, int vol) { return (*encounterHostBindingsCtl).host_pet_soundRun(c, vol); };
        host.pet.bubble = [&](int id) { return (*encounterHostBindingsCtl).host_pet_bubbleRun(id); };
        host.pet.rand = [&](int lo, int hi) { return (*encounterHostBindingsCtl).host_pet_randRun(lo, hi); };

        int &settlePhase = S.pet.settlePhase; // TickActor_SettleFacing: no 0.15 s wait, ends facing down
        host.pet.settle = [&]() { return (*encounterHostBindingsCtl).host_pet_settleRun(); };
        host.petIdle = [&]() { return (*encounterHostBindingsCtl).host_petIdleRun(); };
        host.petFace = [&](bool left) { return (*encounterHostBindingsCtl).host_petFaceRun(left); };
        host.petEngage = [&]() { return (*encounterHostBindingsCtl).host_petEngageRun(); };

        host.npcIdle = [&]() { return (*encounterHostBindingsCtl).host_npcIdleRun(); };
        host.npcFacePet = [&]() { return (*encounterHostBindingsCtl).host_npcFacePetRun(); };
        host.npc.rand = [](int lo, int hi)
        { return hi <= lo ? lo : lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive
        bool npcMute = false; // an idle NPC off screen makes no sound
        host.npc.sound = [&](int c, int vol) { return (*encounterHostBindingsCtl).host_npc_soundRun(c, vol); };
        host.npc.bubble = [](int id)
        { std::printf("npc bubble 0x%x\n", id); };
        host.npc.settle = [&]() { return (*encounterHostBindingsCtl).host_npc_settleRun(); };
        host.npc.busy = [&]() { return (*encounterHostBindingsCtl).host_npc_busyRun(); };
        host.npc.play = [&](gp::ReactAnim a, int v) { return (*encounterHostBindingsCtl).host_npc_playRun(a, v); };
        {
            EncounterHostBindingsEnv env{animDone, animFrame, animId, animLoop, animRefById, animReverse, cur, db, encNpc, events, exclaimTicks, faceLeft, facing, getAnim, isRestIdle, itemDefs, itemName, npcMute, npcRng, ownedCount, playAnim, playerSpecies, pos, pose, reactAnimId, restIdleId, restIdleRate, scripts, settlePhase, settleTicks, sfx, sp, walkTicks, world};
            encounterHostBindingsCtl = std::make_unique<EncounterHostBindings>(std::move(env));
        }

        std::unique_ptr<PetActivities> petActivitiesCtl; // built below, once everything it takes is declared (walk_pet_activities.hpp)
        auto petSound = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).petSound(std::forward<decltype(a)>(a)...); };
        auto playFeedHappy = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).playFeedHappy(std::forward<decltype(a)>(a)...); };
        auto chewSound = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).chewSound(std::forward<decltype(a)>(a)...); };

        auto startFeed = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).startFeed(std::forward<decltype(a)>(a)...); };

        auto startMedicine = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).startMedicine(std::forward<decltype(a)>(a)...); };

        auto tickFeed = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickFeed(std::forward<decltype(a)>(a)...); };

        auto startWorldTutorial = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).startWorldTutorial(std::forward<decltype(a)>(a)...); };
        auto tickWorldTutorial = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickWorldTutorial(std::forward<decltype(a)>(a)...); };

        auto showThought = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).showThought(std::forward<decltype(a)>(a)...); };
        scriptThink = [&](const std::string &kind) { return (*petActivitiesCtl).scriptThinkRun(kind); };
        auto tickThoughtBubble = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickThoughtBubble(std::forward<decltype(a)>(a)...); };

        auto startPotionReaction = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).startPotionReaction(std::forward<decltype(a)>(a)...); };
        auto tickPotion = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickPotion(std::forward<decltype(a)>(a)...); };

        auto pressCancelInWorld = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).pressCancelInWorld(std::forward<decltype(a)>(a)...); };
        auto tickBark = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickBark(std::forward<decltype(a)>(a)...); };

        // ---- the tricks panel (trick_panel.hpp) ----
        barHost.onTricks = [&]
        {
            gp::SystemCtx pc; // the engine system "panel.open" (panel "tricks"); a cancel closes the bar again
            pc.set("panel", "tricks");
            const bool ran = scripts.system("panel.open", pc, [&]
                                            {
                barBusy = 5; // HandleTrickAction: the bar closes (anim 3) and the panel opens (anim 4)
                sfx(trickLay.click, trickLay.clickVolume);
                barAnim = {gp::kTrickBarCloseAnim, 0, 0, false, true};
                panelAnim = {gp::kTrickPanelAnim, 0, 0, false, true};
                panelClosing = false; });
            if (!ran)
                bar.forceClose();
        };
        auto closeTrickPanel = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).closeTrickPanel(std::forward<decltype(a)>(a)...); };
        auto startQuestion = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).startQuestion(std::forward<decltype(a)>(a)...); };
        auto startTrick = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).startTrick(std::forward<decltype(a)>(a)...); };
        auto pickTrick = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).pickTrick(std::forward<decltype(a)>(a)...); };
        auto tickQuestion = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickQuestion(std::forward<decltype(a)>(a)...); };
        auto tickTrick = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickTrick(std::forward<decltype(a)>(a)...); };

        // ---- the island map (world_map.hpp): a full-screen picture box with a marker where the pet is ----
        svc.showMap = [&]
        {
            gp::SystemCtx pc; // the engine system "panel.open" (panel "map")
            pc.set("panel", "map");
            scripts.system("panel.open", pc, [&]
                           {
                const auto m = gp::mapMarker(cur->def->legacyId, pos.x, pos.y);
                gp::DialogueBox b;
                b.kind = gp::BoxKind::Picture;
                b.markX = m ? m->x : 0; // an area off the map keeps the last marker in the ROM; there is none to keep here
                b.markY = m ? m->y : 0;
                dialogue.show(std::move(b)); });
        };

        // ---- the spaceship's colour machine (activity 0x29); see color_machine.hpp ----
        // Step 1: turn to face front. Step 2: the colour cycles every 4 steps until Z; the new colour is the pet's base one.
        svc.startColorMachine = [&]
        {
            colorStep = 1;
            settlePhase = 0;
            walkTicks = 0;
        };
        auto tickColorMachine = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickColorMachine(std::forward<decltype(a)>(a)...); };

        // ---- washing the pet: bar button 4 starts activity 0x27 (HandlePetWashInteraction); see pet_wash.hpp ----
        int &washStep = S.pet.washStep; // 0 = not washing; 1 get up and start the bubble, 2 the bubble plays, 3 the happy reaction
        std::unique_ptr<gp::ReactionSeq> washSeq;

        auto startWash = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).startWash(std::forward<decltype(a)>(a)...); };

        auto tickWash = [&](auto &&...a) -> decltype(auto) { return (*petActivitiesCtl).tickWash(std::forward<decltype(a)>(a)...); };
        {
            PetActivitiesEnv env{animDone, animFrame, animId, animLoop, animRefById, animReverse, bar, barAnim, barBusy, barkSeq, barkStep, cameraShake, colorDir, colorIdx, colorStep, colorTicks, db, dialogue, enc, exclaimTicks, faceLeft, facing, feedCount, feedFood, feedHappy, feedRepeats, feedStep, gameTicks, getAnim, host, hungerDecayAccum, hungerTimer, importPetAnims, machineColours, moodTimer, overId, overTicks, panelAnim, panelClosing, petPal, petTrick, playAnim, pose, potionSeq, potionStep, questionBubble, questionStep, questionTicks, reactAnimId, restIdleId, restIdleRate, rom, room, saveGame, scripts, settings, settlePhase, sfx, sickTimer, sp, stinkTicks, thoughtIcon, thoughtTicks, tiredDecayAccum, tiredTimer, trickLay, trickLevel, trickStep, tutEvent, tutNudge, tutStep, tutTicks, walkTicks, washSeq, washStep};
            petActivitiesCtl = std::make_unique<PetActivities>(std::move(env));
        }
        barHost.onActivity = [&](int id, int, int, int)
        {
            if (room) // the house bar's buttons
            {
                if (roomActivity && roomActivity(id))
                    return true;
                if (id == 4) // go out: the pet walks to the door and leaves
                {
                    room->phase = RoomView::Phase::Leaving;
                    room->walkTicks = 0;
                }
                else
                    std::printf("bar: activity 0x%x (sleep) isn't built for rooms yet\n", id);
                return true;
            }
            if (id != gp::kWashActivity)
                return false;
            startWash();
            return true;
        };
        // The house bar's last button asks "Are you sure you want to quit the game?" (0x20b7). Yes sends the pet to bed
        // (StartPetActivity(0x25) at the bed) and then quits; here it saves and goes straight back to the front-end menu.
        barHost.onDialogue = [&](int id)
        {
            if (id != 0x20b7)
            {
                std::printf("bar: dialogue 0x%x\n", id);
                return;
            }
            gp::DialogueBox b = gp::messageBox(db.textOr("base:str/quit_prompt", "Are you sure you want to quit the game?"));
            b.kind = gp::BoxKind::Confirm;
            b.onYes = [&]
            {
                if (room && roomQuit) // the pet goes to bed first
                {
                    roomQuit();
                    return;
                }
                saveGame();
                S.flow.backToMenu = true;
                S.flow.running = false;
            };
            b.onNo = [] {};
            dialogue.show(std::move(b));
        };

        // ---- player homes and hotels: the code lives in game/walk/walk_room.hpp (RoomController); what it takes from this function goes in through RoomEnv ----
        RoomEnv roomEnv{rom, db, itemDefs, player, scripts, dialogue, S, svc, ren, im, sp, slot, W, gameTicks, walkTicks, pos, prevPos, petPal, facing, offX, offY, barBusy, barHost.category,
                        settings, giveMode, giveLocation, giveTheirs, roomMenuCategory, room, roomActivity, roomQuit, kFrameRate, saveGame,
                        animRefById, getAnim, reactAnimId, uiIdP, requestScreen, petSound, drawShadow, chewSound, buildFiltered, smoothAt, restIdleId, restIdleRate, playAnim, importPetAnims};
        RoomController roomCtl(std::move(roomEnv));
        roomCtl.sfxFn = [](int id, int vol) { gp::audio().playSfx(id, vol); };
        auto roomSlots = [&](auto &&...a) -> decltype(auto) { return roomCtl.roomSlots(std::forward<decltype(a)>(a)...); };
        auto refreshRoom = [&](auto &&...a) -> decltype(auto) { return roomCtl.refreshRoom(std::forward<decltype(a)>(a)...); };
        auto beginRoom = [&](auto &&...a) -> decltype(auto) { return roomCtl.beginRoom(std::forward<decltype(a)>(a)...); };
        auto leaveRoom = [&](auto &&...a) -> decltype(auto) { return roomCtl.leaveRoom(std::forward<decltype(a)>(a)...); };
        auto setBowl = [&](auto &&...a) -> decltype(auto) { return roomCtl.setBowl(std::forward<decltype(a)>(a)...); };
        auto startEating = [&](auto &&...a) -> decltype(auto) { return roomCtl.startEating(std::forward<decltype(a)>(a)...); };
        auto startRoomWash = [&](auto &&...a) -> decltype(auto) { return roomCtl.startRoomWash(std::forward<decltype(a)>(a)...); };
        auto startSleep = [&](auto &&...a) -> decltype(auto) { return roomCtl.startSleep(std::forward<decltype(a)>(a)...); };
        auto startRoomMedicine = [&](auto &&...a) -> decltype(auto) { return roomCtl.startRoomMedicine(std::forward<decltype(a)>(a)...); };
        auto tutorialBegin = [&](auto &&...a) -> decltype(auto) { return roomCtl.tutorialBegin(std::forward<decltype(a)>(a)...); };
        auto startTutorial = [&](auto &&...a) -> decltype(auto) { return roomCtl.startTutorial(std::forward<decltype(a)>(a)...); };
        auto roomTutorial = [&](auto &&...a) -> decltype(auto) { return roomCtl.roomTutorial(std::forward<decltype(a)>(a)...); };
        auto tickEat = [&](auto &&...a) -> decltype(auto) { return roomCtl.tickEat(std::forward<decltype(a)>(a)...); };
        auto equipSlot = [&](auto &&...a) -> decltype(auto) { return roomCtl.equipSlot(std::forward<decltype(a)>(a)...); };
        auto loadTheirItems = [&](auto &&...a) -> decltype(auto) { return roomCtl.loadTheirItems(std::forward<decltype(a)>(a)...); };
        auto startGive = [&](auto &&...a) -> decltype(auto) { return roomCtl.startGive(std::forward<decltype(a)>(a)...); };
        auto reopenItemScreen = [&](auto &&...a) -> decltype(auto) { return roomCtl.reopenItemScreen(std::forward<decltype(a)>(a)...); };
        auto tickRoom = [&](auto &&...a) -> decltype(auto) { return roomCtl.tickRoom(std::forward<decltype(a)>(a)...); };
        auto drawRoom = [&](auto &&...a) -> decltype(auto) { return roomCtl.drawRoom(std::forward<decltype(a)>(a)...); };
        svc.enterRoom = [&](int location) { roomCtl.enterRoom(location); };
        roomQuit = [&] { roomCtl.roomQuitRun(); };
        roomActivity = [&](int id) { return roomCtl.roomActivityRun(id); };
        int &digAnimDY = S.pet.digAnimDY; // 0 = anchor sits at the pet's feet; tune with 9 / 0
        int &digStep = S.pet.digStep, &digTicks = S.pet.digTicks;
        constexpr int kDigTicks = 30;                     // placeholder: length of sound 0xbe (SFX category 9)

        std::vector<HoleFill> holeFills;
        constexpr int kHoleFillFps = 0x1c;
        auto tickHoleFills = [&]() -> decltype(auto) { return (*worldActionsCtl).tickHoleFills(); };

        std::vector<Blast> blasts;
        int gateBias = 0; // added to the quest count when objects are shown or hidden: 1 once a blast has changed the scenery
        auto tickBlasts = [&]() -> decltype(auto) { return (*worldActionsCtl).tickBlasts(); };

        auto startDig = [&]() -> decltype(auto) { return (*worldActionsCtl).startDig(); };

        auto tickDig = [&]() -> decltype(auto) { return (*worldActionsCtl).tickDig(); };

        int &swiftDropStep = S.pet.swiftDropStep, &swiftDropTicks = S.pet.swiftDropTicks;
        std::vector<int> &swiftDropItems = S.pet.swiftDropItems;
        auto startItemDrop = [&](std::vector<int> items) -> decltype(auto) { return (*worldActionsCtl).startItemDrop(items); };
        // the engine system "minigame.prize": what a minigame hands out (game: "swift_pick", "three_in_a_row" or "memory_match"; items: item ids joined by commas, one per prize, repeats
        // for more of one; change the list, or cancel / replace and nothing drops). An unknown id is dropped from the list.
        mgEnv.startItemDrop = [&](std::vector<int> items)
        {
            std::string list;
            for (int n : items)
                if (n >= 0 && n < int(itemDefs.size()))
                    list += (list.empty() ? "" : ",") + itemDefs[size_t(n)].id;
            gp::SystemCtx c;
            c.set("game", mgEnv.game).set("items", list);
            scripts.system("minigame.prize", c, [&]
                           {
                std::vector<int> out;
                std::string rest = c.str("items");
                for (size_t at = 0; at <= rest.size();)
                {
                    size_t end = rest.find(',', at);
                    if (end == std::string::npos)
                        end = rest.size();
                    std::string id = rest.substr(at, end - at);
                    at = end + 1;
                    while (!id.empty() && id.front() == ' ')
                        id.erase(id.begin());
                    if (const gp::ItemDef *d = id.empty() ? nullptr : db.item(id))
                        out.push_back(d->index);
                }
                if (!out.empty())
                    startItemDrop(std::move(out)); });
        };
        // the engine system "minigame.start": a minigame is about to open (game: "swift_pick", "three_in_a_row", "memory_match" or "trick_guess"); cancel or replace and it does not
        mgEnv.begin = [&](const char *game, std::function<void()> go)
        {
            gp::SystemCtx c;
            c.set("game", game);
            scripts.system("minigame.start", c, [&]
                           { go(); });
        };
        // A friend's gift and random game: both target the pet's position, 10 px lower (PlayRandomMinigame and
        // ReceiveCureGiftFromFriend pass GetCameraScrollX() and GetCameraScrollY() + 10, which is the pet).
        svc.dropGift = [&](int item)
        {
            minigames.petX = pos.x;
            minigames.petY = pos.y + 10;
            startItemDrop({item});
        };
        svc.startRandomGame = [&]
        { minigames.openRandomGame(pos.x, pos.y + 10); };
        auto tickSwiftDrop = [&]() -> decltype(auto) { return (*worldActionsCtl).tickSwiftDrop(); };
        {
            WorldActionsEnv env{afterPickup, animDone, animRefById, blasts, cur, db, dialogue, digStep, digTicks, events, holeFills, host, isSpecialItem, itemDefs, itemName, kDigTicks, kHoleFillFps, minigames, overId, overTicks, ownedCount, pendingScreen, petFellIll, playAnim, pos, randIn, reactAnimId, restIdleId, restIdleRate, rnd, scripts, settlePhase, sfx, sp, spawnEnv, swiftDropItems, swiftDropStep, swiftDropTicks, walkTicks};
            worldActionsCtl = std::make_unique<WorldActions>(std::move(env));
        }

        auto useFurnishing = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).useFurnishing(std::forward<decltype(a)>(a)...); };

        auto giveSelected = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).giveSelected(std::forward<decltype(a)>(a)...); };

        auto sellSelected = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).sellSelected(std::forward<decltype(a)>(a)...); };
        svc.startSell = [&] // ShowSellMenu: the item screen on the first tab, in sell mode
        {
            requestScreen([&]
                          {
                sellMode = true; // set while the screen is black, once the item screen is really open
                im = {};
                im.open = true;
                im.category = 0;
                im.filtered = buildFiltered(0); });
        };

        auto useSelectedItem = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).useSelectedItem(std::forward<decltype(a)>(a)...); };

        // ---- 4. STATE + AREA SWITCHING ----
        bool &exitsArmed = S.flow.exitsArmed; // stops bouncing straight back through the door you arrived by
        bool &prevZ = S.flow.prevZ;
        bool &prevX = S.flow.prevX;

        auto goTo = [&](const gp::ExitDef &e) -> decltype(auto) { return (*areaFlowCtl).goTo(e); };
        goHome = [&](int location) { return (*areaFlowCtl).goHomeRun(location); };
        gp::Id blockedExit; // a door the pet walks into that a mod shut (onBeforeExit) is asked about once, until the pet steps away
        auto requestGoTo = [&](const gp::ExitDef &e) -> decltype(auto) { return (*areaFlowCtl).requestGoTo(e); };

        auto removeShopItem = [&](int area, int slot) { // provisional, see below
            for (size_t k = 0; k < pending.v.size(); ++k)
                if (pending.v[k].area == area && pending.v[k].slot == slot)
                {
                    pending.v.erase(pending.v.begin() + k);
                    break;
                }
        };

        auto buyFromShop = [&](auto &&...a) -> decltype(auto) { return (*itemScreenCtl).buyFromShop(std::forward<decltype(a)>(a)...); };
        {
            ItemScreenEnv env{animRefById, barAnim, barBusy, categoryMatches, cur, db, dialogue, equipSlot, facing, giveLocation, giveMode, giveTheirs, im, importPetAnims, itemDefs, itemLeave, medStuck, medStuckLimit, medStuckTicks, money, pending, petPal, pose, requestScreen, rom, room, roomMenuCategory, saveGame, scripts, sellMode, sfx, shopStock, slot, sp, startEating, startFeed, startMedicine, startPotionReaction, startRoomMedicine, svc, walkTicks};
            itemScreenCtl = std::make_unique<ItemScreen>(std::move(env));
        }
        for (int id : {0xf5, 0xf4, 0xf3, 0xf2, 0xf0, 0xef, 0xee, 0, 0xf1, 0xed, 0, 0x105}) // (a printout: it needs the item screen, so it comes after the line above)
            std::printf("swift pick %3d (0x%x): %s\n", id, id,
                        id < int(itemDefs.size()) ? itemName(id).c_str() : "?");

        // ---- quests ----
        // NPC lookup by pool id (NpcDef::index). NpcDef's area field is assumed to be `area`: rename if it differs.
        std::vector<const gp::NpcDef *> npcByIndex(0x4e, nullptr);
        std::map<int, const gp::NpcDef *> modPoolNpcs; // mod npcs with quest_pool: they give and receive generated quests under their own index (1000 and up)
        for (const gp::NpcDef *d : db.allNpcs())
            if (d->index >= 0 && d->index < 0x4e)
                npcByIndex[size_t(d->index)] = d;
            else if (d->questPool)
                modPoolNpcs[d->index] = d;
        auto npcAt = [&](int p) -> const gp::NpcDef *
        {
            if (p >= 0 && p < 0x4e)
                return npcByIndex[size_t(p)];
            const auto it = modPoolNpcs.find(p);
            return it == modPoolNpcs.end() ? nullptr : it->second;
        };

        {
            std::set<std::pair<int, int>> done;
            for (const gp::NpcDef *d : npcByIndex)
                if (d && done.insert({d->portraitSprite, d->portraitPalette}).second)
                    try
                    {
                        gp::importUiSprite(db, rom, d->portraitSprite, d->portraitPalette);
                    }
                    catch (const std::exception &)
                    {
                    }
        }

        // The overhead animation of a stationary NPC (its record's last word): in its own palette.
        for (const gp::NpcDef *d : npcByIndex)
            if (d && d->stationary && d->behaviorFlags != gp::kNoOverheadAnim)
                try
                {
                    gp::importAnim(rom, db, d->behaviorFlags, d->appearancePalette);
                }
                catch (const std::exception &)
                {
                }
        // The art for the tricks idle NPCs perform (activity 0x13), in their own species and palette.
        for (const gp::NpcDef *d : npcByIndex)
            if (d && !d->stationary && (d->behaviorFlags & 0xe0) && gp::knownSpecies(d->speciesOrAnim))
            {
                const int s = d->speciesOrAnim;
                const std::vector<int> ids = gp::artOf(s).trickAnims(); // the trick art of this species, the panda's reaction chain too
                for (int id : ids)
                    if (id >= 0)
                        try
                        {
                            gp::importAnim(rom, db, id, d->appearancePalette);
                        }
                        catch (const std::exception &)
                        {
                        }
            }
        // Trick showcase art: the teachers perform the trick of the player's species, coloured with their own palette.
        for (const gp::NpcDef *d : npcByIndex)
            if (d && d->index >= 0x30 && d->index <= 0x32)
                for (int level = 1; level <= 3; ++level)
                    for (int id : {gp::TrickShowcase::introAnim(level, sp), gp::TrickShowcase::mainAnim(level, sp)})
                        if (id >= 0)
                            try
                            {
                                gp::importAnim(rom, db, id, d->appearancePalette);
                            }
                            catch (const std::exception &e)
                            {
                                std::printf("trick showcase anim 0x%x import failed: %s\n", id, e.what());
                            }

        quests.table = db.quests();          // scripted quests: imported by importRom, mods can replace or append
        quests.world.rom = db.questTables(); // generic-quest item weights and category distances: also from the db
        quests.world.types = db.questTypes();
        for (gp::QuestTypeDef &t : quests.world.types) // a mod type's fixed NPC, by id -> its pool number (it must be in the pool: a base NPC, or one with quest_pool); else the type picks randomly
            if (!t.fixedNpcId.empty())
            {
                t.fixedNpc = -1;
                for (int p = 0; p < 0x4e && t.fixedNpc < 0; ++p)
                    if (npcByIndex[size_t(p)] && npcByIndex[size_t(p)]->id == t.fixedNpcId)
                        t.fixedNpc = p;
                for (const auto &[p, n] : modPoolNpcs)
                    if (n->id == t.fixedNpcId)
                        t.fixedNpc = p;
                if (t.fixedNpc < 0)
                    t.receiver = gp::QuestReceiver::Random;
            }
        quests.world.playerSpecies = sp;
        for (const auto &[idx, d] : modPoolNpcs)
            quests.world.extraPool.push_back(idx);
        quests.world.areaTier = [&](int p) // when quests may use the npc (a mod npc says so itself)
        { const gp::NpcDef *d = npcAt(p); return !d ? 0 : d->questPool && d->index >= 0x4e ? d->questTier : d->area; };
        quests.world.stationary = [&](int p)
        { const gp::NpcDef *d = npcAt(p); return d && d->stationary; };
        quests.dlg = &dialogue;
        quests.world.speciesOf = [&](int p)
        {
            const gp::NpcDef *d = npcAt(p);
            int s = d ? d->speciesOrAnim : 0;
            if (s == 0xfffe)
                s = sp; // 0xfffe = "same as the player", as in importNpcs
            return gp::knownSpecies(s) ? std::clamp(gp::artOf(s).kit, 0, 2) : 0; // a mod's species counts as the base one it takes its kit from
        };

        auto camNowX = [&]
        { return clampCam(pos.x - W / 2, cur->img.w, W); };
        auto camNowY = [&]
        { return clampCam(pos.y - gp::tuning().camY, cur->img.h, H); };
        refreshBits = [&]
        {
            for (const gp::Id &sid : db.offerSetIds())
                if (const gp::OfferSetDef *s = db.offerSet(sid))
                    gp::autoMarkOffers(*s, quests.counter, player);
            for (const gp::ObjectDef &o : cur->def->objects)
            {
                const bool needsOffer = !o.requiresOffered.empty();
                const bool offered = needsOffer && player.hasFlag(gp::offeredKey(o.requiresOffered));
                const bool gone = !o.goneOnFlag.empty() && player.hasFlag(o.goneOnFlag);
                cur->runtime->setHidden(o.id, (needsOffer && !offered) || !o.availableAt(quests.counter + gateBias) || gone);
            }
            cur->runtime->updateVisible(camNowX(), camNowY(), W, H);
        };

        // ---- scripts: object.spawn / move / remove and npc.spawn / move / remove (in the area the pet is in) ----
        {
            gp::ScriptApi &sapi = scripts.api();
            sapi.playSound = [&](int id, int vol)
            { sfx(id, vol); };
            sapi.playMusic = [&](int song)
            {
                if (song < 0)
                    gp::audio().stopMusic();
                else
                    gp::audio().playMusic(song);
            };
            sapi.spawnObject = [&](const std::string &id, const std::string &type, int x, int y, int rx, int ry)
            {
                const gp::ObjectType *ot = db.objectType(type);
                if (!ot)
                    return false;
                gp::ObjectDef o;
                o.id = id;
                o.type = type;
                o.x = x;
                o.y = y;
                if (rx > 0 && ry > 0)
                {
                    o.reachX = rx;
                    o.reachY = ry;
                    o.interactId = 0;
                }
                if (ot->hasArt) // the box the art covers, for the cull (as the loader does for a mod's object)
                {
                    o.boxLeft = x - ot->art.anchorX + ot->art.dx;
                    o.boxTop = y - ot->art.anchorY + ot->art.dy;
                    o.boxRight = o.boxLeft + ot->art.w;
                    o.boxBottom = o.boxTop + ot->art.h;
                }
                else
                {
                    o.boxLeft = x - std::max(rx, 16);
                    o.boxRight = x + std::max(rx, 16);
                    o.boxTop = y - std::max(ry, 16);
                    o.boxBottom = y + std::max(ry, 16);
                }
                if (!cur->runtime->addObject(std::move(o)))
                    return false;
                cur->runtime->updateVisible(camNowX(), camNowY(), W, H);
                return true;
            };
            sapi.moveObject = [&](const std::string &id, int x, int y)
            {
                const bool ok = cur->runtime->moveObject(id, x, y);
                if (ok)
                    cur->runtime->updateVisible(camNowX(), camNowY(), W, H);
                return ok;
            };
            sapi.setObjectType = [&](const std::string &id, const std::string &type)
            {
                const bool ok = cur->runtime->setObjectType(id, type);
                if (ok)
                    cur->runtime->updateVisible(camNowX(), camNowY(), W, H);
                return ok;
            };
            sapi.removeObject = [&](const std::string &id)
            {
                const bool ok = cur->runtime->removeObject(id);
                if (ok)
                    cur->runtime->updateVisible(camNowX(), camNowY(), W, H);
                return ok;
            };

            // Is this NPC in the area, counting the changes still waiting to be made to the live actors?
            auto npcHere = [&](const gp::Id &id)
            {
                bool here = false;
                for (const gp::NpcActor &n : npcs)
                    if (n.def->id == id)
                        here = true;
                for (const auto &op : scriptNpc.pending)
                    if (op.id == id)
                        here = op.kind != ScriptNpcState::Op::Remove;
                return here;
            };
            auto npcCount = [&]
            {
                long n = long(npcs.size());
                for (const auto &op : scriptNpc.pending)
                    n += op.kind == ScriptNpcState::Op::Spawn ? 1 : op.kind == ScriptNpcState::Op::Remove ? -1 : 0;
                return n;
            };
            sapi.spawnNpc = [&, npcHere, npcCount](const std::string &id, int x, int y)
            {
                if (!db.npc(id) || npcHere(id) || npcCount() >= long(kMaxNpcs))
                    return false;
                if (scriptNpc.removed.erase(id)) // an NPC of this area that a script removed earlier: it comes back, here
                    scriptNpc.moved[id] = {x, y};
                else
                    scriptNpc.spawned.push_back({id, {x, y}});
                scriptNpc.pending.push_back({ScriptNpcState::Op::Spawn, id, {x, y}});
                return true;
            };
            sapi.moveNpc = [&, npcHere](const std::string &id, int x, int y)
            {
                if (!npcHere(id))
                    return false;
                bool wasSpawned = false;
                for (auto &s : scriptNpc.spawned)
                    if (s.first == id)
                    {
                        s.second = {x, y};
                        wasSpawned = true;
                    }
                if (!wasSpawned)
                    scriptNpc.moved[id] = {x, y};
                scriptNpc.pending.push_back({ScriptNpcState::Op::Move, id, {x, y}});
                return true;
            };
            // npc.walkTo / face / play / stop / release / info: the changes are queued with the others and made between ticks; the answer reads the live actor
            auto liveNpc = [&](const gp::Id &id) -> const gp::NpcActor *
            {
                for (const gp::NpcActor &n : npcs)
                    if (n.def->id == id)
                        return &n;
                return nullptr;
            };
            sapi.npcWalkTo = [&, npcHere](const std::string &id, int x, int y) { if (!npcHere(id)) return false; scriptNpc.pending.push_back({ScriptNpcState::Op::Walk, id, {x, y}, ""}); return true; };
            sapi.npcFace = [&, npcHere](const std::string &id, int f) { if (!npcHere(id)) return false; scriptNpc.pending.push_back({ScriptNpcState::Op::Face, id, {f, 0}, ""}); return true; };
            sapi.npcStop = [&, npcHere](const std::string &id) { if (!npcHere(id)) return false; scriptNpc.pending.push_back({ScriptNpcState::Op::Stop, id, {0, 0}, ""}); return true; };
            sapi.npcRelease = [&, npcHere](const std::string &id) { if (!npcHere(id)) return false; scriptNpc.pending.push_back({ScriptNpcState::Op::Release, id, {0, 0}, ""}); return true; };
            sapi.npcPlay = [&, npcHere](const std::string &id, const std::string &anim)
            {
                if (!npcHere(id) || (!anim.empty() && !db.anim(anim)))
                    return false;
                scriptNpc.pending.push_back({ScriptNpcState::Op::Play, id, {0, 0}, anim});
                return true;
            };
            sapi.npcInfo = [&, liveNpc](const std::string &id, int &x, int &y, int &f, bool &walking, bool &scripted)
            {
                const gp::NpcActor *n = liveNpc(id);
                if (!n)
                    return false;
                x = n->x();
                y = n->y();
                f = n->body.facing;
                walking = n->walking();
                scripted = n->scripted;
                return true;
            };
            sapi.removeNpc = [&, npcHere](const std::string &id)
            {
                if (!npcHere(id))
                    return false;
                const auto before = scriptNpc.spawned.size();
                scriptNpc.spawned.erase(std::remove_if(scriptNpc.spawned.begin(), scriptNpc.spawned.end(), [&](const auto &s)
                                                       { return s.first == id; }),
                                        scriptNpc.spawned.end());
                if (scriptNpc.spawned.size() == before) // one of the area's own
                {
                    scriptNpc.removed.insert(id);
                    scriptNpc.moved.erase(id);
                }
                scriptNpc.pending.push_back({ScriptNpcState::Op::Remove, id, {0, 0}});
                return true;
            };
        }

        saveGame = [&]() { return (*areaFlowCtl).saveGameRun(); };
        {
            AreaFlowEnv env{H, S, W, animRefById, bar, barBusy, barHost, barInHouse, barkStep, beginRoom, blockedExit, buildFiltered, colorStep, cur, db, dialogue, enc, encNpc, enterAreaEvents, enterShopStock, exitsArmed, fade, goHome, holdTicks, im, itemName, itemSlot, loadNpcs, npcs, ownedCount, pending, pendingExit, playAnim, playBarAnim, playerSpecies, pos, potionStep, prevPos, questionStep, quests, refreshBits, ren, rep, requestScreen, room, saveGame, savePath, scriptLock, scripts, sd, sfx, showCollision, showNpc, sp, speciesDef, statsOpen, tex, trickStep, tutStep, world};
            areaFlowCtl = std::make_unique<AreaFlow>(std::move(env));
        }

        // Game.hooks().onStatChange: every change to money, a personality stat or a care stat passes through the script first
        player.deltaFilter = [&](int kind, int stat, int delta)
        {
            if (delta == 0)
                return delta;
            const int value = kind == 2 ? player.money : kind == 1 ? player.personality[size_t(stat)] : player.care[size_t(stat)];
            static const char *careNames[] = {"hunger", "health", "", "happiness", "tiredness", "cleanliness"}; // CareStat order; 2 has no name
            static const char *personalityNames[] = {"halos", "pitchforks", "stars", "lightbulbs"};
            const std::string name = kind == 0 && stat >= 0 && stat < 6 ? careNames[stat] : kind == 1 && stat >= 0 && stat < 4 ? personalityNames[stat] : "";
            return scripts.fireStatChange(kind, name, delta, value);
        };

        // ---- engine values: what a script reaches with engine.get / engine.set / engine.list ----
        // One line each. A value that is a plain variable is exposed by address; a computed one takes a getter (and a setter, unless it
        // is read-only). Anything exposed here can be set by a mod; it is the mod's job to set it again after a load (Game onLoad).
        {
            gp::EngineRegistry &E = engineValues;
            E.expose("pet.x", &pos.x, "the pet's feet, in pixels");
            E.expose("pet.y", &pos.y, "the pet's feet, in pixels");
            E.expose("pet.facing", &S.pet.facing, "0 up, 1 down, 2 sideways");
            E.expose("pet.faceLeft", &S.pet.faceLeft);
            E.expose("pet.speedMul", &S.pet.speedMul, "steps per tick (1 normal); the debug sprint multiplies it by 4");
            E.expose("pet.name", &player.name);
            E.expose("pet.appearance", &player.appearance, "the colour that is drawn");
            E.expose("pet.baseColour", &player.baseColour, "the colour a wash goes back to");
            E.expose("pet.animId", &S.pet.animId, "the animation that plays (the game changes it again as the pet acts)");
            E.expose("player.money", &player.money);
            E.expose("player.illness", &player.illnessId, "-1 healthy");
            E.expose("quests.counter", &quests.counter, "quests completed + 1; NPCs refresh at the next area change");
            E.expose("game.ticks", &S.world.gameTicks, "60 a second, counted from the start of the run");
            E.expose("game.debug", &gp::debugMode(), "debug keys (Shift sprints, ...)");
            E.exposeFn("area.id", "string", [&] { return gp::EngineValue::ofStr(cur->def->id); });
            E.exposeFn("area.legacyId", "int", [&] { return gp::EngineValue::ofInt(cur->def->legacyId); });
            E.exposeFn("area.width", "int", [&] { return gp::EngineValue::ofInt(cur->def->widthPx()); }, {}, "pixels");
            E.exposeFn("area.height", "int", [&] { return gp::EngineValue::ofInt(cur->def->heightPx()); }, {}, "pixels");
            E.exposeFn("npcs.count", "int", [&] { return gp::EngineValue::ofInt(int(npcs.size())); });
            // gameplay numbers (engine/tuning.hpp): the base game's values until a mod sets them
            gp::Tuning &T = gp::tuning();
            E.expose("tuning.tickHz", &T.tickHz, "simulation steps a second (30)");
            E.expose("tuning.camY", &T.camY, "the camera sits this far above the pet (145)");
            E.expose("tuning.probeDY", &T.probeDY, "collision is tested this far above the feet (-4)");
            E.expose("tuning.digReach", &T.digReach, "px around the pet a dig reaches (30)");
            E.expose("tuning.holeRingSize", &T.holeRingSize, "holes that stay open (5)");
            E.expose("tuning.randomSpawnBatch", &T.randomSpawnBatch, "random finds placed on entering an area (6)");
            E.expose("tuning.spawnRetries", &T.spawnRetries, "tries to find a free spot for a random find (200)");
            E.expose("tuning.findDudPercent", &T.findDudPercent, "chance in 100 that a random find is a dud (15)");
            E.expose("tuning.maxMoney", &T.maxMoney, "99999");
            E.expose("tuning.maxCare", &T.maxCare, "the cap of each care stat (100)");
            E.expose("tuning.maxPersonality", &T.maxPersonality, "the cap of each personality stat (9999)");
            E.expose("tuning.negativeCareMinQuests", &T.negativeCareMinQuests, "care stats ignore drops until this many quests are done (4)");
            E.expose("tuning.sellFactor", &T.sellFactor, "what the seller pays, of the price (0.8)");
            E.expose("tuning.washHygiene", &T.washHygiene, "Cleanliness a wash adds (100)");
            E.expose("tuning.washMoodLimit", &T.washMoodLimit, "a wash cheers the pet up if Happiness is at most this (0x4a)");
            E.expose("tuning.washMoodBonus", &T.washMoodBonus, "(10)");
            E.expose("tuning.stinkHygieneDrop", &T.stinkHygieneDrop, "what catching a smell costs a clean pet (0x50)");
            E.expose("tuning.stinkCountNeeded", &T.stinkCountNeeded, "checks beside a smelly NPC before it passes on (45)");
            E.expose("tuning.stinkCheckTicks", &T.stinkCheckTicks, "steps between those checks (23)");
            E.expose("tuning.idleMinTicks", &T.idleMinTicks, "NPC idle time, steps (30)");
            E.expose("tuning.idleMaxTicks", &T.idleMaxTicks, "exclusive (91)");
            E.expose("tuning.approachDX", &T.approachDX, "befriending: distance the pet stands from the NPC (75)");
            E.expose("tuning.closerDX", &T.closerDX, "... steps closer for a friend (20)");
            E.expose("tuning.awayDX", &T.awayDX, "... walks away from one that is not (30)");
            E.expose("tuning.giveUpTicks", &T.giveUpTicks, "how long the pet tries to get there (120)");
            E.expose("tuning.decayAfterQuests", &T.decayAfterQuests, "hunger and sleepiness wear down once more than this many quests are done (3)");
            E.expose("tuning.tiredDecayRomTicks", &T.tiredDecayRomTicks, "between drops of Sleepiness, 59.94 Hz ticks (20000)");
            E.expose("tuning.hungerDecayRomTicks", &T.hungerDecayRomTicks, "between drops of Hunger, 59.94 Hz ticks (4000)");
            E.expose("tuning.nudgeQuests", &T.nudgeQuests, "the reminder shows while exactly this many quests are done (1)");
            E.expose("tuning.nudgeNpc", &T.nudgeNpc, "the base NPC record number it sends the pet to (0x42, Mayor Peter)");
            E.expose("tuning.nudgeFirstSeconds", &T.nudgeFirstSeconds, "quest reminder, first (30)");
            E.expose("tuning.shopRestockRomTicks", &T.shopRestockRomTicks, "ROM ticks (59.94 Hz) between shop restocks, 0 = never (3000, about 50 s)");
            E.expose("tuning.nudgeRepeatSeconds", &T.nudgeRepeatSeconds, "quest reminder, repeat (180)");
        }

        gp::QuestHooks &qh = quests.h;
        qh.rnd = rnd;
        std::unique_ptr<QuestBindings> questBindingsCtl; // built below, once everything it takes is declared (walk_quest_bindings.hpp)
        qh.text = [&](int id) { return (*questBindingsCtl).qh_textRun(id); };
        qh.held = [&](const gp::Id &id) { return (*questBindingsCtl).qh_heldRun(id); };

        qh.remove = [&](const gp::Id &id, int n) { return (*questBindingsCtl).qh_removeRun(id, n); };

        qh.spawn = [&](int area, const gp::Id &id, int st, bool persist) { return (*questBindingsCtl).qh_spawnRun(area, id, st, persist); };

        qh.place = [&](const gp::Id &id, int x, int y, bool flag) { return (*questBindingsCtl).qh_placeRun(id, x, y, flag); };
        qh.randomEvent = [&]() { return (*questBindingsCtl).qh_randomEventRun(); };
        qh.clearFlagged = [&]() { return (*questBindingsCtl).qh_clearFlaggedRun(); };
        qh.sfx = sfx;
        qh.hold = [&](int ticks, std::function<void()> done) { return (*questBindingsCtl).qh_holdRun(ticks, done); };
        qh.camX = [&]() { return (*questBindingsCtl).qh_camXRun(); };
        qh.camY = [&]() { return (*questBindingsCtl).qh_camYRun(); };
        qh.receiverY = [&]() { return (*questBindingsCtl).qh_receiverYRun(); };
        qh.nowTick = [&]() { return (*questBindingsCtl).qh_nowTickRun(); };
        qh.portrait = [&](gp::DialogueBox &b, int p) { return (*questBindingsCtl).qh_portraitRun(b, p); };
        auto questInfo = [&]() -> decltype(auto) { return (*questBindingsCtl).questInfo(); };
        qh.onDecline = [&]() { return (*questBindingsCtl).qh_onDeclineRun(); };
        qh.onAccept = [&]() { return (*questBindingsCtl).qh_onAcceptRun(); };
        qh.storyBlast = [&](int code, std::function<void()> done) { return (*questBindingsCtl).qh_storyBlastRun(code, done); };
        qh.onNextQuest = [&]() { return (*questBindingsCtl).qh_onNextQuestRun(); };
        qh.shapeQuest = [&](gp::QuestDef &d, int counter, bool scripted, const std::function<void()> &roll) { return (*questBindingsCtl).qh_shapeQuestRun(d, counter, scripted, roll); };
        qh.textSystem = [&](const char *kind, std::string &text) { return (*questBindingsCtl).qh_textSystemRun(kind, text); };
        qh.rewardSystem = [&](int &code) { return (*questBindingsCtl).qh_rewardSystemRun(code); };
        qh.onComplete = [&]() { return (*questBindingsCtl).qh_onCompleteRun(); };

        walk::bindScripts(svc); // world.drop, rand.* (and later player.* / flags): walk_script_bindings.hpp
        svc.petFellIll = petFellIll;
        { // player.pos / lock / react, area.go, camera.shake: the walking world lives here, so the callbacks do too
            gp::ScriptApi &wapi = scripts.api();
            wapi.getPetPos = [&](int &x, int &y)
            {
                if (room)
                    return false;
                x = int(pos.x);
                y = int(pos.y);
                return true;
            };
            wapi.tutorialsOn = [&] { return settings.tutorials; };
            wapi.think = [&](const std::string &kind)
            { return scriptThink && scriptThink(kind); };
            wapi.setPetPos = [&](int x, int y)
            {
                if (room || pendingExit)
                    return false;
                pos = {std::clamp(x, 0, cur->def->widthPx()), std::clamp(y, 0, cur->def->heightPx())};
                prevPos = pos; // no gliding across the map
                return true;
            };
            wapi.lockPet = [&](int what) { scriptLock |= what; };
            wapi.unlockPet = [&] { scriptLock = 0; };
            wapi.petReact = [&](int kind)
            {
                if (room || pendingExit || host.pet.busy())
                    return false;
                static const gp::ReactAnim as[4] = {gp::ReactAnim::Positive, gp::ReactAnim::Emote, gp::ReactAnim::Shocked, gp::ReactAnim::Negative};
                return host.pet.play(as[std::clamp(kind, 0, 3)], 1);
            };
            wapi.petAnimNumber = [&](const std::string &what) -> int
            {
                const gp::SpeciesArt &art = gp::artOf(sp);
                if (what.find(':') != std::string::npos) // an anim id: a mod's, or the game's "base:anim/34"
                {
                    int n = -1;
                    if (std::sscanf(what.c_str(), "base:anim/%d", &n) == 1 && n >= 0 && n <= 0x2ff && what.find('@') == std::string::npos)
                        return n;
                    return db.hasAnim(what) ? gp::modAnimNumber(what) : -1;
                }
                const auto &roles = gp::speciesRoles();
                for (const auto &r : roles)
                    if (what == r.first)
                        return art.*(r.second);
                for (int k = 0; k < 3; ++k) // the three tricks: "trick1" .. "trick3" (the main animation of each level)
                    if (what == "trick" + std::to_string(k + 1))
                        return art.trickMain[k];
                return -1;
            };
            wapi.petPlay = [&](int anim, bool loop, int rate, const std::vector<int> &next)
            {
                if (room || pendingExit || host.pet.busy())
                    return false;
                std::vector<AnimStep> rest;
                for (size_t i = 0; i < next.size(); ++i)
                    rest.push_back({next[i], i + 1 == next.size() && loop, rate});
                playAnim(anim, loop && next.empty(), rate, Pose::Special, std::move(rest));
                return true;
            };
            wapi.petStopAnim = [&] { playAnim(restIdleId(), true, restIdleRate(), Pose::Idle); };
            wapi.petPlaying = [&] { return pose == Pose::Special && !(animDone && queue.empty()); };
            wapi.travel = [&](const std::string &area, int x, int y)
            {
                if (!db.area(area) || pendingExit || fade.a > 0.0 || room)
                    return false;
                gp::ExitDef e;
                e.id = "script:go";
                e.fromArea = cur->def->id;
                e.toArea = area;
                e.consumeAndSpawn = true;
                e.hasSpawnOverride = true;
                e.spawnOverride = {x, y};
                requestGoTo(e);
                return pendingExit.has_value();
            };
            wapi.currentArea = [&] { return cur->def->id; };
            wapi.walkable = [&](int x, int y) { return !room && x >= 0 && y >= 0 && x < cur->def->widthPx() && y < cur->def->heightPx() && world->tileWalkable(x, y) && world->inRoom(x, y) && !world->objectBlocks(x, y); };
            wapi.areaSize = [&](int &w, int &h) { if (room) return false; w = cur->def->widthPx(); h = cur->def->heightPx(); return true; };
            wapi.cameraShake = [&] { cameraShake.start(); };
        }
        { // shop.*: the stock lives here, so the callbacks do too
            auto shopOf = [&](const std::string &name) -> const gp::ShopDef *
            { const gp::ShopDef *s = db.shop(name); return s ? s : db.shopForArea(name); };
            gp::ScriptApi &sapi = scripts.api();
            sapi.shopStock = [&](const std::string &name, std::vector<gp::ScriptApi::ShopLine> &out)
            {
                const gp::ShopDef *s = shopOf(name);
                if (!s)
                    return false;
                if (const gp::ShopStock::Row *row = shopStock.rowOf(s->area))
                    for (const gp::StockEntry &e : *row)
                        if (e.present())
                            out.push_back({e.item, e.stock});
                return true;
            };
            sapi.shopAdd = [&](const std::string &name, const std::string &item, int n)
            {
                const gp::ShopDef *s = shopOf(name);
                if (!s || !db.item(item))
                    return 0;
                for (int i = 0; i < n; ++i)
                    shopStock.claim(s->area, item);
                return n;
            };
            sapi.shopRemove = [&](const std::string &name, const std::string &item)
            {
                const gp::ShopDef *s = shopOf(name);
                return s && shopStock.removeItem(s->area, item);
            };
            sapi.shopRestock = [&](const std::string &name)
            {
                if (name.empty())
                {
                    shopStock.restock(db.shops(), shopRnd);
                    return !db.shops().empty();
                }
                const gp::ShopDef *s = shopOf(name);
                if (!s)
                    return false;
                shopStock.rollOne(*s, shopRnd);
                return true;
            };
        }

        gp::QuestTextEnv &txt = quests.txt; // adjust to the real member name

        quests.txt.rnd = rnd;
        quests.txt.typeLines = [&](int mode, int kind) { return (*questBindingsCtl).quests_txt_typeLinesRun(mode, kind); };
        quests.txt.str = [&](const std::string &k) { return (*questBindingsCtl).quests_txt_strRun(k); };
        quests.txt.npcName = [&](int p) { return (*questBindingsCtl).quests_txt_npcNameRun(p); };
        quests.txt.npcTier = [&](int p) { return (*questBindingsCtl).quests_txt_npcTierRun(p); };
        quests.txt.areaName = [&](int area) { return (*questBindingsCtl).quests_txt_areaNameRun(area); };
        {
            QuestBindingsEnv env{applyStock, blasts, cameraShake, cur, db, events, gameTicks, gateBias, holdDone, holdNpc, holdTicks, itemName, loadNpcs, modPoolNpcs, nearestNpc, npcAt, npcByIndex, npcs, nudge, ownedCount, pos, qh, quests, refreshBits, rnd, saveGame, scripts, sfx};
            questBindingsCtl = std::make_unique<QuestBindings>(std::move(env));
        }
        txt.itemName = [&](const gp::Id &id, int n) -> std::string
        {
            const gp::ItemDef *d = db.item(id);
            if (!d)
                return id; // or "" if you prefer
            const std::string nm = db.textOr(d->name, "?");
            if (n < 2)
                return nm;
            if (!d->plural.empty())
                return db.textOr(d->plural, nm);
            return (!nm.empty() && nm.back() == 's') ? nm : nm + "s"; // suffix string at 0xd135 assumed to be "s"
        };

        quests.load(player.questsCompleted);
        loadNpcs();

        auto drawQuestHud = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawQuestHud(std::forward<decltype(a)>(a)...); };
        auto drawScriptObjectives = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawScriptObjectives(std::forward<decltype(a)>(a)...); };
        enterAreaEvents(*cur->def, true);
        refreshBits(); // add
        scripts.fireGameLoad(); // a mod's chance to set its engine values again from what it saved
        scripts.fireAreaEnter(cur->def->id, ""); // the game starts here: ctx.from.id is nil
        long gameTickN = 0;         // steps since the game started, for the game-wide tick hooks
        gp::SceneInput lastKeys;    // the keys of the latest step
        bool &running = S.flow.running;
        bool &backToMenu = S.flow.backToMenu;
        const auto tickHz = [] { return std::max(1.0, gp::tuning().tickHz); }; // verified: the game ticks at 30 Hz (tuning.tickHz, a mod can change it)
        const Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 prev = SDL_GetPerformanceCounter();
        double &acc = S.flow.acc;
        // Fade in from black (the front end fades out before handing over). The clock starts after the first
        // presented frame, so texture-upload hitches in that frame don't eat the fade.

        Uint64 fadeLast = SDL_GetPerformanceCounter();

        // What pressing Z on an object does (well, tent, signs, minigames, the onInteract script seam): walk_interactions.hpp.
        // Needs `minigames`, so it is built here rather than next to `svc`.
        walk::Interactions interactions(svc, minigames);

        auto drawScriptCmds = [&](auto &&...a) -> decltype(auto) { return (*worldDrawCtl).drawScriptCmds(std::forward<decltype(a)>(a)...); };
        {
            WorldDrawEnv env{animRefById, db, gameTicks, itemDefs, kEvPal, kFlowerSprite, kHoleDY, kHoleSprite, kShadowAlpha, npcAnim, npcPosF, numbers, objShadow, playerSpecies, quests, reactAnimId, ren, rom, sfx, signText, sprites, stinkTicks, swiftTitle, text, uiIdP};
            worldDrawCtl = std::make_unique<WorldDraw>(std::move(env));
        }

        std::unique_ptr<FrameDraw> frameDrawCtl; // built below, once everything it takes is declared (walk_frame_draw.hpp)
        auto drawFrame = [&]() -> decltype(auto) { return (*frameDrawCtl).drawFrame(); };
        {
            FrameDrawEnv env{H, W, acc, animFrame, animId, animRefById, bar, barAnim, barAnimRef, barBusy, barGfx, barHost, barInHouse, bgTex, blasts, cur, db, dialogue, digAnimDY, digStep, drawDialogue, drawEvent, drawGroundSprite, drawItemShadow, drawNpc, drawNpcShadow, drawObject, drawQuestHud, drawRoom, drawScriptCmds, drawScriptObjectives, drawShadow, drawShadowSprite, drawUiF, emptyText, enc, events, exclaimTicks, faceLeft, facing, fade, fadeLast, feedStep, flipMode, freq, gameTicks, getAnim, holeFills, im, itemDefs, itemGfx, itemName, itemRows, itemSlot, itemWindow, kEvPal, kFrameRate, kHoleFillFps, kHoleSprite, kWalkAnim, menuCmds, minigames, nearestNpc, nearestShopItem, npcAnim, npcAt, npcPosF, npcs, numbers, offX, offY, overId, overTicks, ownedCount, panelAnim, panelClosing, pending, pendingExit, petPal, playAnim, playerSpecies, pos, pose, prevPos, questionBubble, questionStep, questionTicks, quests, reactAnimId, ren, renderAlpha, rom, room, sceneActive, scripts, sfx, shadowFor, shakeDy, showNpc, smoothAt, sp, statNotice, statsBgTex, statsOpen, stinkTicks, tabAnimId, tex, text, thoughtIcon, thoughtTicks, tickHz, trickLay, uiIdP, walkTicks};
            frameDrawCtl = std::make_unique<FrameDraw>(std::move(env));
        }

        std::unique_ptr<WorldStep> worldStepCtl; // built below, once everything it takes is declared (walk_world_step.hpp)
        auto stepWorld = [&]() -> decltype(auto) { return (*worldStepCtl).stepWorld(); };
        {
            WorldStepEnv env{W, afterPickup, animDone, animFrame, animId, animLoop, animRate, animRefById, animReverse, animTicks, bar, barAnim, barAnimRef, barBusy, barHost, barkStep, blockedExit, buyFromShop, cameraShake, closeTrickPanel, colorStep, cur, db, dialogue, digStep, enc, encNpc, events, exitsArmed, faceLeft, facing, fade, feedStep, gameTickN, gameTicks, getAnim, giveMode, holdDone, holdNpc, holdTicks, host, idleThreshold, idleTicks, im, interactions, isRestIdle, itemLeave, itemMenuTick, itemName, kBarRate, kMaxNpcs, kSettleTicks, kWalkAnim, lastKeys, lastSaveTick, medStuck, medStuckLimit, medStuckTicks, minigames, nearestNpc, nearestShopItem, npcMute, npcPrev, npcRng, npcs, nudge, overId, overTicks, ownedCount, panelAnim, panelClosing, pendingBarState, pendingExit, pendingScreen, pickTrick, playAnim, pos, pose, potionStep, pressCancelInWorld, prevDirs, prevPos, prevX, prevZ, questionStep, quests, queue, reactAnimId, refreshBits, requestGoTo, requestScreen, restIdleId, restIdleRate, rom, room, roomTutorial, runBarState, saveGame, sceneActive, scriptLock, scriptNpc, scripts, sellMode, settings, settleTicks, sfx, shakeDy, shopRestockAccum, shopRnd, shopStock, showDone, showLevel, showNpc, showStarted, showcase, sp, speedMul, startDig, startWorldTutorial, statNotice, statsOpen, stinkXfer, svc, swiftDropStep, tickBark, tickBlasts, tickColorMachine, tickDig, tickFeed, tickHoleFills, tickPotion, tickQuestion, tickRoom, tickStatNotice, tickSwiftDrop, tickThoughtBubble, tickTrick, tickWash, tickWorldTutorial, trickStep, tryPickup, tutNudge, tutStep, updateNpcAnim, useSelectedItem, walkTicks, washStep, world};
            worldStepCtl = std::make_unique<WorldStep>(std::move(env));
        }

        while (running)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                if (e.type == SDL_QUIT)
                {
                    running = false;
                }
                else if (e.type == SDL_KEYDOWN && !e.key.repeat)
                {
                    SDL_Keycode dk = gp::keys().canonical(e.key.keysym.scancode, e.key.keysym.sym); // a rebound key acts as its action's default
                    if (!gp::debugMode() && dk != SDLK_RETURN && dk != SDLK_TAB)
                        dk = SDLK_UNKNOWN; // the developer keys need debug mode (typed on the main menu)
                    switch (dk)
                    {
                    case SDLK_1:
                        anchorMode = 0;
                        break; // origin + anchor, per frame (current)
                    case SDLK_2:
                        anchorMode = 1;
                        break; // origin only
                    case SDLK_3:
                        anchorMode = 2;
                        break; // frame 0's origin + anchor for every frame
                    case SDLK_4:
                        anchorMode = 3;
                        break; // bottom-centre of each frame
                    case SDLK_5:
                        anchorMode = 4;
                        break; // bottom-centre of each frame
                    case SDLK_j:
                        --offX;
                        break;
                    case SDLK_l:
                        ++offX;
                        break;
                    case SDLK_i:
                        --offY;
                        break;
                    case SDLK_k:
                        ++offY;
                        break;
                    case SDLK_f:
                        flipMode ^= 1;
                        break;
                    case SDLK_t:
                        showStats = !showStats;
                        break;
                    case SDLK_o:
                        std::printf("cam %d,%d  pos %d,%d  (room %dx%d  top=%d bottom=%d)\n",
                                    clampCam(pos.x - W / 2, cur->img.w, W), clampCam(pos.y - gp::tuning().camY, cur->img.h, H),
                                    pos.x, pos.y, cur->room.width, cur->room.height,
                                    cur->room.top, cur->room.bottom);
                        break;
                    // ---- one-shot actions (rates for 29, 47, 49 are placeholders; 45 is known) ----
                    case SDLK_e:
                        playAnim(29, false, 14, Pose::Special); // eat
                        break;
                    case SDLK_b:
                        playAnim(gp::artOf(sp).callFront, false, 14, Pose::Special); // bark
                        break;
                    case SDLK_n:
                        player.illnessId = (player.illnessId + 1) % 11; // line 1460, the N key
                        std::printf("illness %d\n", player.illnessId);
                        break;
                    case SDLK_s:
                        playAnim(46, false, 14, Pose::Special, {{gp::artOf(sp).settled, true, gp::artOf(sp).settledRate}}); // action 7 in the game: 46 once, then sit idle 45 at rate 7
                        break;
                    case SDLK_p:
                    {
                        const bool walking = pose == Pose::Move || pose == Pose::Wait;
                        const int dumpId = walking ? kWalkAnim[facing] : animId;
                        const AnimRef D = getAnim(dumpId);
                        if (D.anim)
                            for (size_t n = 0; n < D.anim->frames.size(); ++n)
                            {
                                const gp::SpriteFrame &q = D.anim->frames[n];
                            }
                        break;
                    }
                    case SDLK_h:
                        player.adjustPersonality(gp::Halos, 10);
                        std::printf("halos %d pitchforks %d\n", player.personality[gp::Halos], player.personality[gp::Pitchforks]);
                        break;
                    case SDLK_u:
                        player.adjustPersonality(gp::Pitchforks, 10);
                        std::printf("halos %d pitchforks %d\n", player.personality[gp::Halos], player.personality[gp::Pitchforks]);
                        break;
                    case SDLK_r:
                        player.personality = {};
                        std::printf("stats reset\n");
                        break;
                    case SDLK_F5:
                        smoothCam = !smoothCam;
                        std::printf("smooth camera %s\n", smoothCam ? "on" : "off");
                        break;
                    case SDLK_F10: // back to the front-end menu
                        backToMenu = true;
                        running = false;
                        break;
                    case SDLK_RETURN:
                        if (barBusy == 4)
                            closeTrickPanel(); // event 6 closes the panel
                        else if (barBusy != 5)
                            openBar(false);
                        break;
                    case SDLK_TAB:
                        if (barBusy != 4 && barBusy != 5)
                            openBar(true);
                        break;
                    case SDLK_LEFTBRACKET:
                        statsBase -= 0x40;
                        // rebuildStatsBg();
                        break;
                    case SDLK_RIGHTBRACKET:
                        statsBase += 0x40;
                        // rebuildStatsBg();
                        break;
                    case SDLK_MINUS:
                        statsBase -= 0x400;
                        // rebuildStatsBg();
                        break;
                    case SDLK_EQUALS:
                        statsBase += 0x400;
                        // rebuildStatsBg();
                        break;
                    case SDLK_COMMA:
                        // statsFine -= 1;
                        //  rebuildStatsBg();
                        break;
                    case SDLK_PERIOD:
                        // statsFine += 1;
                        //   rebuildStatsBg();
                        break;
                    case SDLK_SEMICOLON:
                        // statsFine -= 16;
                        //  rebuildStatsBg();
                        break;
                    case SDLK_QUOTE:
                        // statsFine += 16;
                        //   rebuildStatsBg();
                        break;
                    default:
                        break;
                    }
                    // std::printf("mode %d, offset (%d,%d), flip formula %d\n", anchorMode, offX, offY, flipMode);
                }

            const Uint64 now = SDL_GetPerformanceCounter();
            acc += double(now - prev) / double(freq);
            prev = now;
            if (acc > 0.25)
                acc = 0.25;

            if (pendingExit && fade.a >= 1.0)
            {
                const gp::ExitDef e = *pendingExit;
                pendingExit.reset();
                if (!goTo(e))
                    exitsArmed = false;
                fade.go(0.0, 8.0 / 30); // fade in
            }
            if (pendingScreen && fade.a >= 1.0)
            {
                auto sw = std::move(pendingScreen);
                pendingScreen = nullptr;
                sw();
                fade.go(0.0, 8.0 / 30);
            }

            while (acc >= 1.0 / tickHz())
            {
                acc -= 1.0 / tickHz();
                stepWorld();
            }

            drawFrame();
        }
        saveGame();
        // the renderer outlives this run now, so free what we made
        SDL_DestroyTexture(tex);
        SDL_DestroyTexture(bgTex);
        SDL_DestroyTexture(statsBgTex);
        SDL_DestroyTexture(swiftBgTex);
        // anim and object textures are freed by ~SpriteCache
        return backToMenu ? gp::kRunToMenu : gp::kRunQuit;
    }
    catch (const std::exception &e)
    {
        std::printf("error: %s\n", e.what());
        return gp::kRunError;
    }
    return gp::kRunQuit;
}

#ifndef GP_NO_MAIN
// Standalone mode, same command line as before: walk <rom> <area> [--collision] [--slot N]
int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3)
    {
        std::printf("usage: walk <rom> <area> [--collision] [--slot N] [--smooth]\n");
        return 1;
    }
    bool showCollision = false;
    int slot = 0;
    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--collision") == 0)
            showCollision = true;
        else if (std::strcmp(argv[i], "--smooth") == 0)
            smoothCam = true;
        else if (std::strcmp(argv[i], "--slot") == 0 && i + 1 < argc)
            slot = std::clamp(std::atoi(argv[++i]), 0, 2);
    }
    try
    {
        gp::AppHost host(argv[1], "walk");
        return runWalk(host.ctx, slot, std::atoi(argv[2]), showCollision) == gp::kRunError ? 1 : 0;
    }
    catch (const std::exception &e)
    {
        std::printf("error: %s\n", e.what());
        return 1;
    }
}
#endif