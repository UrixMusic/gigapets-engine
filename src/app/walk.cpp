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
#include <SDL2/SDL.h>
#include "../loaders/import.hpp"
#include "../engine/collision.hpp"
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
#include "../game/menu_bar.hpp"
#include "../game/item_use.hpp" // at the top with the other includes
#include "../game/pending_event.hpp"
#include "../game/fixed_spawn.hpp"
#include "../loaders/fixed_spots.hpp"
#include "../game/quest_flow.hpp"
#include "../game/item.hpp"
#include "../game/progress_bits.hpp"
#include "../game/offerings.hpp"
#include "../game/save.hpp"
#include "../game/save_player.hpp"
#include "../game/swift_pick.hpp"
#include "../mods/mod_loader.hpp"
#include "../game/three_in_a_row.hpp"
#include "../game/memory_match.hpp"
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
    bool tileWalkable(int x, int y) const override { return g_.walkable(x, y + kProbeDY); }
    bool objectBlocks(int x, int y) const override { return o_.blocks(x, y + kProbeDY); }
    bool inRoom(int x, int y) const override { return gp::inRoom(room_, x, y + kProbeDY); }

private:
    const gp::GroundField &g_;
    const gp::AreaRuntime &o_;
    gp::Room room_;
};

struct WalkItemHost : gp::ItemUseHost
{
    void addStatCounter(int counter, int amount) override
    {
        std::printf("stat counter %d += %d (not stored yet)\n", counter, amount);
    }
} itemHost;

static int clampCam(int raw, int world, int view)
{
    return std::min(std::max(raw, 0), world - view); // same order as the game
}

static float clampCamF(float raw, int world, int view)
{
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
int runWalk(gp::AppContext &ctx, int slot, int startArea, bool showCollision)
{
    // globals from the standalone days: reset so a second run (after going back to the menu) starts clean
    showStats = false;
    player = gp::Player{};
    const std::string savePath = gp::slotPath(slot);
    gp::SaveData sd;
    const bool haveSave = gp::loadFromFile(savePath, sd);
    const int playerSpecies = haveSave ? std::clamp(sd.getInt("species", 0), 0, 2) : 0;
    player.money = 500;        // stand-in until Player has money (near `inventory`)
    int &money = player.money; // replaces: int money = 500;
    bool statsOpen = false;

    uint16_t kingFlags = haveSave ? uint16_t(sd.getInt("kingFlags") & 0xfff) : 0;
    uint16_t gardenFlags = haveSave ? uint16_t(sd.getInt("gardenFlags") & 0xfff) : 0;
    std::function<void()> refreshBits, saveGame;
    uint32_t lastSaveTick = 0;
    const int sp = playerSpecies;
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
        for (const auto &w : rep.warnings)
            std::fprintf(stderr, "mod warning: %s\n", w.c_str());
        for (const auto &e : rep.errors)
            std::fprintf(stderr, "mod error: %s\n", e.c_str());
        for (const auto &id : rep.loaded)
            std::printf("loaded mod %s\n", id.c_str());
        if (!rep.ok())
            return 1;
        for (const std::string &id : rep.loaded)
            scripts.loadMod(id, std::filesystem::path("mods") / id);
        for (const auto &e : scripts.errors())
            std::fprintf(stderr, "script error: %s\n", e.c_str());
        if (!scripts.errors().empty())
            return 1;
        // Every hook must point at something that exists and use a real hook name, or it would silently never fire.
        {
            gp::HookTargets targets;
            targets.npcExists = [&](const std::string &id)
            { return db.hasNpc(id); };
            targets.areaExists = [&](const std::string &id)
            { return db.area(id) != nullptr; };
            targets.itemExists = [&](const std::string &id)
            { return db.item(id) != nullptr; };
            const std::vector<std::string> hookProblems = gp::validateHooks(scripts, targets);
            for (const std::string &p : hookProblems)
                std::fprintf(stderr, "script error: %s\n", p.c_str());
            if (!hookProblems.empty())
                return 1;
        }
        // The pet's species and palette come from the db now: the colour picked in the front end, else the species default.
        const gp::SpeciesDef *speciesDef = db.speciesByLegacy(playerSpecies);
        if (!speciesDef)
        {
            std::printf("species %d not found in the content db\n", playerSpecies);
            return gp::kRunError;
        }
        const auto slotInfo = gp::peekSlot(slot);
        const int petPal = (slotInfo.full && slotInfo.appearance >= 0) ? slotInfo.appearance : speciesDef->defaultPalette;
        std::printf("pet palette 0x%x (%s)\n", petPal, (slotInfo.full && slotInfo.appearance >= 0) ? "from save" : "species default");
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
        gp::ShopStock shopStock;
        if (!gp::loadStockDump("stock.bin", shopStock))
        {
            std::printf("stock.bin not found or wrong size, using row 1 test data\n");
            shopStock.initRow1();
        }
        for (int r = 0; r < 22; ++r)
        {
            std::printf("shop row %d:", r);
            for (const gp::StockEntry &e : shopStock.rows[r])
                if (e.w0)
                    std::printf(" %dx%s", e.stock(), db.textOr(itemDefs[e.item()].name, "?").c_str());
            std::printf("\n");
        }
        gp::PendingList pending;
        auto shopRnd = [](int n)
        { return std::rand() % n; };
        auto enterShopStock = [&](const gp::AreaDef &a) { // call when the destination is a shop
            const int row = gp::shopRowForArea(a.legacyId);
            if (row >= 0)
                gp::spawnShopItemStock(pending, shopStock, a.legacyId, row, a.spawnPoints, shopRnd);
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

        for (int r = 0; r < 22; ++r)
            for (const gp::StockEntry &e : shopStock.rows[r])
                if (e.w0 && !gp::isModSprite(itemDefs[e.item()].worldSprite))
                    try
                    {
                        gp::importUiSprite(db, rom, itemDefs[e.item()].worldSprite, gp::kMenuPalette);
                    }
                    catch (const std::exception &)
                    {
                    }

        for (int a : {0x10, 0x11, 0x12, 0x16}) // arrow
            try
            {
                gp::importAnim(rom, db, a, 0x28c);
            }
            catch (const std::exception &)
            {
            }
        for (int s : {0x9c1, 0x9c2}) // flower, dug hole
            try
            {
                gp::importUiSprite(db, rom, s, 0x28c);
            }
            catch (const std::exception &e)
            {
                std::printf("flower/hole sprite 0x%x import failed: %s\n", s, e.what());
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
        const gp::AreaDef *startDef = db.areaByLegacy(id);
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
        if (slot >= 0 && slot < 3)
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
        const int W = 320, H = 240;
        unsigned statsBase = 0xD8DC;
        SDL_Renderer *ren = ctx.ren;                     // window + renderer belong to the AppHost
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest (SDL's default): keeps pixel art crisp at fractional positions
        SDL_RenderSetLogicalSize(ren, W, H);
        const SDL_bool oldIntScale = SDL_RenderGetIntegerScale(ren);
        SDL_RenderSetIntegerScale(ren, smoothCam ? SDL_TRUE : SDL_FALSE); // uniform pixel widths while scrolling smoothly
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
        auto addAll = [&](const int (&t)[3])
        { petIds.push_back(t[sp]); };
        addAll(gp::kWalkUp);
        addAll(gp::kWalkDown);
        addAll(gp::kWalkSide);
        addAll(gp::kWalkSideStyle);
        addAll(gp::kSitDown);
        addAll(gp::kSitAlt);
        addAll(gp::kEat);
        addAll(gp::kIdleBase);
        addAll(gp::kIdleSide);
        addAll(gp::kIdleStyle);
        addAll(gp::kTurn180);
        addAll(gp::kTurn90);
        addAll(gp::kTurn90Style);
        addAll(gp::kTurn180Style);
        addAll(gp::kSettled);
        addAll(gp::kSettledStyle);
        addAll(gp::kIdleFlagA);
        addAll(gp::kIdleFlagAStyle);
        addAll(gp::kIdleFlagB);
        addAll(gp::kIdleFlagBStyle);
        addAll(gp::kCallFront);
        addAll(gp::kCallSide);
        addAll(gp::kCallStyle);
        petIds.push_back(0x1c);
        petIds.push_back(0x80);
        petIds.push_back(0xe9);
        for (int id : gp::reactIdsForTier(sp))
            petIds.push_back(id);

        std::sort(petIds.begin(), petIds.end());
        petIds.erase(std::unique(petIds.begin(), petIds.end()), petIds.end());
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

        auto petAnimId = [&](int n) -> gp::Id
        {
            char b[48];
            std::snprintf(b, sizeof b, "base:anim/%d@%x", n, petPal);
            gp::Id withPal = b;
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
        const int kWalkAnim[3] = {gp::kWalkUp[sp], gp::kWalkDown[sp], gp::kWalkSide[sp]}; // 0 = up, 1 = down, 2 = sideways

        // ---- objects: drawn as sprites (not baked into the background) so they can sort against NPCs and the pet ----
        auto drawObject = [&](const gp::ObjectDef &o, float camX, float camY)
        { sprites.drawObject(o, camX, camY); };

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
        auto loadNpcs = [&]()
        {
            npcs.clear();
            npcPrev.clear();
            const int area = cur->def->legacyId;
            for (const gp::NpcDef *d : db.npcsInArea(area))
            {
                gp::NpcSpawnInfo info;
                info.areaId = area;
                if (!gp::npcSpawnsInArea(d->index, info, area, player.questsCompleted))
                    continue;
                npcs.emplace_back(*d);
            }
            std::printf("%zu NPCs in area %d\n", npcs.size(), area);
        };
        loadNpcs();
        auto animRefById = [&](const gp::Id &id) -> AnimRef
        { return sprites.animRef(id); };

        auto reactAnimId = [](int id, int pal)
        {
            char b[48];
            std::snprintf(b, sizeof b, "base:anim/%d@%x", id, pal);
            return gp::Id(b);
        };

        // NPC animation key: 0 = idle (def->anim), 1 = walk up, 2 = walk down, 3 = walk sideways
        auto npcAnim = [&](const gp::NpcActor &n, int key)
        {
            const gp::Id *w = &n.def->anim;
            if (key >= 1 && key <= 3)
                w = &n.def->walkAnim[key - 1];
            else if (key == 4)
                w = &n.def->turn180Anim;
            else if (key == 5)
                w = &n.def->turn90Anim;
            else if (key == 6)
                return animRefById(n.forcedAnimId);
            return animRefById(w->empty() ? n.def->anim : *w);
        };
        auto updateNpcAnim = [&](gp::NpcActor &n)
        {
            const int key = n.animKey();
            const int frames = int(npcAnim(n, key).size());
            n.setAnim(key, frames, 14, key < 4); // keys 4/5 are one-shots
            n.advanceAnim(frames);
        };
        auto drawNpc = [&](const gp::NpcActor &n, float camX, float camY)
        {
            const AnimRef A = npcAnim(n, std::max(n.anim.animId, 0));
            if (A.size() == 0)
                return;
            const int fr = std::min(n.anim.currentFrame, int(A.size()) - 1);
            const gp::SpriteFrame &s = A.anim->frames[fr];
            const int key = n.animKey();
            const bool flip = key == 3                 ? n.body.facing == gp::FaceLeft
                              : (key == 4 || key == 5) ? n.body.facing == gp::FaceRight
                              : key == 6               ? n.forcedFlip
                                                       : false;
            const auto np = npcPosF(n);
            const float x = flip ? np.first - camX + s.anchorX - s.dx - s.w
                                 : np.first - camX - s.anchorX + s.dx;
            SDL_FRect r{x, np.second - camY - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
            SDL_RenderCopyExF(ren, (*A.tex)[fr], nullptr, &r, 0, nullptr,
                              flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        };

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
        TextDrawer signText(ren, db.font(gp::fontId(1, 0x11f))); // sign text: font 1, palette 0x11f
        TextDrawer text(ren, db.font(gp::fontId(0)));            // dialogue text: font 0
        TextDrawer numbers(ren, db.font(gp::fontId(2)));         // stat numbers: font 2
        TextDrawer badge(ren, db.font(gp::fontId(7)));
        std::unique_ptr<gp::EncounterSequence> &enc = S.world.enc;
        gp::NpcActor *&encNpc = S.world.encNpc;
        auto nearestNpc = [&]() -> gp::NpcActor *
        {
            gp::NpcActor *bestNpc = nullptr;
            long bd = 28L * 28L;
            for (gp::NpcActor &n : npcs)
            {
                const long dx = n.x() - pos.x, dy = n.y() - pos.y, d = dx * dx + dy * dy;
                if (d <= bd)
                {
                    bd = d;
                    bestNpc = &n;
                }
            }
            return bestNpc;
        };
        // The 12 signs the original builds at runtime with SprintF. Tried before the table lookup.

        // Minigame starters now live in the ContentDB as InteractionDefs (importInteractions in import.hpp).

        auto nearestShopItem = [&]() -> const gp::ShopEvent *
        {
            const gp::ShopEvent *b = nullptr;
            long bd = 28L * 28L;
            for (const gp::ShopEvent &e : pending.v)
            {
                if (e.type != 3 || e.area != cur->def->legacyId)
                    continue;
                const long dx = e.x - pos.x, dy = e.y - pos.y, d = dx * dx + dy * dy;
                if (d <= bd)
                {
                    bd = d;
                    b = &e;
                }
            }
            return b;
        };

        // Draws a UI sprite the way objects are drawn: position minus anchor, plus the frame's offset.
        auto drawUi = [&](int spriteId, int x, int y, int pal = gp::kDialoguePalette)
        {
            const gp::Id uid = gp::uiSpriteId(spriteId, pal);
            const AnimRef A = animRefById(uid);
            static std::set<gp::Id> warned; // #include <set>
            if (A.size() == 0)
            {
                if (warned.insert(uid).second)
                    std::printf("ui sprite missing: %s\n", uid.c_str());
                return;
            }
            const gp::SpriteFrame &s = A.anim->frames[0];
            // std::printf("ui %x: %dx%d anchor(%d,%d) d(%d,%d)\n", spriteId, s.w, s.h, s.anchorX, s.anchorY, s.dx, s.dy);
            SDL_Rect r{x + s.dx, y + s.dy, s.w, s.h}; // try this first
            SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
        };

        auto drawUiId = [&](const gp::Id &id, int x, int y)
        {
            const AnimRef A = animRefById(id);
            if (A.size() == 0)
                return;
            const gp::SpriteFrame &s = A.anim->frames[0];
            SDL_Rect r{x - s.anchorX + s.dx, y - s.anchorY + s.dy, s.w, s.h};
            SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
        };
        // The game draws icons at y = rowY + E - 12, where E comes from a call the decompiler calls
        // GetSpriteWidth. E = the frame height is a first guess: adjust by eye.
        auto drawIcon = [&](int spriteId, int x, int rowY)
        {
            const AnimRef A = animRefById(gp::uiSpriteId(spriteId));
            if (A.size() == 0)
                return;
            const gp::SpriteFrame &s = A.anim->frames[0];
            SDL_Rect r{x - s.anchorX + s.dx, rowY - 12 + s.h - s.anchorY + s.dy, s.w, s.h};
            SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
        };

        struct Layout
        {
            int frame, x, y, textX, textY, textW, btnX, btnY;
        };
        static const Layout kLayouts[3] = {
            {0x48, 0x30, 0x30, 0x10, 0x12, 0xbd, 0x74 + 0x2b, 0x57},
            {0x49, 0x30, 0x14, 0x10, 0x12, 0xbd, 0x74 + 0x2b, 0x8c},
            {0x4a, 0x16, 0x14, 0x0c, 0x10, 0xf4, 0xb0 + 0x2b, 0x8c},
        };

        // Layout numbers are from RenderActiveDialogueWindow (stats box: x 0x16, y 0x14, width 0xf4).
        auto drawDialogue = [&](const gp::DialogueBox &b)
        {
            const bool stats = b.kind == gp::BoxKind::Stats || b.kind == gp::BoxKind::StatsLow;
            const Layout &L = kLayouts[stats ? 2 : std::clamp(b.layout, 0, 2)];
            // With a portrait the original passes indent 0x36 over the first 0x28 px of height.
            const int indent = b.portrait.empty() ? 0 : 0x36;
            const int indentH = b.portrait.empty() ? 0 : 0x28;

            if (b.kind == gp::BoxKind::Sign) // <-- the block goes here, inside the lambda
            {
                drawUi(0x17, 0, 0, 0x11f);
                const int h = signText.layout(b.body, 0x1e, 0, 0x105, gp::Align::Center).height;
                signText.draw(b.body, 0x1e, 0x6e - h / 2, 0x105, gp::Align::Center);
                return;
            }

            drawUi(L.frame, L.x, L.y);
            if (!b.portrait.empty())
                drawUiId(b.portrait, L.x + 0x14, L.y + 0x14);

            if (!stats)
            {
                // Message / yes-no boxes: centred (align 1), indented around the portrait if there is one.
                text.draw(b.body, L.x + L.textX, L.y + L.textY, L.textW, gp::Align::Center, indent, indentH);
                if (b.kind == gp::BoxKind::Confirm)
                {
                    const int y = L.y + L.btnY;
                    drawUi(b.yesSelected ? 0x4f : 0x4e, L.x + L.btnX - 0x2b, y); // Yes
                    drawUi(b.yesSelected ? 0x4b : 0x4c, L.x + L.btnX, y);        // No
                }
                else if (b.kind == gp::BoxKind::Quantity)
                {
                    numbers.draw(std::to_string(b.qty), L.x + L.btnX - 0x30, L.y + L.btnY, 0x20, gp::Align::Right);
                    drawUi(0x27, L.x + L.btnX, L.y + L.btnY);
                }
                else
                    drawUi(0x27, L.x + L.btnX, L.y + L.btnY);

                return;
            }

            const int base = b.kind == gp::BoxKind::StatsLow ? 0x8c : 0x3c;
            text.draw(b.title, 0x32, base - 0x19, 0xdc, gp::Align::Center);
            if (b.kind == gp::BoxKind::Stats) // type 5 (unfriended): centred, forced to y 0x78
                text.draw(b.body, L.x + L.textX, 0x78, L.textW, gp::Align::Center);
            else // type 4 (friended): left-aligned at the top, indented around the portrait
                text.draw(b.body, L.x + L.textX, L.y + L.textY, L.textW, gp::Align::Left, indent, indentH);

            struct Cell
            {
                int icon, iconX, numX, rowY;
            };
            const Cell cells[4] = {
                {0xac2, 0x5a, 0x6e, base},        // halos
                {0xace, 0x5a, 0x6e, base + 0x23}, // pitchforks
                {0xad6, 0xb4, 0xc8, base},        // stars
                {0xac6, 0xb4, 0xc8, base + 0x23}, // lightbulbs
            };
            for (int i = 0; i < 4; i++)
            {
                drawIcon(cells[i].icon, cells[i].iconX, cells[i].rowY);
                numbers.draw(std::to_string(b.req[i]), cells[i].numX, cells[i].rowY, 0x1e, gp::Align::Right);
            }
            drawUi(0x27, L.x + L.btnX, L.y + L.btnY);
        };

        BarAnim &barAnim = S.ui.barAnim;
        int &barBusy = S.ui.barBusy; // g_isPetBusy
        bool &barInHouse = S.ui.barInHouse;
        const int kBarRate = 12;
        auto playBarAnim = [&](int id)
        { barAnim = {id, 0, 0, false, true}; };

        struct WalkBarHost : menubar::MenuBarHost
        {
            int &busy;
            bool &inHouse;
            int &slot;
            std::function<void(int)> play;
            bool pressed = false;
            WalkBarHost(int &b, bool &h, int &s, std::function<void(int)> p)
                : busy(b), inHouse(h), slot(s), play(std::move(p)) {}
            std::function<void(int)> onState;
            int category = 0;
            void switchGameState(int s) override
            {
                if (onState)
                    onState(s);
                else
                    std::printf("bar: SwitchGameState(0x%x)\n", s);
            }
            void startPetActivity(int id, int prm, int x, int y) override
            {
                std::printf("bar: StartPetActivity(0x%x, %d) at (%d,%d)\n", id, prm, x, y);
            }
            int rollPetPrizeId(int kind) override
            {
                std::printf("bar: prize roll %d\n", kind);
                return 0;
            }
            void setPetBusy(int v) override { busy = v; }
            void setLoopState(int v) override { std::printf("bar: loopState=%d\n", v); }
            void setItemMenuMode(int) override {}
            void setItemMenuCategory(int c) override { category = c; }
            void startDialogue(int id) override { std::printf("bar: dialogue 0x%x\n", id); }
            void openTricksPanel() override { std::printf("bar: tricks panel (not ported)\n"); }
            int currentSaveSlot() override { return slot; }
            void requestRedraw() override {}
            void startCloseAnim() override { play(inHouse ? 1 : 3); } // CloseMenuBar()
            void playOpenAnim(int) override {}
            void moveCursor(int ev, int &sel, int count) override // still a guess
            {
                sel = (sel + (ev == 1 ? 1 : count - 1)) % count;
            }
        };
        WalkBarHost barHost(barBusy, barInHouse, slot, playBarAnim);
        menubar::MenuBar bar(barHost);

        struct WalkGfx : gp::Gfx
        {
            std::function<void(int, int, int, int)> tile;
            void drawTile(int s, int p, int x, int y, int, int) override { tile(s, p, x, y); }
            void drawTextBox(int, int, int, int, const std::string &, int, int, int) override {}
        } barGfx;
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
        auto ownedCount = [&](int id)
        {
            if (id < 0 || id >= int(itemDefs.size()))
                return 0;
            return player.itemCount(itemDefs[size_t(id)].id);
        };
        auto itemName = [&](int id)
        { return db.textOr(itemDefs[id].name, "?"); };
        const std::string emptyText = "You don't have any."; // real string at 0xb2e4: decode later

        for (int id : {0xf5, 0xf4, 0xf3, 0xf2, 0xf0, 0xef, 0xee, 0, 0xf1, 0xed, 0, 0x105})
            std::printf("swift pick %3d (0x%x): %s\n", id, id,
                        id < int(itemDefs.size()) ? itemName(id).c_str() : "?");

        // ---- pending events: dig spots, hidden quest items, ground items ----
        std::mt19937 digGen(SDL_GetTicks());
        const gp::RandFn rnd = gp::makeRandFn(digGen);
        gp::WorldItems events;
        // Shared services for the free functions in walk_npc_talk.hpp / walk_script_bindings.hpp.
        WalkServices svc{db, scripts, player, dialogue, quests, S, events, refreshBits, saveGame,
                         [&]
                         { return cur->def->legacyId; }};

        gp::FixedSpawnEnv spawnEnv = gp::playerSpawnEnv(player);
        spawnEnv.itemNumber = [&](const gp::Id &id) // db lookup, so mod items work
        {
            const gp::ItemDef *d = db.item(id);
            return d ? d->index : -1;
        };
        auto enterAreaEvents = [&](const gp::AreaDef &a, bool newZone)
        {
            if (gp::shopRowForArea(a.legacyId) >= 0)
                return; // shops use the old PendingList
            if (newZone)
            {
                events.clearNonPersistent();
                if (a.maxEvents > 0)
                    events.spawnRandom(a, gp::kRandomSpawnBatch, rnd);
            }
            gp::spawnFixedSpots(events, a.legacyId, db.fixedSpotsIn(a.legacyId), spawnEnv);
        };

        auto randIn = [&](int lo, int hi)
        { return std::uniform_int_distribution<int>(lo, hi)(digGen); };
        auto sfx = [](int id)
        { std::printf("sfx 0x%x\n", id); }; // no sound system yet

        auto isSpecialItem = [](int i)
        { return (i >= 2 && i <= 17) || (i >= 0x109 && i <= 0x10b); };

        // AdjustPersonalityStat + AddToStatCounter(2 + stat*4, applied delta)
        auto personalityDelta = [&](int stat, int delta)
        {
            const int before = player.personality[stat];
            player.adjustPersonality(gp::PersonalityStat(stat), delta);
            itemHost.addStatCounter(2 + stat * 4, player.personality[stat] - before);
        };

        // GrantItemOrEvent, special-id branches only. Returns false for normal items.
        auto grantSpecial = [&](int id, bool sound) -> bool
        {
            if (!isSpecialItem(id))
                return false;

            if (id == 0x109)
            {
                player.adjustMoney(randIn(5, 11));
                if (sound)
                    sfx(0x14);
            }
            else if (id == 0x10a)
            {
                player.adjustMoney(randIn(20, 51));
                if (sound)
                    sfx(0x14);
            }
            else if (id == 0x10b) // mystery bag
            {
                if (randIn(0, 99) < 60)
                {
                    const int r = randIn(1, 5);
                    if (r == 1)
                    {
                        player.adjustMoney(randIn(30, 61));
                        if (sound)
                            sfx(0x14);
                    }
                    else // r = 2..5 -> stat 0..3
                        personalityDelta(r - 2, randIn(1, 10));
                    if (sound)
                        sfx(0x53);
                }
                else
                {
                    const int r = randIn(1, 8);
                    switch (r)
                    {
                    case 1:
                        player.adjustCare(gp::Tiredness, -80);
                        break;
                    case 2:
                        player.illnessId = randIn(0, 9);
                        break;
                    case 3:
                        player.adjustCare(gp::Cleanliness, -80);
                        break;
                    case 4:
                        player.adjustCare(gp::Happiness, -80);
                        break;
                    default: // r = 5..8 -> stat 0..3
                        personalityDelta(r - 5, -randIn(1, 10));
                        break;
                    }
                    if (sound)
                        sfx(1);
                }
            }
            else // ids 2..17
            {
                const int stat = (id - 2) / 4, k = (id - 2) % 4;
                const int amt = k == 0 ? 1 : k == 1 ? randIn(1, 3)
                                         : k == 2   ? randIn(1, 5)
                                                    : 50;
                personalityDelta(stat, amt);
                if (sound)
                    sfx(0x53);
            }
            return true;
        };

        auto tryPickup = [&](int idx)
        {
            const int area = cur->def->legacyId;
            gp::PickupEnv pe;
            pe.held = [&](int i)
            { return ownedCount(i); };
            pe.capacity = [&](int i)
            { return isSpecialItem(i) ? 1 : (i >= 0 && i < int(itemDefs.size()) ? itemDefs[i].maxQty : 1); };
            pe.grant = [&](int i)
            {
                if (!grantSpecial(i, true))
                {
                    if (i >= 0 && i < int(itemDefs.size()))
                        player.addItem(itemDefs[size_t(i)].id, 1);
                    gp::onItemGranted(events, area, i, db.fixedSpotsIn(area), spawnEnv);
                }
            };
            const int pickedItem = idx >= 0 && idx < events.size() ? events.events()[size_t(idx)].item : -1;
            const gp::PickupOutcome o = events.pickup(idx, pe);
            if (o.result == gp::PickupResult::Granted && pickedItem >= 0 && pickedItem < int(itemDefs.size()))
            {
                // Spots with notLegacyFlag stay gone once their item has been picked up (SetLegacyFlag).
                const gp::Id &pickedId = itemDefs[size_t(pickedItem)].id;
                for (const gp::FixedSpotDef *s : db.fixedSpotsIn(area))
                    if (s->notLegacyFlag && s->item == pickedId)
                    {
                        gp::markLegacy(player, pickedId);
                        break;
                    }
            }
            if (o.result == gp::PickupResult::NeedItem)
                dialogue.show(gp::messageBox("You need " + itemName(o.needItem) + " first.")); // placeholder text
            else if (o.result == gp::PickupResult::AtCapacity)
                dialogue.show(gp::messageBox("You can't carry any more.")); // placeholder text
        };

        // Marker/flower anims run at 18 fps like InitMapMarkerQueue. Which id is which is a GUESS.
        constexpr int kEvPal = 0x28c, kFlowerSprite = 0x9c1, kHoleSprite = 0x9c2;
        constexpr int kPromptArrowAnim = 0x11; // GUESS: try 0x10 / 0x11 / 0x12

        auto drawWorldSprite = [&](const gp::Id &id, int wx, int wy, float camX, float camY)
        {
            const AnimRef A = animRefById(id);
            if (!A.size())
            {
                static std::set<gp::Id> warnedW;
                if (warnedW.insert(id).second)
                    std::printf("world sprite missing: %s\n", id.c_str());
                return;
            }
            const gp::SpriteFrame &s = A.anim->frames[0];
            SDL_FRect r{wx - camX - s.anchorX + s.dx, wy - camY - s.anchorY + s.dy, float(s.w), float(s.h)};
            SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
        };
        int kHoleDY = 8; // tune by eye

        // Object type id -> shadow ("marker") sprite, -1 = none. Resolved the first time a type is drawn.
        std::map<gp::Id, int> objShadow;
        auto shadowFor = [&](const gp::Id &type) -> int
        {
            const auto cached = objShadow.find(type);
            if (cached != objShadow.end())
                return cached->second;
            int sprite = -1, t = -1;
            const std::string head = type.substr(0, type.find('@')); // ignore a trailing "@palette"
            const size_t last = head.find_last_of("0123456789");
            if (last != std::string::npos)
            {
                size_t b = last;
                while (b > 0 && std::isdigit(static_cast<unsigned char>(head[b - 1])))
                    --b;
                t = std::atoi(head.c_str() + b); // the last run of digits in the id
            }
            if (t >= 0 && db.objectType(type))
            {
                const int base = 0xe9bd + t * 5; // hasMarker, tile, palette, markerTile, overlayTile
                const int marker = rom.u16(base + 3);
                if (rom.u16(base) != 0 && marker != 0xffff)
                    try
                    {
                        gp::importUiSprite(db, rom, marker, 0x28c);
                        sprite = marker;
                    }
                    catch (const std::exception &e)
                    {
                        std::printf("object shadow 0x%x import failed: %s\n", marker, e.what());
                    }
            }
            std::printf("object type '%s' -> index %d, shadow sprite %d\n", type.c_str(), t, sprite);
            objShadow[type] = sprite;
            return sprite;
        };

        constexpr Uint8 kShadowAlpha = 175; // tune by eye: 255 = opaque
        auto drawShadowSprite = [&](const gp::Id &id, float wx, float wy, float camX, float camY)
        {
            const AnimRef A = animRefById(id);
            if (!A.size())
                return;
            const gp::SpriteFrame &s = A.anim->frames[0];
            SDL_Texture *tx = (*A.tex)[0];
            SDL_SetTextureAlphaMod(tx, kShadowAlpha); // these textures are only ever used for shadows
            SDL_FRect r{wx - camX - s.anchorX + s.dx, wy - camY - s.anchorY + s.dy, float(s.w), float(s.h)};
            SDL_RenderCopyF(ren, tx, nullptr, &r);
        };

        // DrawShadow: the blob is chosen by the sprite's WIDTH (the game's "GetSpriteHeight" is really the width);
        // "alt" is the species-2 variant.
        auto drawShadow = [&](int spriteW, float wx, float wy, bool alt, float camX, float camY)
        {
            int idx = (spriteW - 16 >= 0) ? (spriteW - 16) >> 3 : (spriteW - 9) >> 3;
            idx = std::clamp(idx, 0, 11);
            drawShadowSprite(gp::uiSpriteId((alt ? 0x9a0 : 0x994) + idx, 0x28c),
                             wx - (idx * 8 + 8) / 2, wy - (alt ? 0x10 : 6), camX, camY);
        };
        auto drawNpcShadow = [&](const gp::NpcActor &n, float camX, float camY)
        {
            if (n.def->index == 0x39)
                return;
            const AnimRef A = npcAnim(n, std::max(n.anim.animId, 0));
            if (A.size() == 0)
                return;
            const int fr = std::min(n.anim.currentFrame, int(A.size()) - 1);
            const int nsp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
            const auto np = npcPosF(n);
            drawShadow(A.anim->frames[fr].w, np.first, np.second, nsp == 2, camX, camY);
        };
        auto drawItemShadow = [&](int item, int wx, int wy, float camX, float camY)
        {
            if (item < 0 || item >= int(itemDefs.size()))
                return;
            const AnimRef A = animRefById(gp::uiSpriteId(itemDefs[item].worldSprite, gp::kMenuPalette));
            if (A.size())
                drawShadow(A.anim->frames[0].w, wx, wy, false, camX, camY);
        };

        auto drawGroundSprite = [&](const gp::Id &id, int wx, int wy, float camX, float camY)
        {
            const AnimRef A = animRefById(id);
            if (!A.size())
            {
                static std::set<gp::Id> warnedW;
                if (warnedW.insert(id).second)
                    std::printf("world sprite missing: %s\n", id.c_str());
                return;
            }
            const gp::SpriteFrame &s = A.anim->frames[0];
            static bool shown = false;
            if (!shown && id == gp::uiSpriteId(kHoleSprite, kEvPal))
            {
                shown = true;
                std::printf("hole frame: w=%d h=%d anchor(%d,%d) d(%d,%d)\n", s.w, s.h, s.anchorX, s.anchorY, s.dx, s.dy);
            }
            SDL_FRect r{wx - camX - float(s.w / 2) + s.dx, wy - camY - float(s.h / 2) + s.dy + kHoleDY, float(s.w), float(s.h)};
            SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
        };
        auto drawEvent = [&](const gp::WorldItem &e, float camX, float camY)
        {
            if (e.state == gp::EventState::BuriedRandom) // kind 5: flower
                drawWorldSprite(gp::uiSpriteId(kFlowerSprite, kEvPal), e.x, e.y, camX, camY);
            else if (e.state == gp::EventState::Visible && e.item < int(itemDefs.size())) // kind 2: the item itself
                drawWorldSprite(gp::uiSpriteId(itemDefs[e.item].worldSprite, gp::kMenuPalette), e.x, e.y, camX, camY);
        };

        auto categoryMatches = [](const gp::ItemDef &d, int category) -> bool
        {
            switch (category)
            {
            case 0: // all
                return true;
            case 1: // food
                return d.type == 9 || d.type == 3;
            case 2: // medicine
                return d.type == 10;
            case 3: // quest items
                return d.type == 11 || d.type == 12 || d.type == 15;
            case 4: // room items
                return d.type == 8 || d.type == 6 || d.type == 5 || d.type == 4 ||
                       d.type == 1 || d.type == 2;
            default:
                return false;
            }
        };

        auto buildFiltered = [&](int category)
        {
            std::vector<int> out;
            for (const gp::ItemDef &d : itemDefs)
                if (ownedCount(d.index) > 0 && categoryMatches(d, category))
                    out.push_back(d.index);
            return out;
        };

        ItemMenu &im = S.ui.im;

        auto itemWindow = [&]
        {
            std::vector<int> w;
            for (size_t i = size_t(im.firstRow - 1) * 6; i < im.filtered.size() && w.size() < 18; ++i)
                w.push_back(im.filtered[i]);
            return w;
        };
        auto itemRows = [&]
        { return std::max(1, (int(im.filtered.size()) - 1) / 6 + 1); };

        auto itemMenuTick = [&](uint8_t pressed, bool xPress)
        {
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
            {
                im.open = false;
                barBusy = 0;
                barAnim.visible = false;
            }
        };

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

        auto openBar = [&](bool house) // OpenMenuBar when closed; closes it if already open
        {
            if (bar.isOpen())
            {
                bar.cancel(barBusy == 2); // same gate as X: ignored while sliding open or closing
                return;
            }
            if (barBusy != 0 || dialogue.open() || enc || im.open || pendingExit)
                return;
            barBusy = 1;
            barInHouse = house;
            bar.open(house);
            playBarAnim(house ? 0 : 2);
        };

        barHost.onState = [&](int s)
        {
            if (s == kStatsState)
            {
                requestScreen([&]
                              { statsOpen = true; });
                return;
            }
            if (s != 0xe)
            {
                std::printf("bar: SwitchGameState(0x%x)\n", s);
                return;
            }
            im = {};
            im.open = true;
            im.category = barHost.category;
            im.filtered = buildFiltered(im.category);
            std::printf("item menu: category %d, %zu items, slot %d\n", im.category, im.filtered.size(), itemSlot);
        };

        // Queue draw calls, then sort by layer
        std::vector<Cmd> &menuCmds = S.ui.menuCmds;
        struct ItemGfx : gp::Gfx
        {
            std::function<void(int, int, int, int, int)> tile;
            std::function<void(int, int, int, const std::string &, int, int, int)> box;
            std::function<int(int)> spriteW; // new
            void drawTile(int s, int p, int x, int y, int layer, int) override { tile(s, p, x, y, layer); }
            void drawTextBox(int st, int, int x, int y, const std::string &t, int w, int m, int l) override { box(st, x, y, t, w, m, l); }
            int getSpriteWidth(int s) override { return spriteW ? spriteW(s) : 0; } // new
        } itemGfx;
        itemGfx.tile = [&](int sprite, int pal, int x, int y, int layer)
        {
            static std::set<gp::Id> warned;
            const gp::Id uid = gp::uiSpriteId(sprite, pal);
            if (!animRefById(uid).size() && warned.insert(uid).second)
                std::printf("item menu tile missing: %s (at %d,%d layer %x)\n", uid.c_str(), x, y, layer);
            menuCmds.push_back({layer, [=, &animRefById]
                                {
                                    const AnimRef A = animRefById(gp::uiSpriteId(sprite, pal));
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
            if (aid == gp::kIdleBase[sp] && p == Pose::Idle)
            {
                idleTicks = 0;
                idleThreshold = 90 + std::rand() % 91; // RandRange(30,60) / 10 seconds at 30 ticks/sec
            }
            // std::printf("play anim %d (%s, rate %d)\n", aid, loop ? "loop" : "once", rate);
        };

        gp::EncounterHost host;
        auto walkAxis = [&](int target, bool xAxis) -> bool
        {
            int &c = xAxis ? pos.x : pos.y;
            if (c == target)
                return true;
            const uint8_t dir = xAxis ? (target < c ? gp::DirLeft : gp::DirRight)
                                      : (target < c ? gp::DirUp : gp::DirDown);
            const int before = c;
            pos = gp::stepPlayer(*world, pos, dir).pos;
            if ((before < target) != (c < target)) // overshot: snap
                c = target;

            pose = Pose::Move;
            settleTicks = 0;
            const int oldF = facing;
            const bool oldL = faceLeft;
            if (xAxis)
            {
                facing = 2;
                faceLeft = dir == gp::DirLeft;
            }
            else
                facing = dir == gp::DirUp ? 0 : 1;
            if (facing != oldF || faceLeft != oldL)
                walkTicks = 0;
            ++walkTicks;
            return c == target;
        };
        host.petX = [&]
        { return pos.x; };
        host.npcX = [&]
        { return encNpc->x(); };
        host.npcY = [&]
        { return encNpc->y(); };
        host.petWalkToX = [&](int x)
        { return walkAxis(x, true); };
        host.petWalkToY = [&](int y)
        { return walkAxis(y, false); };
        host.npcLock = [&]
        {
            const bool wasWalking = encNpc->walking();
            if (wasWalking)
                encNpc->startActivity(gp::kActivityTalkedTo, npcRng);
            encNpc->locked = true;
            return wasWalking;
        };
        host.npcSettled = [&]
        {
            return encNpc->settled();
        };
        host.npcUnlock = [&]
        {
            encNpc->locked = false;
            encNpc->clearForced();
        };
        host.markFriended = [&]
        {
            player.markFriended(encNpc->def->id);
            return int(player.friended.size());
        };
        host.event = [](const char *k, int v)
        {
            std::printf("event %s 0x%x\n", k, v);
        };

        host.pet.play = [&](gp::ReactAnim a, int v)
        {
            const bool side = facing == 2;
            if (facing == 0 && (a == gp::ReactAnim::Idle || a == gp::ReactAnim::Positive ||
                                a == gp::ReactAnim::Emote || a == gp::ReactAnim::Shocked))
                return false; // facing up: no art in these tables
            const int id = gp::reactId(a, sp, v, side);
            const int n = id < 0 ? 0 : int(getAnim(id).size());
            if (n == 0)
                return false;
            playAnim(id, false, a == gp::ReactAnim::Bored ? 7 : 14, Pose::Special);
            animReverse = a == gp::ReactAnim::NegOutro || a == gp::ReactAnim::Outro;
            if (animReverse)
                animFrame = n - 1;
            return true;
        };

        host.pet.busy = [&]
        { return !animLoop && !animDone; };
        host.pet.sound = [](int c, int id)
        { std::printf("sound cat %d id 0x%x\n", c, id); };
        host.pet.bubble = [](int id)
        { std::printf("bubble 0x%x\n", id); };
        host.pet.rand = [&](int lo, int hi)
        { return lo + std::rand() % (hi - lo + 1); };

        int &settlePhase = S.pet.settlePhase; // TickActor_SettleFacing: no 0.15 s wait, ends facing down
        host.pet.settle = [&]() -> bool
        {
            if (settlePhase == 0)
            {
                if (facing == 1)
                    return true;
                if (facing == 0)
                {
                    faceLeft = (std::rand() % 3) == 0;
                    playAnim(gp::kTurn180[sp], false, 14, Pose::Settle, {{gp::kTurn90[sp], false, 14}, {gp::kIdleBase[sp], true, 14}});
                }
                else
                    playAnim(gp::kTurn90[sp], false, 14, Pose::Settle, {{gp::kIdleBase[sp], true, 14}});
                settlePhase = 1;
                return false;
            }
            if (pose == Pose::Idle && animId == gp::kIdleBase[sp])
            {
                facing = 1;
                settlePhase = 0;
                return true;
            }
            return false;
        };
        host.petIdle = [&]
        {
            if (facing == 1)
                playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle);
            else if (facing == 2)
                playAnim(gp::kIdleSide[sp], true, 14, Pose::Idle); // kIdleSide[0]
            else
            {
                pose = Pose::Move; // up: no idle art, keep the old hold
                walkTicks = 0;
                settleTicks = 0;
            }
        };
        host.petFace = [&](bool left)
        { facing = 2; faceLeft = left; walkTicks = 0; }; // petIdle follows
        host.petEngage = [&]
        {
            if (facing == 0)
            {
                facing = 2;
                faceLeft = pos.x >= encNpc->x();
            }
            playAnim(gp::kIdleSide[sp], true, 14, Pose::Idle); // kIdleSide[0]
        };

        host.npcIdle = [&]
        { encNpc->clearForced(); };
        host.npcFacePet = [&]
        {
            encNpc->body.facing = encNpc->x() < pos.x ? gp::FaceRight : gp::FaceLeft;
        };
        host.npc.rand = [](int lo, int hi)
        { return lo + std::rand() % (hi - lo + 1); };
        host.npc.sound = [](int c, int id)
        { std::printf("npc sound cat %d id 0x%x\n", c, id); };
        host.npc.bubble = [](int id)
        { std::printf("npc bubble 0x%x\n", id); };
        host.npc.settle = [&]
        { return encNpc->settled(); };
        host.npc.busy = [&]
        { return !encNpc->forcedAnimId.empty() && encNpc->animBusy(); };
        host.npc.play = [&](gp::ReactAnim a, int v)
        {
            const gp::NpcActor &n = *encNpc;
            const int f = n.body.facing;
            const bool side = f == gp::FaceLeft || f == gp::FaceRight;
            if (!side && f != gp::FaceDown &&
                (a == gp::ReactAnim::Idle || a == gp::ReactAnim::Positive || a == gp::ReactAnim::Emote || a == gp::ReactAnim::Shocked))
                return false; // facing up: no art
            const int idle = std::atoi(n.def->anim.c_str() + n.def->anim.rfind('/') + 1);
            const int tier = idle < 0x70 ? 0 : idle < 0xc0 ? 1
                                                           : 2; // GUESS: tier from the idle id's range
            const int sp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
            const int id = gp::reactId(a, sp, v, side);
            if (id < 0)
                return false;
            const gp::Id full = reactAnimId(id, n.def->appearancePalette);
            if (animRefById(full).size() == 0)
                return false;
            encNpc->forceAnim(full); // change forceAnim/forcedAnim to store an Id (std::string)
            const bool neg = a == gp::ReactAnim::Negative || a == gp::ReactAnim::NegIntro ||
                             a == gp::ReactAnim::NegOutro;
            const bool panda = sp == 2;
            if (neg)
                encNpc->forcedFlip = panda ? (f == gp::FaceLeft) : (f != gp::FaceLeft);
            else
                encNpc->forcedFlip = side && gp::reactMirror(sp, f == gp::FaceRight);

            std::printf("npc play %d (tier %d, idle %d)\n", id, tier, idle);
            return true;
        };

        auto playFeedHappy = [&]
        {
            // (3, 2) in the original: 3 = repeat count, 2 = probably the variant. Try 0 if the art looks wrong.
            if (!host.pet.play(gp::ReactAnim::Positive, 2))
                animDone = true; // no art: let the sequence move on
        };

        auto startFeed = [&]
        {
            facing = 1; // face down so the front-facing happy plays
            walkTicks = 0;
            feedFood = true;
            feedStep = 1;
            playAnim(gp::kEat[sp], false, 14, Pose::Special);
        };

        auto startMedicine = [&](int curedIllness)
        {
            facing = 1;
            walkTicks = 0;
            feedFood = false;
            feedStep = 3;
            feedHappy = 0;
            overId = 137 + (curedIllness % 13);
            overTicks = 0;
            playFeedHappy();
        };

        auto tickFeed = [&]
        {
            if (!feedStep || !animDone)
                return;
            if (feedStep == 1)
            {
                feedRepeats = 4 + std::rand() % 6; // RandRange(4,9)
                feedCount = 1;
                feedStep = 2;
                playAnim(gp::kEat[sp], false, 14, Pose::Special);
            }
            else if (feedStep == 2)
            {
                if (++feedCount <= feedRepeats)
                    playAnim(gp::kEat[sp], false, 14, Pose::Special);
                else
                {
                    feedStep = 3;
                    feedHappy = 0;
                    playFeedHappy();
                }
            }
            else if (feedStep == 3)
            {
                if (++feedHappy < 3)
                    playFeedHappy();
                else
                {
                    if (feedFood && player.care[gp::Happiness] < 0x1a)
                        player.adjustCare(gp::Happiness, 6);
                    feedStep = 0;
                    overId = -1;
                    playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle);
                }
            }
        };

        // PlayDigForItemRoutine, simplified: settle, bubble + sounded task, then the hit test.
        int &digAnimDY = S.pet.digAnimDY; // 0 = anchor sits at the pet's feet; tune with 9 / 0
        int &digStep = S.pet.digStep, &digTicks = S.pet.digTicks;
        constexpr int kDigAnimId[3] = {0x1c, 0x80, 0xe9}; // PlayTaskAnimationVariant table, by species
        constexpr int kDigTicks = 30;                     // placeholder: length of sound 0xbe (SFX category 9)

        auto startDig = [&]
        { digStep = 1; digTicks = 0; settlePhase = 0; walkTicks = 0; };

        auto tickDig = [&]
        {
            if (!digStep)
                return;
            switch (digStep)
            {
            case 1: // settle facing (the ROM's reaction step is skipped)
                if (host.pet.settle())
                    digStep = 2;
                break;
            case 2: // bubble + RunSoundedAnimationTask
                overId = 0x16;
                overTicks = 0;
                player.adjustCare(gp::Cleanliness, sp == 1 ? -2 : -1);
                player.adjustCare(gp::Tiredness, -1); // check this direction in-game
                playAnim(kDigAnimId[sp], true, 14, Pose::Special);
                digTicks = 0;
                digStep = 3;
                break;
            case 3: // wait for sound 9 to end
                if (++digTicks >= kDigTicks)
                    digStep = 4;
                break;
            case 4:
            { // IsBubbleFinished, then the hit test
                const int frames = int(animRefById(reactAnimId(0x16, 0x28c)).size());
                if (frames > 0 && overTicks * kBubbleFps / 30 < frames)
                    break;
                overId = -1;
                const size_t holesBefore = events.holes().size();

                uint32_t bits = 0; // player flags -> bits
                for (int n = 0; n < 32; ++n)
                    if (player.hasTrigger(n))
                        bits |= 1u << n;

                const gp::DigResult r = events.dig(cur->def->legacyId, pos.x, pos.y, bits, rnd);

                for (int n = 0; n < 32; ++n) // bits -> player flags (only new ones matter)
                    if (bits >> n & 1u)
                        player.setTrigger(n);

                const bool found = r.kind == gp::DigResult::Kind::Item;
                std::printf("dig: %s\n", found ? "item" : r.kind == gp::DigResult::Kind::Dud ? "dud"
                                                                                             : "miss");
                std::printf("holes now: %zu\n", events.holes().size());
                if (r.triggerSet)
                    std::printf("trigger flags 0x%x (SavePetStatusBlock)\n", bits);
                animDone = true;
                digStep = 5;
                break;
            }
            case 5: // wait for the reaction, then idle
                if (animDone)
                {
                    digStep = 0;
                    playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle);
                }
                break;
            }
        };

        int &swiftDropStep = S.pet.swiftDropStep, &swiftDropTicks = S.pet.swiftDropTicks;
        std::vector<int> &swiftDropItems = S.pet.swiftDropItems;
        auto startItemDrop = [&](std::vector<int> items)
        {
            if (items.empty())
                return;
            sfx(0x5d);
            swiftDropItems = std::move(items);
            swiftDropStep = 1;
            swiftDropTicks = 0;
            settlePhase = 0;
            walkTicks = 0;
        };
        mgEnv.startItemDrop = startItemDrop;
        auto tickSwiftDrop = [&]
        {
            if (!swiftDropStep || minigames.active() || pendingScreen) // activity runs once the world is back
                return;
            if (swiftDropStep == 1)
            {
                if (host.pet.settle())
                    swiftDropStep = 2;
            }
            else if (++swiftDropTicks >= 45) // 1.5 s at 30 Hz
            {
                // ROM RandRange is exclusive at the top; this randIn is inclusive, so hi - 1.
                int x = minigames.petX + randIn(-35, 35), y = minigames.petY + randIn(-2, 3);
                for (size_t k = 0; k < swiftDropItems.size(); ++k)
                {
                    if (k > 0) // each next item: 6..10 lower, 0x14..0x22 to the other side of the target x (sign of the side: GUESS)
                    {
                        y += randIn(6, 0xb - 1);
                        const int d = randIn(0x14, 0x23 - 1);
                        x = minigames.petX + (x >= minigames.petX ? -d : d);
                    }
                    events.placeAt(cur->def->legacyId, swiftDropItems[k], x, y, false); // persist flag is a guess
                }
                sfx(0x54);
                swiftDropStep = 0;
                playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle);
            }
        };

        auto useSelectedItem = [&]
        {
            const size_t idx = size_t((im.selRow - 1) * 6 + im.selCol - 1);
            if (idx >= im.filtered.size())
                return;
            const gp::ItemDef &d = itemDefs[im.filtered[idx]];

            // Item.hooks(id).onItemUse: a script that returns false takes the whole use over (no eating, no medicine,
            // nothing consumed unless the script takes it). The menu closes so it is rebuilt from the new inventory.
            if (scripts.fireItemUse(d.id).suppress)
            {
                im.open = false;
                barBusy = 0;
                barAnim.visible = false;
                return;
            }

            if (d.type == int(gp::ItemType::Medicine))
            {
                if (d.w9 == gp::kMedicineNoIllness)
                    return;
                const bool cured = gp::applyMedicine(player, d);
                player.removeItem(d.id, 1);
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
                }
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

            // Menu result 2: leave the menu and play the eat animation.
            im.open = false;
            barBusy = 0;
            barAnim.visible = false;
            startFeed();
        };

        // ---- 4. STATE + AREA SWITCHING ----
        bool &exitsArmed = S.flow.exitsArmed; // stops bouncing straight back through the door you arrived by
        bool &prevZ = S.flow.prevZ;
        bool &prevX = S.flow.prevX;

        auto goTo = [&](const gp::ExitDef &e) -> bool
        {
            enc.reset();
            encNpc = nullptr;
            const gp::TransitionDef *tr = db.transition(cur->def->id, e.toArea);
            if (!tr)
            {
                std::printf("no room-table row for %s -> %s\n", cur->def->id.c_str(), e.toArea.c_str());
                return false;
            }
            auto next = loadArea(db, e.toArea, showCollision);
            if (!next)
            {
                std::printf("area %s could not be loaded\n", e.toArea.c_str());
                return false;
            }
            const gp::IPoint s = e.hasSpawnOverride ? e.spawnOverride : tr->spawn;
            std::printf("area %d -> %d, spawn (%d,%d)\n", cur->def->legacyId, next->def->legacyId, s.x, s.y);
            const int fromLegacy = cur->def->legacyId;
            cur = std::move(next);
            if (tr->flags & 2)
                enterShopStock(*cur->def); // destination is a shop
            else
            {
                if (tr->flags & 1)
                    pending.consumeUnflaggedForArea(fromLegacy); // leaving a shop
                enterAreaEvents(*cur->def, e.consumeAndSpawn);
                refreshBits();
                saveGame();
            }
            world = std::make_unique<AreaWorld>(*cur->ground, *cur->runtime, cur->room);
            pos = {s.x, s.y};
            prevPos = pos; // no gliding across the map after a door
            SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cur->img.w, cur->img.h);
            SDL_UpdateTexture(tex, nullptr, cur->img.rgba.data(), cur->img.w * 4);
            cur->runtime->updateVisible(clampCam(pos.x - W / 2, cur->img.w, W),
                                        clampCam(pos.y - kCamY, cur->img.h, H));
            loadNpcs();
            exitsArmed = false;
            return true;
        };
        auto requestGoTo = [&](const gp::ExitDef &e)
        {
            if (pendingExit || fade.a > 0.0)
                return;
            pendingExit = e;
            fade.go(1.0, 8.0 / 30); // fade out
        };

        auto removeShopItem = [&](int area, int slot) { // provisional, see below
            for (size_t k = 0; k < pending.v.size(); ++k)
                if (pending.v[k].area == area && pending.v[k].slot == slot)
                {
                    pending.v.erase(pending.v.begin() + k);
                    break;
                }
        };

        auto buyFromShop = [&](const gp::ShopEvent &pe)
        {
            const int item = pe.item, area = pe.area, slot = pe.slot;
            const int row = gp::shopRowForArea(area), si = shopStock.find(row, item);
            if (si < 0)
                return;
            const gp::ItemDef &d = itemDefs[item];
            const std::string name = itemName(item);
            const gp::BuyQuote q = gp::quotePurchase(shopStock.rows[row][si].stock(), money, d.price, ownedCount(item), d.maxQty);

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
                dialogue.show(gp::messageBox(name + " " + std::to_string(d.price) + " Bucks each. You can't afford any."));
                return;
            }
            // Pick: the real game opens a quantity picker here (string at 0xb637). STOPGAP until dialogue.hpp has one:
            // a Yes/No box that buys exactly 1.
            gp::DialogueBox b = gp::messageBox(name + " " + std::to_string(d.price) + " Bucks each. How many would you like?");
            b.kind = gp::BoxKind::Quantity;
            b.qty = 1;
            b.qtyMax = q.maxQty;
            b.onQuantity = [&, item, area, row](int qty)
            {
                gp::deliverPurchase(shopStock, row, item, qty, [&](int id)
                                    { if (player.itemCount(itemDefs[size_t(id)].id) < itemDefs[size_t(id)].maxQty) player.addItem(itemDefs[size_t(id)].id, 1); }, // CheckQuestItemObjectivesComplete not ported
                                    [&](int)
                                    { pending.removeShopItem(area, item); });
                money -= qty * itemDefs[item].price; // Item_ProcessPurchase: AdjustPlayerMoney(-qty * price)
            };
            dialogue.show(std::move(b));
        };

        // ---- quests ----
        // NPC lookup by pool id (NpcDef::index). NpcDef's area field is assumed to be `area`: rename if it differs.
        std::vector<const gp::NpcDef *> npcByIndex(0x4e, nullptr);
        for (int a = 0; a < 256; ++a)
            for (const gp::NpcDef *d : db.npcsInArea(a))
                if (d->index >= 0 && d->index < 0x4e)
                    npcByIndex[size_t(d->index)] = d;
        auto npcAt = [&](int p) -> const gp::NpcDef *
        { return p >= 0 && p < 0x4e ? npcByIndex[size_t(p)] : nullptr; };

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

        quests.table = db.quests();          // scripted quests: imported by importRom, mods can replace or append
        quests.world.rom = db.questTables(); // generic-quest item weights and category distances: also from the db
        quests.world.types = db.questTypes();
        quests.world.playerSpecies = sp;
        quests.world.areaTier = [&](int p)
        { const gp::NpcDef *d = npcAt(p); return d ? d->area : 0; };
        quests.world.stationary = [&](int p)
        { const gp::NpcDef *d = npcAt(p); return d && d->stationary; };
        quests.dlg = &dialogue;
        quests.world.speciesOf = [&](int p)
        {
            const gp::NpcDef *d = npcAt(p);
            int s = d ? d->speciesOrAnim : 0;
            if (s == 0xfffe)
                s = sp; // 0xfffe = "same as the player", as in importNpcs
            return (s >= 0 && s <= 2) ? s : 0;
        };

        auto camNowX = [&]
        { return clampCam(pos.x - W / 2, cur->img.w, W); };
        auto camNowY = [&]
        { return clampCam(pos.y - kCamY, cur->img.h, H); };
        refreshBits = [&]
        {
            for (const gp::Id &sid : db.offerSetIds())
                if (const gp::OfferSetDef *s = db.offerSet(sid))
                    gp::autoMarkOffers(*s, quests.counter, player);
            for (const gp::ObjectDef &o : cur->def->objects)
            {
                const bool needsOffer = !o.requiresOffered.empty();
                const bool offered = needsOffer && player.hasFlag(gp::offeredKey(o.requiresOffered));
                cur->runtime->setHidden(o.id, (needsOffer && !offered) || !o.availableAt(quests.counter));
            }
            cur->runtime->updateVisible(camNowX(), camNowY());
        };

        saveGame = [&]
        {
            player.questsCompleted = quests.counter;
            gp::SaveData s = sd; // keep keys we don't own (whatever createNewSave wrote: colour, starter stats...)
            s.setInt("version", 3);
            s.setInt("statsInit", 1);
            s.setInt("species", sp);
            s.kv.erase("kingFlags");
            s.kv.erase("gardenFlags");
            gp::savePlayer(player, s);
            s.kv.erase("inventory");
            gp::saveToFile(savePath, s);
        };

        gp::QuestHooks &qh = quests.h;
        qh.rnd = rnd;
        qh.text = [&](int id)
        { return gp::fmtAll(db.textOr("base:text/" + std::to_string(id), "?"), player.name); }; // key format: same as item names, unverified for quest text
        qh.held = [&](const gp::Id &id)
        { return player.itemCount(id); };

        qh.remove = [&](const gp::Id &id, int n)
        {
            player.removeItem(id, n);
        };

        qh.spawn = [&](int area, const gp::Id &id, int st, bool persist)
        {
            const gp::ItemDef *d = db.item(id);
            if (!d)
                return;
            if (const gp::AreaDef *a = db.areaByLegacy(area))
                events.spawnAtFreeSlot(*a, d->index, st == 2 ? gp::EventState::Visible : gp::EventState::BuriedRandom, persist, rnd);
        };

        qh.place = [&](const gp::Id &id, int x, int y, bool flag)
        {
            const gp::ItemDef *d = db.item(id);
            if (!d)
                return;
            events.placeAt(cur->def->legacyId, d->index, x, y, flag);
        };
        qh.randomEvent = [&]
        { events.spawnRandom(*cur->def, 1, rnd); }; // the ROM call's args are lost in the decompile
        qh.clearFlagged = [&]
        { events.consumePersistent(); };
        qh.sfx = sfx;
        qh.camX = [&]
        { return pos.x; };
        qh.camY = [&]
        { return pos.y; };
        qh.receiverY = [&]
        {
            for (const gp::NpcActor &n : npcs)
                if (n.def->index == quests.q.def.receiver)
                    return n.y();
            return pos.y;
        };
        qh.nowTick = [&]
        { return int(gameTicks); };
        qh.portrait = [&](gp::DialogueBox &b, int p)
        {
            if (const gp::NpcDef *d = npcAt(p))
                b.portrait = gp::uiSpriteId(d->portraitSprite, d->portraitPalette);
        };
        qh.onNextQuest = [&]
        {
            player.questsCompleted = quests.counter;
            loadNpcs(); // only ever runs from a dialogue callback, never inside talkToNpc's own NpcActor reference
            refreshBits();
            saveGame();
        };

        walk::bindScripts(svc); // world.drop, rand.* (and later player.* / flags): walk_script_bindings.hpp

        gp::QuestTextEnv &txt = quests.txt; // adjust to the real member name

        quests.txt.rnd = rnd;
        quests.txt.str = [&](const std::string &k)
        { return db.textOr("base:str/quest/" + k); }; // "" when missing: the builders then show "(key)"
        quests.txt.npcName = [&](int p)
        { const gp::NpcDef *d = npcAt(p); return d ? db.textOr(d->nameText, "?") : std::string("?"); };
        quests.txt.npcTier = quests.world.areaTier;
        quests.txt.areaName = [&](int area)
        {
            const gp::AreaDef *a = db.areaByLegacy(area);
            return a ? db.textOr(a->name, "?") : std::string("?");
        };
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

        // DrawQuestRewardHud. gateA = pet not busy, gateB = no mini pet connected (not ported: pass true).
        auto drawQuestHud = [&](bool gateA, bool gateB)
        {
            const gp::QuestState &qs = quests.q;
            if (qs.phase == 0 || !qs.hudActive)
                return;
            if (gateA && gateB)
            {
                int x = 0x23;
                for (size_t k = 0; k < qs.def.objectives.size() && k < 3; ++k, x += 100)
                {
                    const gp::QuestEntry &o = qs.def.objectives[k];
                    const gp::ItemDef *dp = db.item(o.item);
                    if (!dp)
                        continue;
                    const gp::ItemDef &d = *dp;
                    const AnimRef A = animRefById(gp::uiSpriteId(d.worldSprite, gp::kMenuPalette)); // ROM uses item record word 7 as the palette
                    if (A.size())
                    {
                        const gp::SpriteFrame &s = A.anim->frames[0];
                        const int y = s.h + 200;
                        SDL_Rect r{x - s.anchorX + s.dx, y - s.anchorY + s.dy + 1, s.w, s.h};
                        SDL_RenderCopy(ren, (*A.tex)[0], nullptr, &r);
                    }
                    const int shown = std::min(player.itemCount(o.item), o.count);
                    text.draw(std::to_string(shown) + "/" + std::to_string(o.count), x, 210, 60, gp::Align::Right);
                }
            }
            if (gp::timerShown(qs))
            {
                const int rem = gp::timerRemaining(qs, gp::elapsedSeconds(int(gameTicks), qs.timerStart));
                char b[16];
                std::snprintf(b, sizeof b, "%d:%02d", rem / 60, rem % 60);
                text.draw(b, 200, 20, 90, gp::Align::Right);
            }
        };
        enterAreaEvents(*cur->def, true);
        refreshBits(); // add
        bool &running = S.flow.running;
        bool &backToMenu = S.flow.backToMenu;
        const double kTickHz = 30.0; // verified: the game ticks at 30 Hz
        const Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 prev = SDL_GetPerformanceCounter();
        double &acc = S.flow.acc;
        // Fade in from black (the front end fades out before handing over). The clock starts after the first
        // presented frame, so texture-upload hitches in that frame don't eat the fade.

        Uint64 fadeLast = SDL_GetPerformanceCounter();

        // What pressing Z on an object does (well, tent, signs, minigames, the onInteract script seam): walk_interactions.hpp.
        // Needs `minigames`, so it is built here rather than next to `svc`.
        walk::Interactions interactions(svc, minigames);

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
                    switch (e.key.keysym.sym)
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
                                    clampCam(pos.x - W / 2, cur->img.w, W), clampCam(pos.y - kCamY, cur->img.h, H),
                                    pos.x, pos.y, cur->room.width, cur->room.height,
                                    cur->room.top, cur->room.bottom);
                        break;
                    // ---- one-shot actions (rates for 29, 47, 49 are placeholders; 45 is known) ----
                    case SDLK_e:
                        playAnim(29, false, 14, Pose::Special); // eat
                        break;
                    case SDLK_b:
                        playAnim(gp::kCallFront[sp], false, 14, Pose::Special); // bark
                        break;
                    case SDLK_n:
                        player.illnessId = (player.illnessId + 1) % 11; // line 1460, the N key
                        std::printf("illness %d\n", player.illnessId);
                        break;
                    case SDLK_s:
                        playAnim(46, false, 14, Pose::Special, {{gp::kSettled[sp], true, sp == 0 ? 7 : 5}}); // action 7 in the game: 46 once, then sit idle 45 at rate 7
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
                        SDL_RenderSetIntegerScale(ren, smoothCam ? SDL_TRUE : SDL_FALSE);
                        std::printf("smooth camera %s\n", smoothCam ? "on" : "off");
                        break;
                    case SDLK_F10: // back to the front-end menu
                        backToMenu = true;
                        running = false;
                        break;
                    case SDLK_RETURN:
                        openBar(false);
                        break;
                    case SDLK_TAB:
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

            while (acc >= 1.0 / kTickHz)
            {
                acc -= 1.0 / kTickHz;
                gameTicks += 2;
                // remember where everything was before this tick, for the smooth renderer
                prevPos = pos;
                npcPrev.resize(npcs.size());
                for (size_t ni = 0; ni < npcs.size(); ++ni)
                    npcPrev[ni] = {npcs[ni].x(), npcs[ni].y()};
                if (gameTicks - lastSaveTick >= 1800)
                {
                    lastSaveTick = gameTicks;
                    saveGame();
                }
                const Uint8 *k = SDL_GetKeyboardState(nullptr);
                const uint8_t rawDirs = enc ? 0 : gp::pollHeld(k[SDL_SCANCODE_LEFT], k[SDL_SCANCODE_RIGHT], k[SDL_SCANCODE_UP], k[SDL_SCANCODE_DOWN]);
                const uint8_t newlyPressed = uint8_t(rawDirs & ~prevDirs);

                prevDirs = rawDirs;

                // Z is read once per tick, as an edge. prevZ updates even while a box is open, so the press
                // that opens a box can never also close it.
                const bool zDown = k[SDL_SCANCODE_Z];
                const bool zPressed = zDown && !prevZ;
                const bool xDown = k[SDL_SCANCODE_X];
                const bool xPressed = xDown && !prevX;
                prevX = xDown;
                prevZ = zDown;
                if (barAnim.visible)
                {
                    const AnimRef A = barAnimRef(barAnim.id);
                    const int n = int(A.size());
                    if (n == 0)
                        barAnim.done = true;
                    else if (!barAnim.done)
                    {
                        const int idx = ++barAnim.ticks * kBarRate / 30;
                        barAnim.frame = std::min(idx, n - 1);
                        if (idx >= n)
                            barAnim.done = true;
                    }
                    if (barBusy == 1 && (barAnim.frame >= 5 || barAnim.done))
                        barBusy = 2; // GUESS: DAT_0020d9 == 5
                    if (barBusy == 3 && barAnim.done)
                    {
                        barBusy = 0;
                        barAnim.visible = false;
                    }
                }
                if (pendingExit || pendingScreen)
                    continue;
                if (statsOpen)
                {
                    if (zPressed || xPressed)
                    {
                        requestScreen([&]
                                      { statsOpen = false; barBusy = 0; barAnim.visible = false; });
                    }
                    continue;
                }
                if (minigames.active())
                {
                    MinigameInput in;
                    in.live = fade.a <= 0.0; // ignore presses during the fade-in
                    in.z = zPressed;
                    in.x = xPressed;
                    in.left = (newlyPressed & gp::DirLeft) != 0;
                    in.right = (newlyPressed & gp::DirRight) != 0;
                    in.up = (newlyPressed & gp::DirUp) != 0;
                    in.down = (newlyPressed & gp::DirDown) != 0;
                    minigames.tick(in);
                    continue; // world frozen while a minigame is up
                }
                if (im.open)
                {
                    itemMenuTick(newlyPressed, xPressed);
                    if (zPressed && im.open)
                        useSelectedItem();
                    continue; // world frozen while the item screen is up
                }
                if (dialogue.open())
                {
                    gp::DialogueBox &b = *dialogue.box;
                    if (b.kind == gp::BoxKind::Confirm)
                    {
                        if (newlyPressed & gp::DirLeft)
                            b.yesSelected = true; // Yes is on the left
                        if (newlyPressed & gp::DirRight)
                            b.yesSelected = false;
                        if (zPressed)
                            dialogue.choose(b.yesSelected ? 1 : 2); // don't use b after this
                    }
                    else if (b.kind == gp::BoxKind::Quantity)
                    {
                        if (newlyPressed & (gp::DirRight | gp::DirUp))
                            b.qty = std::min(b.qty + 1, b.qtyMax);
                        if (newlyPressed & (gp::DirLeft | gp::DirDown))
                            b.qty = std::max(b.qty - 1, 1);
                        if (zPressed)
                            dialogue.pick(b.qty); // don't use b after this
                        else if (xPressed)
                            dialogue.choose(2); // cancel with X: a guess
                    }
                    else if (zPressed)
                        dialogue.choose(1);
                    continue;
                }

                if (bar.isOpen())
                {
                    barHost.pressed = zDown;
                    if (newlyPressed & gp::DirLeft)
                        bar.handleEvent(0, true);
                    if (newlyPressed & gp::DirRight)
                        bar.handleEvent(1, true);
                    if (zPressed)
                        bar.handleEvent(4, true);
                    else if (xPressed)
                        bar.cancel(barBusy == 2);
                }
                // The pet can't be controlled while the bar is opening, open, or closing (g_isPetBusy != 0).
                // The world keeps running.
                const bool petLocked = barBusy != 0 || feedStep != 0 || digStep != 0 || swiftDropStep != 0;
                const uint8_t dirs = petLocked ? 0 : rawDirs;
                if (enc)
                {
                    enc->tick(host);
                    if (enc->done())
                    {
                        const gp::NpcDef &d = *encNpc->def;
                        const bool friend_ = enc->outcome() == gp::EncounterSequence::Outcome::Friend;
                        encNpc->locked = false;
                        enc.reset();
                        encNpc = nullptr;
                        playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle);
                        if (friend_ && gp::befriendRoll(npcRng, gp::befriendChance(d.befriendOdds, player.personality[gp::Stars], d.stars)))
                            walk::grantFriendship(svc, false);
                    }
                    if (enc && encNpc)
                    {
                        static int dbg = 0;
                        if (++dbg % 30 == 0)
                            std::printf("step %d npc.busy=%d forced=%s key=%d frame=%d\n",
                                        enc->step(), int(host.npc.busy()), encNpc->forcedAnimId.c_str(),
                                        encNpc->animKey(), encNpc->anim.currentFrame);
                    }
                }
                for (gp::NpcActor &n : npcs)
                {
                    n.tick(npcRng);
                    updateNpcAnim(n);
                }

                // ---- movement + facing + walk animation ----
                if (dirs)
                {
                    medStuck = false;  // real input ends the stuck walk
                    pose = Pose::Move; // any direction input cancels idle/special animations
                    settleTicks = 0;
                    speedMul = (SDL_GetModState() & KMOD_SHIFT) ? 4 : 1; // temporary test boost
                    for (int rep = 0; rep < speedMul; ++rep)
                        pos = gp::stepPlayer(*world, pos, dirs).pos;
                    const int oldFacing = facing;
                    const bool oldLeft = faceLeft;
                    if (dirs & (gp::DirLeft | gp::DirRight))
                    {
                        facing = 2;
                        faceLeft = (dirs & gp::DirLeft) != 0;
                    }
                    else
                        facing = (dirs & gp::DirUp) ? 0 : 1;
                    // The game only calls UpdateActorSpriteFrame (which restarts the walk loop via
                    // PlaySpriteAnimation) when walking starts or the direction changes, not every tick.
                    if (facing != oldFacing || faceLeft != oldLeft)
                        walkTicks = 0;
                    // The game runs this on each button press event (not while a key stays held), so every
                    // new press restarts the walk loop at frame 0, even when tapping the same direction.
                    if (newlyPressed)
                        walkTicks = 0;
                    ++walkTicks; // the game animates while a direction is held, even when blocked by a wall or object
                }
                else if (pose != Pose::Move && pose != Pose::Wait)
                    walkTicks = 0; // (while Move/Wait the walk loop keeps its phase)

                // ---- just stopped walking ----
                if (!dirs && pose == Pose::Move && !enc && !medStuck)
                {
                    if (facing == 1)
                        playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle); // facing down: straight to the base idle, no wait, no turn
                    else
                    {
                        pose = Pose::Wait; // sideways / up: the walk loop keeps playing while the 0.15 s settle timer runs
                        settleTicks = 0;
                    }
                }
                if (pose == Pose::Wait)
                {
                    ++walkTicks; // the walk animation keeps advancing during the wait (the game doesn't freeze it)
                    if (++settleTicks >= kSettleTicks)
                    {
                        // After walking up the 180 degree turn is 52 then 53; after sideways it is just 53; then 34.
                        if (facing == 0)
                        {
                            // the game picks the turn side at random: RandRange(1,3) == 1 -> left (1/3), otherwise right (2/3)
                            faceLeft = (std::rand() % 3) == 0;
                            playAnim(gp::kTurn180[sp], false, 14, Pose::Settle, {{gp::kTurn90[sp], false, 14}, {gp::kIdleBase[sp], true, 14}});
                        }
                        else
                            playAnim(gp::kTurn90[sp], false, 14, Pose::Settle, {{gp::kIdleBase[sp], true, 14}});
                    }
                }

                // ---- idle timer: after 3-6 s of base idle the pet sits (action 7: 46 once, then 45 looping) ----
                if (pose == Pose::Idle && animId == gp::kIdleBase[sp] && !enc && !digStep && ++idleTicks >= idleThreshold)
                    playAnim(gp::kSitDown[sp], false, 14, Pose::Special,
                             {{gp::kSettled[sp], true, sp == 0 ? 7 : 5}});

                // ---- advance the current animation (rate = frames per second, same rule as walking) ----
                if (pose != Pose::Move && pose != Pose::Wait)
                {
                    const int n = int(getAnim(animId).size());
                    if (n > 0)
                    {
                        ++animTicks;
                        const int idx = animTicks * animRate / 30;
                        if (animLoop)
                            animFrame = idx % n;
                        else if (idx >= n)
                        {
                            animFrame = animReverse ? 0 : n - 1;
                            animDone = true;
                        }
                        else
                            animFrame = animReverse ? n - 1 - idx : idx;
                    }
                    else if (!animLoop)
                        animDone = true; // nothing to show, don't get stuck

                    if (animDone)
                    {
                        if (!queue.empty())
                        {
                            const AnimStep s = queue.front();
                            std::vector<AnimStep> rest(queue.begin() + 1, queue.end());
                            playAnim(s.id, s.loop, s.rate, rest.empty() ? Pose::Idle : Pose::Settle, rest);
                        }
                        // don't drop to idle between steps
                        else if (!enc && !feedStep && !digStep)
                            playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle);
                    }
                }
                if (medStuck)
                {
                    ++walkTicks; // keep the walk loop animating in place
                    if (++medStuckTicks >= medStuckLimit)
                        playAnim(gp::kIdleBase[sp], true, 14, Pose::Idle); // playAnim clears medStuck
                }
                if (overId >= 0)
                    ++overTicks;
                tickFeed();
                tickDig();
                tickSwiftDrop();
                quests.tick(int(gameTicks));

                // ---- exit trigger / talk to NPC ----
                const gp::ObjectDef *nearObj = cur->runtime->nearestInteractable(pos.x, pos.y);
                const bool zWorld = zPressed && !petLocked;
                const int curArea = cur->def->legacyId;
                const gp::ShopEvent *shopItem = zWorld ? nearestShopItem() : nullptr;
                const int pickIdx = (zWorld && !shopItem) ? events.nearestPrompt(curArea, pos.x, pos.y) : -1;
                const gp::ExitDef *ie = (zPressed && nearObj && !shopItem && pickIdx < 0)
                                            ? db.findExit(cur->def->id, nearObj->id, false)
                                            : nullptr;
                const gp::ExitDef *ex = nearObj ? db.findExit(cur->def->id, nearObj->id, true) : nullptr;

                // debug: press O to dump the nearest interactable object
                static bool prevO = false;
                const bool oDown = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_O];
                if (oDown && !prevO)
                    for (uint32_t a = 0xe393; a < 0xe540; ++a)
                    {
                        const int id = rom.u16(a);
                        std::printf("%x: %d (0x%x) %s\n", a, id, id,
                                    db.textOr("base:text/" + std::to_string(id), "").c_str());
                    }
                prevO = oDown;

                if (ie)
                    requestGoTo(*ie);
                else if (!ex)
                    exitsArmed = true;
                else if (exitsArmed)
                    requestGoTo(*ex);

                const bool outdoors = curArea <= 9; // GUESS for the ROM's 0x3c969(area) check; interiors/shops can't dig

                if (zWorld && !ie && !enc)
                {
                    if (shopItem)
                        buyFromShop(*shopItem);
                    else if (pickIdx >= 0)
                        tryPickup(pickIdx);

                    else if (nearObj && interactions.interact(cur->def->id, *nearObj))
                    {
                    }
                    else if (gp::NpcActor *t = nearestNpc())
                        walk::talkToNpc(svc, *t);
                    else if (outdoors)
                        startDig(); // misses and duds play the no-item reaction
                }
            }

            renderAlpha = smoothCam ? float(std::min(acc * kTickHz, 1.0)) : 1.f;
            const float petFX = smoothAt(prevPos.x, pos.x), petFY = smoothAt(prevPos.y, pos.y); // pet's drawn position
            const float camX = clampCamF(petFX - W / 2, cur->img.w, W);
            const float camY = clampCamF(petFY - kCamY, cur->img.h, H);
            cur->runtime->updateVisible(int(std::floor(camX)), int(std::floor(camY)));

            // ---- DRAW ----
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_FRect dst{-camX, -camY, float(cur->img.w), float(cur->img.h)};
            SDL_RenderCopyF(ren, tex, nullptr, &dst);

            for (const gp::Hole &h : events.holes())
                if (h.area == cur->def->legacyId)
                    drawGroundSprite(gp::uiSpriteId(kHoleSprite, kEvPal), h.x, h.y, camX, camY);

            // Shadows first, so they lie on the ground under every sprite (layer 0x4000 in the original).
            for (int idx : cur->runtime->visible())
            {
                const gp::ObjectDef *o = cur->runtime->objects()[size_t(idx)];
                const int sprite = shadowFor(o->type);
                if (sprite >= 0)
                    drawShadowSprite(gp::uiSpriteId(sprite, 0x28c), o->x, o->y + 1, camX, camY);
            }
            for (const gp::NpcActor &n : npcs)
                drawNpcShadow(n, camX, camY);
            for (const gp::WorldItem &e : events.events())
                if (e.area == cur->def->legacyId && e.state == gp::EventState::Visible)
                    drawItemShadow(e.item, e.x, e.y, camX, camY);
            auto drawPet = [&]()
            {
                const bool faceRight = !(facing == 2 && faceLeft);
                const bool panda = sp == 2;
                const bool walking = pose == Pose::Move || pose == Pose::Wait;
                bool flip = false;
                const bool negClass = animId == gp::reactId(gp::ReactAnim::Negative, sp, 1, true) ||
                                      animId == gp::reactId(gp::ReactAnim::Negative, sp, 2, true) ||
                                      animId == gp::reactId(gp::ReactAnim::NegIntro, sp, 0, true);
                const bool sideClass = animId == gp::kIdleSide[sp] || animId == gp::kCallSide[sp] ||
                                       animId == gp::reactId(gp::ReactAnim::Positive, sp, 0, true) ||
                                       animId == gp::reactId(gp::ReactAnim::Emote, sp, 0, true) ||
                                       animId == gp::reactId(gp::ReactAnim::Shocked, sp, 0, true);
                if (walking)
                    flip = facing == 2 && faceLeft;
                else if (animId == gp::kTurn180[sp] || animId == gp::kTurn90[sp])
                    flip = !faceLeft;
                else if (negClass)
                    flip = panda ? !faceRight : faceRight; // same rule host.npc.play uses
                else if (sideClass)
                    flip = facing == 2 && gp::reactMirror(sp, faceRight);
                const AnimRef A = getAnim(walking ? kWalkAnim[facing] : animId);
                if (A.size() == 0)
                    return;
                const int n = int(A.size());
                const int f = walking ? (walkTicks * kFrameRate / 30) % n
                                      : std::min(animFrame, n - 1);
                const gp::SpriteFrame &s = A.anim->frames[f];
                drawShadow(s.w, petFX, petFY, sp == 2, camX, camY); // DrawEntitySpriteWithShadow

                const int ax = s.anchorX, ay = s.anchorY;
                float x;
                if (!flip)
                    x = petFX - camX - ax + s.dx;
                else if (flipMode == 0)
                    x = petFX - camX + ax - s.dx - s.w;
                else
                {
                    // hypothesis: record word 2 gives the flipped x; the owner x is the sprite's right edge
                    const float X = petFX - camX + s.dr;
                    x = ax != 0 ? X - s.w : X;
                }
                float y = petFY - camY - ay + s.dy;
                x += offX;
                y += offY;

                SDL_FRect r{x, y, float(s.w), float(s.h)};
                SDL_RenderCopyExF(ren, (*A.tex)[f], nullptr, &r, 0, nullptr,
                                  flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

                // marker 1 (magenta): the pet's position
                // SDL_SetRenderDrawColor(ren, 255, 0, 255, 255);
                // SDL_FRect dot{petFX - camX - 1, petFY - camY - 1, 3.f, 3.f};
                // SDL_RenderFillRectF(ren, &dot);

                // marker 2 (green): bottom-centre of the drawn sprite
                // SDL_SetRenderDrawColor(ren, 0, 255, 0, 255);
                // SDL_FRect dot2{x + s.w / 2 - 1, y + s.h - 1, 3.f, 3.f};
                // SDL_RenderFillRectF(ren, &dot2);
            };

            // One y-sorted pass: objects, NPCs and the pet all sort by their feet anchor y. At equal y, objects
            // draw first, then NPCs, then the pet (a guess; the game's tie order is not known).
            struct Item
            {
                int y;
                int kind; // 0 object, 1 npc, 2 pet
                const void *p;
            };
            std::vector<Item> items;
            for (int idx : cur->runtime->visible())
            {
                const gp::ObjectDef *o = cur->runtime->objects()[size_t(idx)];
                items.push_back({o->y, 0, o});
            }
            for (const gp::NpcActor &n : npcs)
                items.push_back({n.y(), 1, &n});

            for (const gp::WorldItem &e : events.events())
            {
                if (e.area != cur->def->legacyId)
                    continue;
                items.push_back({e.y, 4, &e}); // filter removed: drawEvent already checks the state
            }
            if (overId == 0x16)
                items.push_back({pos.y - 1, 5, nullptr}); // just behind the pet, sorted against objects
            items.push_back({pos.y, 2, nullptr});
            std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b)
                             { return a.y < b.y; });

            for (const gp::ShopEvent &e : pending.v)
                if (e.type == 3 && e.area == cur->def->legacyId)
                    items.push_back({e.y, 3, &e});
            for (const Item &it : items)
            {
                if (it.kind == 0)
                    drawObject(*static_cast<const gp::ObjectDef *>(it.p), camX, camY);
                else if (it.kind == 1)
                    drawNpc(*static_cast<const gp::NpcActor *>(it.p), camX, camY);

                else if (it.kind == 3)
                {
                    const auto &e = *static_cast<const gp::ShopEvent *>(it.p);
                    const AnimRef A = animRefById(gp::uiSpriteId(itemDefs[e.item].worldSprite, gp::kMenuPalette));
                    if (A.size())
                    {
                        const gp::SpriteFrame &s = A.anim->frames[0];
                        SDL_FRect r{e.x - camX - s.anchorX + s.dx, e.y - camY - s.anchorY + s.dy, float(s.w), float(s.h)};
                        SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
                    }
                }
                else if (it.kind == 4)
                    drawEvent(*static_cast<const gp::WorldItem *>(it.p), camX, camY);
                else if (it.kind == 5)
                {
                    const AnimRef A = animRefById("base:anim/" + std::to_string(overId) + "@28c");
                    if (A.size())
                    {
                        const int f = std::min(overTicks * kBubbleFps / 30, int(A.size()) - 1);
                        const gp::SpriteFrame &s = A.anim->frames[f];
                        const float ox = petFX - camX, oy = petFY - camY + digAnimDY;
                        SDL_FRect r{ox - s.anchorX + s.dx, oy - s.anchorY + s.dy, float(s.w), float(s.h)};
                        SDL_RenderCopyF(ren, (*A.tex)[f], nullptr, &r);
                    }
                }
                else
                    drawPet();
            }

            if (!dialogue.open() && !enc)
            {
                const int pi = events.nearestPrompt(cur->def->legacyId, pos.x, pos.y);
                if (pi >= 0)
                {
                    const gp::WorldItem &e = events.events()[size_t(pi)];
                    const AnimRef A = animRefById(reactAnimId(kPromptArrowAnim, kEvPal));
                    if (A.size())
                    {
                        const int f = int(SDL_GetTicks() * 18ull / 1000) % int(A.size());
                        const gp::SpriteFrame &s = A.anim->frames[f];
                        const float ax = e.x - camX, ay = e.y - camY - 24; // GUESS: height above the item
                        SDL_FRect r{ax - s.anchorX + s.dx, ay - s.anchorY + s.dy, float(s.w), float(s.h)};
                        SDL_RenderCopyF(ren, (*A.tex)[f], nullptr, &r);
                    }
                }
            }
            drawQuestHud(barBusy == 0 && feedStep == 0 && digStep == 0 && !im.open && !statsOpen, true);
            if (dialogue.open())
                drawDialogue(*dialogue.box);
            minigames.draw(renderAlpha);
            if (showStats)
            {
                constexpr int kRow = 16; // raise if letters still overlap
                struct Row
                {
                    const char *label;
                    int value;
                };
                const Row rows[] = {
                    {"HNGR", player.care[gp::Hunger]},
                    {"HLTH", player.care[gp::Health]},
                    {"???", player.care[gp::Unknown2]},
                    {"MOOD", player.care[gp::Happiness]},
                    {"SLPY", player.care[gp::Tiredness]},
                    {"HYGN", player.care[gp::Cleanliness]},
                    {"HALO", player.personality[gp::Halos]},
                    {"PTCH", player.personality[gp::Pitchforks]},
                    {"STAR", player.personality[gp::Stars]},
                    {"BULB", player.personality[gp::Lightbulbs]},
                };
                constexpr int n = int(sizeof rows / sizeof rows[0]);

                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
                SDL_Rect bg{4, 4, 110, n * kRow + 8};
                SDL_RenderFillRect(ren, &bg);

                int y = 8;
                for (const Row &r : rows)
                {
                    text.draw(r.label, 8, y, 70, gp::Align::Left);
                    numbers.draw(std::to_string(r.value), 80, y, 30, gp::Align::Right);
                    y += kRow;
                }
            }
            if (im.open)
            {
                SDL_RenderCopy(ren, bgTex, nullptr, nullptr);
                menuCmds.clear();
                gp::drawSlotScene(itemGfx, rom, itemSlot);
                const std::vector<int> win = itemWindow();
                gp::GridView v;
                v.list = &win;
                v.count = int(win.size());
                v.firstRow = im.firstRow;
                v.selRow = im.selRow;
                v.selCol = im.selCol;
                v.totalRows = itemRows();
                v.category = im.category;
                gp::drawItemGrid(itemGfx, itemDefs, v, ownedCount, itemName, emptyText);
                gp::drawTabMarker(itemGfx, rom, im.tab, false, im.sub, -1);
                std::stable_sort(menuCmds.begin(), menuCmds.end(), [](const Cmd &a, const Cmd &b)
                                 { return a.layer < b.layer; });
                for (Cmd &c : menuCmds)
                    c.draw();
            }
            if (statsOpen)
            {
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                SDL_RenderClear(ren);                                    // new: wipe the world drawing
                SDL_SetTextureBlendMode(statsBgTex, SDL_BLENDMODE_NONE); // new: ignore the texture's alpha
                SDL_RenderCopy(ren, statsBgTex, nullptr, nullptr);
                menuCmds.clear();
                auto rs = [&](int a)
                { return readRomString(rom, a); };
                gp::StatsView v;
                v.species = playerSpecies;
                v.petPalette = petPal;
                v.name = player.name;
                v.tricksKnown = player.tricksKnown;
                v.healthy = player.isHealthy();
                for (int i = 0; i < 4; ++i)
                    v.personality[i] = player.personality[i];
                v.hunger = gp::statLabel(rom, gp::kHungerLabels, player.care[gp::Hunger], rs);
                v.happiness = gp::statLabel(rom, gp::kHappinessLabels, player.care[gp::Happiness], rs);
                v.cleanliness = gp::statLabel(rom, gp::kCleanlinessLabels, player.care[gp::Cleanliness], rs);
                v.health = gp::statLabel(rom, gp::kHealthLabels, player.care[gp::Health], rs);
                v.tiredness = gp::statLabel(rom, gp::kTirednessLabels, player.care[gp::Tiredness], rs);
                v.illness = gp::kIllnessNames[std::clamp(player.illnessId, 0, 9)];

                std::string qt;
                const int qn = quests.statusText(qt);
                v.questText = qt;
                v.questPortrait = -1;
                if (const gp::NpcDef *d = npcAt(qn); d && d->portraitSprite > 0 && d->portraitSprite != 0xffff)
                {
                    v.questPortrait = d->portraitSprite;
                    v.questPortraitPal = d->portraitPalette;
                }
                gp::drawPetStats(itemGfx, rom, v, "Sick");
                std::stable_sort(menuCmds.begin(), menuCmds.end(), [](const Cmd &a, const Cmd &b)
                                 { return a.layer < b.layer; });
                for (Cmd &c : menuCmds)
                    c.draw();
            }
            if (barAnim.visible && !im.open && !statsOpen)
            {
                const AnimRef A = barAnimRef(barAnim.id);
                if (A.size())
                {
                    const int f = std::min(barAnim.frame, int(A.size()) - 1);
                    const gp::SpriteFrame &s = A.anim->frames[f];
                    SDL_Rect r{0 - s.anchorX + s.dx, 0xb4 - s.anchorY + s.dy, s.w, s.h}; // screen space, no camera
                    SDL_RenderCopy(ren, (*A.tex)[f], nullptr, &r);
                }
            }
            if (bar.isOpen() && barBusy == 2)
                gp::drawBarCursor(barGfx, rom, barInHouse, bar.selected(), barHost.pressed);
            static bool prevFullUi = false; // or a local declared before the loop
            const bool fullUi = im.open;
            if (fullUi != prevFullUi)
            {
                prevFullUi = fullUi;
                if (!pendingExit)
                {
                    fade.a = 0.8; // quick fade in from near-black on both open and close
                    fade.go(0.0, 6.0 / 30);
                }
            }
            {
                const Uint64 t = SDL_GetPerformanceCounter();
                fade.step(std::min(double(t - fadeLast) / double(freq), 1.0 / 20)); // clamp so loadArea hitches don't skip the fade
                fadeLast = t;
                if (fade.a > 0.0)
                {
                    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, Uint8(255.0 * std::min(fade.a, 1.0)));
                    SDL_RenderFillRect(ren, nullptr);
                }
            }
            SDL_RenderPresent(ren);
        }
        saveGame();
        SDL_RenderSetIntegerScale(ren, oldIntScale); // the renderer outlives this run: hand it back as we found it
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