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
#include "../game/item_tabs.hpp"
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
        int petPal = (slotInfo.full && slotInfo.appearance >= 0) ? slotInfo.appearance : speciesDef->defaultPalette; // washing can change it
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

        for (int a : {0xe, 0x10, 0x11, 0x12, 0x16, 0x17, 0x18, gp::kTabAnimIn, gp::kTabAnimOut, gp::kStinkAnim, gp::kExclaimIntroAnim, gp::kExclaimOutroAnim, gp::kTrickQuestionBubble, gp::kTrickAnswerBubble, gp::kHaloBubble, gp::kPitchforkBubble, gp::kStarBubble}) // 0xe: the wash bubble; arrow, dig bubble; 0x17/0x18: the thought bubble
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
                if (sprite <= 0 || sprite == gp::kNoSprite)
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
        auto addAll = [&](const int (&t)[3])
        { petIds.push_back(t[sp]); };
        addAll(gp::kWalkUp);
        addAll(gp::kWalkDown);
        addAll(gp::kWalkSide);
        addAll(gp::kWalkSideStyle);
        addAll(gp::kSitDown);
        addAll(gp::kSitAlt);
        addAll(gp::kEat);
        addAll(gp::kBowlBite);
        addAll(gp::kEatStyle);
        addAll(gp::kActionStyle);
        addAll(gp::kPositiveStyle);
        if (sp == 2)
            petIds.push_back(gp::kPandaFrameStyle);
        addAll(gp::kBoredStyle);
        addAll(gp::kBedGetIn);
        addAll(gp::kSleepCycle);
        addAll(gp::kBedGetOut);
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

        for (int level = 1; level <= 3; ++level) // the tricks (trick_showcase.hpp)
            for (int id : {gp::TrickShowcase::introAnim(level, sp), gp::TrickShowcase::mainAnim(level, sp), gp::TrickShowcase::closingAnim(level, sp)})
                if (id >= 0)
                    petIds.push_back(id);
        if (sp == 2)
            for (int n : {2, 3, 4})
                petIds.push_back(gp::TrickShowcase::reactionAnim(n));
        petIds.push_back(gp::TrickShowcase::turnAroundAnim(sp));
        petIds.push_back(gp::TrickShowcase::directionalAnim(sp));
        petIds.push_back(gp::TrickShowcase::actorReactionAnim(sp));
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
            else if (key == 7)
                w = &n.def->idleSideAnim;
            else if (key == 8)
                w = &n.def->sickIdleAnim;
            else if (key == 9)
                w = &n.def->sadIdleAnim;
            else if (key == 6)
                return animRefById(n.forcedAnimId);
            return animRefById(w->empty() ? n.def->anim : *w);
        };
        auto updateNpcAnim = [&](gp::NpcActor &n)
        {
            const int key = n.animKey();
            const int frames = int(npcAnim(n, key).size());
            // Keys 0..3, 7, 8 and 9 (idles and walks) loop; 4/5 (turns) and 6 (reactions) are one-shots. The sick and sad idles
            // run at 7 fps (the dog's sick one at 14), like the pet's.
            const int npcSp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
            const int rate = key == 6 ? n.forcedRate : key == 9 || (key == 8 && npcSp != 0) ? 7 : 14;
            n.setAnim(key, frames, rate, key < 4 || key >= 7, key == 6 && n.forcedReverse);
            n.advanceAnim(frames);
        };
        int stinkTicks = 0; // the stink cloud's animation clock (the pet's and the NPCs')
        gp::StatNotice statNotice; // the panel that drops from the top right when money or a stat changes
        player.pendingMoney = 0;   // what was set up before the world started (starting money, a load) is not news
        itemHost.sink = [&](int counter, int amount)
        { statNotice.add(counter, amount); };
        int holdTicks = 0;                // a quest reward activity is holding the pet (sim steps left), 0 = none
        gp::NpcActor *holdNpc = nullptr;  // the NPC held with it (SetAnimalEncounterLockFlag), released when the hold ends
        std::function<void()> holdDone;   // what runs when the hold ends
        gp::StinkTransfer stinkXfer; // how long the pet has stood beside a stinky NPC / been stinky beside a clean one
        auto drawNpc = [&](const gp::NpcActor &n, float camX, float camY)
        {
            const AnimRef A = npcAnim(n, std::max(n.anim.animId, 0));
            if (A.size() == 0)
                return;
            const int fr = std::min(n.anim.currentFrame, int(A.size()) - 1);
            const gp::SpriteFrame &s = A.anim->frames[fr];
            const int key = n.animKey();
            // Key 7 (idle toward the pet): dog and monkey art faces left, panda art faces right (PlayIdleAnim_Scripted).
            const int npcSp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
            const bool sideIdleFlip = n.body.facing == (npcSp == 2 ? gp::FaceLeft : gp::FaceRight);
            const bool flip = key == 3                 ? n.body.facing == gp::FaceLeft
                              : (key == 4 || key == 5) ? n.body.facing == gp::FaceRight
                              : key == 6               ? n.forcedFlip
                              : key == 7               ? !n.def->idleSideAnim.empty() && sideIdleFlip
                                                       : false;
            const auto np = npcPosF(n);
            const float x = flip ? np.first - camX + s.anchorX - s.dx - s.w
                                 : np.first - camX - s.anchorX + s.dx;
            SDL_FRect r{x, np.second - camY - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
            SDL_RenderCopyExF(ren, (*A.tex)[fr], nullptr, &r, 0, nullptr,
                              flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
            if (n.overheadTick >= 0 && n.def->stationary) // its own overhead animation, over the NPC
            {
                const AnimRef B = animRefById(reactAnimId(n.def->behaviorFlags, n.def->appearancePalette));
                if (B.size())
                {
                    const int bf = std::min(n.overheadTick * gp::kOverheadFps / 30, int(B.size()) - 1);
                    const gp::SpriteFrame &bs = B.anim->frames[size_t(bf)];
                    SDL_FRect br{np.first - camX - bs.anchorX + bs.dx, np.second - camY - bs.anchorY + bs.dy + 1, float(bs.w), float(bs.h)};
                    SDL_RenderCopyF(ren, (*B.tex)[size_t(bf)], nullptr, &br);
                }
            }
            if (n.stinky && !n.def->stationary) // SpawnMessageBubble(6, ...): the stink cloud loops over a stinky NPC
            {
                const AnimRef K = animRefById("base:anim/" + std::to_string(gp::kStinkAnim) + "@28c");
                if (K.size())
                {
                    const int dir = key == 3 ? (n.body.facing == gp::FaceLeft ? 0 : 1) : 2;
                    const gp::BubbleOffset bo = gp::overheadOffset(std::clamp(npcSp, 0, 2), dir, 0);
                    const int f = (stinkTicks * gp::kStinkFps / 30) % int(K.size());
                    const gp::SpriteFrame &ks = K.anim->frames[size_t(f)];
                    SDL_FRect kc{np.first - camX + bo.dx - ks.anchorX + ks.dx,
                                 np.second - camY + bo.dy + gp::bubbleLower(std::clamp(npcSp, 0, 2)) - ks.anchorY + ks.dy, float(ks.w), float(ks.h)};
                    SDL_RenderCopyF(ren, (*K.tex)[size_t(f)], nullptr, &kc);
                }
            }
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
        gp::QuestNudge nudge;           // "Go see Mayor Peter!" (quest_nudge.hpp)
        nudge.restart(long(gameTicks));
        const gp::NpcDef *mayorDef = nullptr; // NPC 0x42, set once the NPCs are indexed
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
        // A player home or hotel (game/room_scene.hpp): while this exists the world is frozen and the room is shown. The
        // pet is drawn with the indoor ("style") art and only walks left and right along the floor.
        struct RoomView
        {
            enum class Phase
            {
                Entering, // from the door, down and across to the floor line
                Idle,
                Walking,
                Settling, // stopped: the walk keeps playing for 0.15 s
                Turning,  // the 90 degree turn back to the front
                Leaving,  // along the floor, then up and across to the door
                Placing,  // choosing where a furnishing goes (the pet isn't shown)
                Placed,   // the item is down: the room stays up for a moment, then fades back to the item screen
                Eating    // at the food bowl (see eatStep)
            };
            int location = 0;
            SDL_Texture *tex = nullptr;
            int x = gp::kRoomDoorX, y = gp::kRoomDoorY; // the pet, in room coordinates
            int scroll = 0;
            int px = gp::kRoomDoorX, py = gp::kRoomDoorY, pscroll = 0; // x, y and scroll before the latest tick (smooth mode)
            int pcurX = 0, pcurY = 0;                                   // the placing cursor before the latest tick
            Phase phase = Phase::Entering;
            bool faceLeft = false;
            int walkTicks = 0, settleTicks = 0, animTicks = 0;
            // Another save slot's home: its pet sits in the room (hasVisitor), or the slot is unused (forRent).
            bool hasVisitor = false, forRent = false;
            int visitSpecies = 0, visitPal = 0, visitTicks = 0;
            std::vector<gp::RoomDecoration> table; // the room's fixed pieces: door 0, bed 1, food bowl 4, ...
            int nearby = -1;                       // the table entry the pet is next to (g_nearbyDecorationIndex)
            gp::SlotItems slots{};   // the 32 furnishing slots this room shows
            gp::RoomSprites sprites; // door, bed, furniture, wall pieces and the equipped items, in layers
            // Placing a furnishing (room_scene.hpp): the item, the slot the cursor is on, the cursor's position, and whether
            // Z has been pressed (the item is put down once the cursor arrives).
            int placeItem = 0, placeSlot = 0, curX = 0, curY = 0;
            bool placeConfirmed = false;
            // Eating at the bowl. Steps follow Pet_RepeatedTrickWithDialogue: 6 walk along the floor, 7 up the diagonal, 8 the
            // panda's intro, 9 lean in for a bite, 10 chew, 11 pause, 12 back down, 13 turn to the front, 14 happy.
            int eatStep = 0, eatRepeat = 0, eatChews = 0, eatPause = 0, eatHappy = 0, eatSX = 0, eatAnim = -1;
            bool eatFlip = false, eatLoop = false;
            bool eatGivesHappy = true;      // the bowl's +6 Happiness at the end; not for a cure
            bool eatWash = false;           // a wash or a sleep: the +10 Happiness top-up at the end (step 20 is the bubble)
            bool sleepQuit = false;         // sleeping to quit the game: no stat changes, and no waking up
            int eatFps = 14;                // the frame rate of eatAnim (the yawn and the sleep run slower)
            bool eatReverse = false;        // eatAnim plays last frame to first (the panda's outro)
            int tutorialEvent = -1;         // a first-time message in progress (steps 40 turn to the front, 41 wait, then show it)
            // Sleeping, steps 30 yawn, 31 along the floor, 32 up the diagonal to the bed, 33 get in, 34 sleep cycles, 35 get out
            // (then 12, 13, 14 as after the bowl).
            int overId = -1, overTicks = 0; // the animation over the pet (the cure's rainbow bubbles), -1 = none
            RoomView() = default;
            RoomView(const RoomView &) = delete;
            RoomView &operator=(const RoomView &) = delete;
            ~RoomView()
            {
                if (tex)
                    SDL_DestroyTexture(tex);
            }
        };
        std::unique_ptr<RoomView> room;
        std::function<bool(int)> roomActivity; // the house bar's activities that are built for rooms (assigned below)
        std::function<void(int)> goHome;       // the outdoor bar's last button: to the door of this save's house (assigned below)
        std::function<void()> roomQuit;        // the quit button's Yes while in a room: sleep, then leave the game
        int roomMenuCategory = 0;              // the item screen's tab when a furnishing was chosen, to come back to it
        // FindEligibleNPCForEncounter (0x03d27a): an NPC can be talked to when it is on screen and the pet is inside the
        // box around it: strictly less than 0x3c (60) px away on BOTH axes, so a 120 x 120 square, not a circle.
        // When several qualify the LAST one in instance order wins (the ROM keeps overwriting its selection).
        auto nearestNpc = [&]() -> gp::NpcActor *
        {
            constexpr int kTalkBox = 0x3c;
            const float camXl = clampCamF(float(pos.x) - W / 2, cur->img.w, W);
            const float camYl = clampCamF(float(pos.y) - kCamY, cur->img.h, H);
            gp::NpcActor *pick = nullptr;
            for (gp::NpcActor &n : npcs)
            {
                const float sx = float(n.x()) - camXl, sy = float(n.y()) - camYl;
                if (sx < 0 || sx >= W || sy <= 0 || sy > H) // not on screen: the ROM skips it entirely
                    continue;
                if (std::abs(n.x() - pos.x) < kTalkBox && std::abs(n.y() - pos.y) < kTalkBox)
                    pick = &n;
            }
            return pick;
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

        auto drawUiF = [&](int spriteId, float x, float y, int pal = gp::kDialoguePalette) // drawUi at a sub-pixel position
        {
            const AnimRef A = animRefById(gp::uiSpriteId(spriteId, pal));
            if (A.size() == 0)
                return;
            const gp::SpriteFrame &s = A.anim->frames[0];
            SDL_FRect r{x + s.dx, y + s.dy, float(s.w), float(s.h)};
            SDL_RenderCopyF(ren, (*A.tex)[0], nullptr, &r);
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

            if (b.kind == gp::BoxKind::Picture) // the island map (RenderActiveDialogueWindow, type 6)
            {
                // Colour 0 of the map is see-through; behind it is the screen's backdrop, not the world.
                gp::clearFrame(ren); // the whole screen: the border around the picture in 16:9
                drawUi(gp::kMapTile, 0, 0, gp::kMapPalette);
                drawUi(gp::kMapMarkerTile, b.markX, b.markY, gp::kMapMarkerPalette);
                return;
            }
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

            if (b.kind == gp::BoxKind::Quest) // DrawQuestObjectiveScreen: everything at fixed screen positions
            {
                auto str = [&](const char *k)
                { return db.textOr(std::string("base:str/quest/") + k); };
                int y = 0x32;
                text.draw(b.questHeading, 100, y, 0xfa, gp::Align::Left);
                y += 0x10;
                for (const auto &row : b.questRows)
                {
                    numbers.draw(std::to_string(row.first), 0x6e, y, 0x14, gp::Align::Left);
                    numbers.draw(row.second, 0x7d, y, 0xaf, gp::Align::Left);
                    y += 0xc;
                }
                y += 8;
                text.draw(str("objective_bring"), 100, y, 0xfa, gp::Align::Left);
                y += 0x10;
                numbers.draw(b.questBringTo, 0x6e, y, 0xaf, gp::Align::Left);
                y += numbers.layout(b.questBringTo, 0x6e, y, 0xaf, gp::Align::Left).height + 3;
                if (!b.questLookIn.empty())
                {
                    text.draw(str("objective_look"), 100, y, 0xfa, gp::Align::Left);
                    y += 0x10;
                    numbers.draw(b.questLookIn, 0x6e, y, 0xaf, gp::Align::Left);
                }
                drawUi(0x27, L.x + L.btnX, L.y + L.btnY);
                return;
            }

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
            std::function<bool(int, int, int, int)> onActivity; // true = handled (0x27: wash)
            void startPetActivity(int id, int prm, int x, int y) override
            {
                if (onActivity && onActivity(id, prm, x, y))
                    return;
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
            std::function<void(int)> onDialogue;
            void startDialogue(int id) override
            {
                if (onDialogue)
                    onDialogue(id);
                else
                    std::printf("bar: dialogue 0x%x\n", id);
            }
            std::function<void()> onTricks;
            void openTricksPanel() override
            {
                if (onTricks)
                    onTricks();
            }
            int currentSaveSlot() override { return slot; }
            void requestRedraw() override {}
            void startCloseAnim() override { play(inHouse ? 1 : 3); } // CloseMenuBar()
            void playOpenAnim(int) override {}
            void playSound(int id, int vol) override { gp::audio().playSfx(id, vol); }
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
            if (gp::QuestNudge::applies(player.questsCompleted, quests.q.phase))
                nudge.restart(long(gameTicks)); // Game_ProcessTick: an area change restarts the reminder
            if (const int song = gp::rau::zoneSong(a.legacyId); song >= 0)
                gp::audio().playMusic(song); // Sound_LookupAndStart via RemapZoneIndexToAudioId; other areas keep the current song
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
                    sfx(0x14, 200);
            }
            else if (id == 0x10a)
            {
                player.adjustMoney(randIn(20, 51));
                if (sound)
                    sfx(0x14, 200);
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
                            sfx(0x14, 200);
                    }
                    else // r = 2..5 -> stat 0..3
                        personalityDelta(r - 2, randIn(1, 10));
                    if (sound)
                        sfx(0x53, 200);
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
                        sfx(1, 200);
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
                    sfx(0x53, 200);
            }
            return true;
        };

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
        int shakeDy = 0;
        BarAnim panelAnim;
        bool panelClosing = false;
        int potionStep = 0, exclaimTicks = -1; // a colour potion's reaction; the exclamation bubble's clock (-1 = none)
        int colorStep = 0, colorIdx = 0, colorDir = 0, colorTicks = 0; // the spaceship colour machine
        std::unique_ptr<gp::ReactionSeq> potionSeq;
        long tiredDecayAccum = 0, hungerDecayAccum = 0; // the stat decay counts (thought_bubble.hpp)
        int thoughtIcon = -1, thoughtTicks = 0; // the bubble up over the pet (icon tile), -1 = none

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
                    sfx(0x53, 200); // GrantItemOrEvent: PlaySoundEffect(0x53, 200) when the pickup asks for a sound
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
            if (o.result == gp::PickupResult::Granted)
                afterPickup = true; // ProcessStoryModeObjectInteraction: IsFirstTimeEvent(0xf) right after the item is granted
            if (o.result == gp::PickupResult::NeedItem)
                dialogue.show(gp::messageBox("You need " + itemName(o.needItem) + " first.")); // placeholder text
            else if (o.result == gp::PickupResult::AtCapacity)
                dialogue.show(gp::messageBox("You can't carry any more.")); // placeholder text
        };

        // The map-marker arrows (anims 0x10 / 0x11 / 0x12, 18 fps) are in icon_selector.hpp.
        constexpr int kEvPal = 0x28c, kFlowerSprite = 0x9c1, kHoleSprite = 0x9c2;

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
            const AnimRef A = animRefById(uiIdP(itemDefs[item].worldSprite, gp::kMenuPalette));
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
                drawWorldSprite(uiIdP(itemDefs[e.item].worldSprite, gp::kMenuPalette), e.x, e.y, camX, camY);
        };

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

        auto buildFiltered = [&](int category)
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

        auto tabAnimId = [&](bool in)
        { return gp::Id(std::string("base:anim/") + std::to_string(in ? gp::kTabAnimIn : gp::kTabAnimOut) + "@28c"); };
        // ItemMenuScreen's input: the grid (Menu opens the tab list), the tab list (Up/Down, Select on release, Cancel/Menu back), and
        // nothing while the list slides.
        auto itemMenuTick = [&](uint8_t pressed, bool xPress, bool menuPress, bool zPress, bool zRelease)
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
            // Not while the pet is busy with something: feeding, digging, a swift-pick drop, or a wash.
            if (S.pet.feedStep != 0 || S.pet.digStep != 0 || S.pet.swiftDropStep != 0 || S.pet.washStep != 0 || showNpc || tutStep != 0 || potionStep != 0 || barkStep != 0 || trickStep != 0 || questionStep != 0 || colorStep != 0 || holdTicks > 0)
                return;
            if (room) // in a room it is always the house bar, and not while the pet is walking in or out or the item is placed
            {
                using P = RoomView::Phase;
                if (room->phase == P::Entering || room->phase == P::Leaving || room->phase == P::Placing || room->phase == P::Placed ||
                    room->phase == P::Eating)
                    return;
                house = true;
            }
            barBusy = 1;
            barInHouse = house;
            bar.open(house);
            gp::audio().playSfx(menubar::kSfxOpen, menubar::kSfxOpenVol); // OpenMenuBar: PlaySoundEffect(0x59, 0xbe)
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
            if (s >= 0xb && s <= 0xd && goHome) // MapLocationToGameState(slot): Go Home
            {
                goHome(s - 0xb);
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
            return !player.isHealthy()                                    ? gp::kIdleFlagB[sp]
                   : player.care[gp::Happiness] < gp::kUnhappyBelow ? gp::kIdleFlagA[sp]
                                                                       : gp::kIdleBase[sp];
        };
        auto restIdleRate = [&] { return restIdleId() == gp::kIdleBase[sp] || (restIdleId() == gp::kIdleFlagB[sp] && sp == 0) ? 14 : 7; };
        auto isRestIdle = [&](int aid) { return aid == gp::kIdleBase[sp] || aid == gp::kIdleFlagA[sp] || aid == gp::kIdleFlagB[sp]; };

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
        host.event = [&](const char *k, int v)
        {
            std::printf("event %s 0x%x\n", k, v);
            if (std::strcmp(k, "sound") == 0)
                sfx(v); // PlaySoundEffect(0x54, 200) when a friend is made
        };
        host.petY = [&]
        { return pos.y; };
        host.spawnItem = [&](int item, int x, int y) // SpawnPendingEvent: visible, not persistent, category-flagged
        { events.placeAt(cur->def->legacyId, item, x, y, false); };

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
            animReverse = gp::playsReversed(a);
            if (animReverse)
                animFrame = n - 1;
            return true;
        };

        host.pet.busy = [&]
        { return !animLoop && !animDone; };
        host.pet.sound = [&](int c, int vol)
        { gp::audio().playSfx(gp::petVoiceId(playerSpecies, c), vol < 0x20 ? 0xbe : vol); }; // PlaySoundFromCategory(species, c, vol); see host.npc.sound for vol 4
        host.pet.bubble = [&](int id)
        {
            if (id == gp::kExclaimIntroAnim) // the exclamation over the head (SpawnMessageBubble 0xb, phase 4)
                exclaimTicks = 0;
            else
                std::printf("bubble 0x%x\n", id);
        };
        host.pet.rand = [&](int lo, int hi)
        { return hi <= lo ? lo : lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive

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
                    playAnim(gp::kTurn180[sp], false, 14, Pose::Settle, {{gp::kTurn90[sp], false, 14}, {restIdleId(), true, restIdleRate()}});
                }
                else
                    playAnim(gp::kTurn90[sp], false, 14, Pose::Settle, {{restIdleId(), true, restIdleRate()}});
                settlePhase = 1;
                return false;
            }
            if (pose == Pose::Idle && isRestIdle(animId))
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
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
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
        { return hi <= lo ? lo : lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive
        bool npcMute = false; // an idle NPC off screen makes no sound
        host.npc.sound = [&](int c, int vol)
        {
            if (npcMute)
                return;
            const int s = encNpc->def->speciesOrAnim == 0xfffe ? playerSpecies : encNpc->def->speciesOrAnim;
            // The idle noise's one odd call passes 4 where the others pass 0xbe; at volume 4 it is inaudible and the first
            // sound of a meeting sometimes vanished, so it plays at the usual level (GUESS: 4 isn't a volume).
            gp::audio().playSfx(gp::petVoiceId(s <= 2 ? s : playerSpecies, c), vol < 0x20 ? 0xbe : vol);
        };
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
            // Outros play last frame to first, same as the pet's (host.pet.play); the ROM shares one routine for both.
            encNpc->forcedReverse = gp::playsReversed(a);
            const bool neg = a == gp::ReactAnim::Negative || a == gp::ReactAnim::NegIntro ||
                             a == gp::ReactAnim::NegOutro;
            const bool panda = sp == 2;
            if (neg)
                encNpc->forcedFlip = panda ? (f == gp::FaceLeft) : (f != gp::FaceLeft);
            else
                encNpc->forcedFlip = side && gp::reactMirror(sp, f == gp::FaceRight);

            return true;
        };

        // PlaySoundFromCategory(species, category, volume): the pet's own voice (pet_voice.hpp)
        auto petSound = [&](int category, int vol)
        { gp::audio().playSfx(gp::petVoiceId(sp, category), vol); };
        auto playFeedHappy = [&]
        {
            // (3, 2) in the original: 3 = repeat count, 2 = probably the variant. Try 0 if the art looks wrong.
            petSound(4, 0xbe); // PlayPositiveReactionAnim: the happy panting
            if (!host.pet.play(gp::ReactAnim::Positive, 2))
                animDone = true; // no art: let the sequence move on
        };
        // TickActor_RepeatAnimThenPause: each time the eating animation starts, category 6, or 5 one time in three, at 0xbe
        auto chewSound = [&]
        { petSound(std::rand() % 3 == 0 ? 5 : 6, 0xbe); };

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
            petSound(0xa, 0xff); // HandleCureIllnessEncounter: the gulp
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
                chewSound();
            }
            else if (feedStep == 2)
            {
                if (++feedCount <= feedRepeats)
                {
                    playAnim(gp::kEat[sp], false, 14, Pose::Special);
                    chewSound();
                }
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
                    playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                }
            }
        };

        // ---- first-time messages (TickFirstNPCEncounterSequence, activity 0x26); see settings.hpp ----
        auto startWorldTutorial = [&](int event)
        {
            tutEvent = event;
            tutStep = 1;
            settlePhase = 0;
            walkTicks = 0;
        };
        auto tickWorldTutorial = [&]
        {
            if (tutStep == 1) // the pet turns to face the front (TickActor_SettleFacing)
            {
                if (!host.pet.settle())
                    return;
                if (!tutNudge)
                {
                    player.tutorialsShown = gp::markShown(player.tutorialsShown, tutEvent); // marked shown as it begins
                    saveGame();
                }
                // HandleNPCInteractionInput: the idle that fits how the pet is (sick, unhappy, or ordinary)
                facing = 1;
                walkTicks = 0;
                if (!player.isHealthy())
                    playAnim(gp::kIdleFlagB[sp], true, sp == 0 ? 14 : 7, Pose::Idle);
                else if (player.care[gp::Happiness] < gp::kUnhappyBelow)
                    playAnim(gp::kIdleFlagA[sp], true, 7, Pose::Idle);
                else
                    playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                tutTicks = 0;
                tutStep = 2;
            }
            else if (tutStep == 2 && ++tutTicks >= gp::kTutorialWaitTicks) // 0.25 s, then the message
            {
                tutStep = 0;
                if (tutNudge) // ShowOneTimeEventMessage(-1): the message with the Mayor's portrait
                {
                    tutNudge = false;
                    gp::DialogueBox nb = gp::messageBox(readRomString(rom, gp::kNudgeMessageAddr));
                    if (mayorDef)
                        nb.portrait = gp::uiSpriteId(mayorDef->portraitSprite, mayorDef->portraitPalette);
                    dialogue.show(std::move(nb));
                }
                else
                    dialogue.show(gp::messageBox(db.textOr("base:text/" + std::to_string(gp::tutorialText(tutEvent)))));
                tutEvent = -1;
            }
        };

        // ---- the hungry / tired bubble (TickCareStatWatchdog conditions 4 and 6); see thought_bubble.hpp ----
        auto tickThoughtBubble = [&]
        {
            const bool walking = pose == Pose::Move;
            const bool calm = tutStep == 0 && !enc && !dialogue.open();
            ++stinkTicks;
            // the stats wear down (conditions 1 and 2)
            if (gp::decayStep(tiredDecayAccum, gp::kTiredDecayLimit, player.questsCompleted))
            {
                player.adjustCare(gp::Tiredness, -gp::tiredDecayAmount(sp));
                tiredTimer.accum = tiredTimer.limit - gp::kThoughtRetryTicks;
            }
            if (gp::decayStep(hungerDecayAccum, gp::kHungerDecayLimit, player.questsCompleted))
            {
                const int before = player.care[gp::Hunger];
                player.adjustCare(gp::Hunger, -gp::hungerDecayAmount(sp));
                if (gp::crossedHungry(before, player.care[gp::Hunger]))
                    hungerTimer.accum = hungerTimer.limit - gp::kThoughtRetryTicks;
            }
            if (thoughtIcon < 0)
            {
                if (gp::hungerBubbleStep(hungerTimer, player.care[gp::Hunger], walking, false))
                    thoughtIcon = gp::kThoughtHungerIcon;
                else if (gp::sickBubbleStep(sickTimer, !player.isHealthy(), walking, false))
                    thoughtIcon = gp::kThoughtSickIcon;
                else if (gp::tiredBubbleStep(tiredTimer, player.care[gp::Tiredness], walking, false))
                    thoughtIcon = gp::kThoughtTiredIcon;
                else if (gp::moodBubbleStep(moodTimer, player.care[gp::Happiness], walking, false))
                    thoughtIcon = gp::kThoughtMoodIcon;
                thoughtTicks = 0;
                return;
            }
            ++thoughtTicks;
            // the timers keep counting while a bubble is up, but cannot add another
            gp::hungerBubbleStep(hungerTimer, player.care[gp::Hunger], walking, true);
            gp::tiredBubbleStep(tiredTimer, player.care[gp::Tiredness], walking, true);
            gp::sickBubbleStep(sickTimer, !player.isHealthy(), walking, true);
            gp::moodBubbleStep(moodTimer, player.care[gp::Happiness], walking, true);
            if (thoughtTicks < gp::kThoughtShowTicks)
                return;
            const AnimRef G = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimGrow) + "@28c");
            const AnimRef H = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimHold) + "@28c");
            if (!gp::thoughtView(thoughtTicks, int(G.size()), int(H.size())).done)
                return;
            const int event = calm ? gp::thoughtTutorial(settings.tutorials, player.tutorialsShown, thoughtIcon, pose == Pose::Idle) : -1;
            thoughtIcon = -1;
            if (event >= 0)
                startWorldTutorial(event);
        };

        // ---- a colour potion's reaction (activity 0x13, TickActor_ShockedEncounterSequence); see potion.hpp ----
        // Step 1: turn to face front. Step 2: the emote reaction with the exclamation bubble over the pet, then the shocked one.
        auto startPotionReaction = [&]
        {
            potionStep = 1;
            settlePhase = 0;
            walkTicks = 0;
            potionSeq = std::make_unique<gp::ReactionSeq>(gp::ReactionSeq::Kind::Emote);
        };
        auto tickPotion = [&]
        {
            if (exclaimTicks >= 0)
                ++exclaimTicks;
            if (potionStep == 1)
            {
                if (host.pet.settle())
                    potionStep = 2;
            }
            else if (potionStep == 2 && potionSeq && potionSeq->tick(host.pet))
            {
                potionSeq.reset();
                potionStep = 0;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            }
        };

        // ---- X in the overworld (HandlePetStageEvents event 5) ----
        // Standing still: activity 0xc settles the pet to face front, then TickActor_IdleRandomSound with a count of 1, the
        // species' call (category 1 or 2). Walking: just category 3 at full volume, unless it is already sounding.
        auto pressCancelInWorld = [&]
        {
            if (pose == Pose::Idle)
            {
                barkStep = 1;
                settlePhase = 0;
                walkTicks = 0;
                barkSeq = std::make_unique<gp::ReactionSeq>(gp::ReactionSeq::Kind::Noise, 1);
            }
            else if (pose == Pose::Move && !gp::audio().sfxActive(gp::petVoiceId(sp, 3)))
                petSound(3, 0xff);
        };
        auto tickBark = [&]
        {
            if (barkStep == 1)
            {
                if (host.pet.settle())
                    barkStep = 2;
            }
            else if (barkStep == 2 && barkSeq && barkSeq->tick(host.pet))
            {
                barkSeq.reset();
                barkStep = 0;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            }
        };

        // ---- the tricks panel (trick_panel.hpp) ----
        barHost.onTricks = [&]
        {
            barBusy = 5; // HandleTrickAction: the bar closes (anim 3) and the panel opens (anim 4)
            sfx(gp::kTrickClick, gp::kTrickClickVolume);
            barAnim = {gp::kTrickBarCloseAnim, 0, 0, false, true};
            panelAnim = {gp::kTrickPanelAnim, 0, 0, false, true};
            panelClosing = false;
        };
        auto closeTrickPanel = [&] // Z, X or Enter: the panel just goes, with no closing animation, and the bar is not shown again
        {
            if (barBusy != 4)
                return;
            sfx(gp::kTrickClick, gp::kTrickClickVolume);
            panelAnim.visible = false;
            panelClosing = false;
            barAnim.visible = false;
            bar.forceClose();
            barBusy = 0;
        };
        auto startQuestion = [&] // activity 0x14: bubble 10, half a second, bubble 9
        {
            questionStep = 1;
            questionTicks = 0;
            questionBubble = gp::kTrickQuestionBubble;
        };
        auto startTrick = [&](int level) // activity 0xd: TickActor_NPCEncounterSequence
        {
            trickStep = 1;
            trickLevel = level;
            settlePhase = 0;
            walkTicks = 0;
            petTrick.startWith(level, sp, gp::TrickShowcase::petRepeats(level, sp, player.personality[gp::Lightbulbs], [&](int lo, int hi)
                                                                       { return lo + std::rand() % (hi - lo); })); // RandRange: hi exclusive
        };
        auto pickTrick = [&](int slot) // the panel and the bar go at once and the pet acts
        {
            panelAnim.visible = false;
            panelClosing = false;
            barAnim.visible = false;
            bar.forceClose();
            barBusy = 0;
            if (player.tricksKnown <= slot)
                startQuestion();
            else
                startTrick(slot + 1); // (the prize roll, RollPetPrizeId(3), isn't wired)
        };
        auto tickQuestion = [&]
        {
            if (!questionStep)
                return;
            const AnimRef A = animRefById("base:anim/" + std::to_string(questionBubble) + "@28c");
            const int len = gp::exclaimSteps(int(A.size()));
            ++questionTicks;
            if (questionStep == 1 && questionTicks >= len)
            {
                questionStep = 2;
                questionTicks = 0;
            }
            else if (questionStep == 2 && questionTicks >= int(gp::kTrickQuestionWait * 30))
            {
                questionStep = 3;
                questionBubble = gp::kTrickAnswerBubble;
                questionTicks = 0;
            }
            else if (questionStep == 3 && questionTicks >= len)
            {
                questionStep = 0;
                questionBubble = 0;
            }
        };
        auto tickTrick = [&]
        {
            if (!trickStep)
                return;
            if (trickStep == 1)
            {
                if (!host.pet.settle()) // TickActor_ReactionState: face the front first
                    return;
                trickStep = 2;
            }
            gp::TrickShowcase::Host th;
            th.full = true;
            th.play = [&](int id, int rate)
            {
                if (getAnim(id).size() == 0)
                    return false;
                playAnim(id, false, rate, Pose::Special);
                return true;
            };
            th.playReverse = [&](int id, int rate, bool)
            {
                if (getAnim(id).size() == 0)
                    return false;
                playAnim(id, false, rate, Pose::Special);
                animReverse = true;
                return true;
            };
            th.cameraPan = [&]
            { cameraShake.start(); };
            th.setFacing = [&](bool faceRight) // the monkey's turn-around: the turn art is mirrored by faceLeft
            {
                faceLeft = !faceRight;
                facing = 2;
            };
            th.busy = [&]
            { return !animLoop && !animDone; };
            th.anim = [&]
            { return animId; };
            th.frame = [&]
            { return animFrame; };
            th.sound = [&](int id, int vol)
            { sfx(id, vol); };
            th.soundActive = [&](int id)
            { return gp::audio().sfxActive(id); };
            th.stopSound = [&](int id)
            { gp::audio().stopSfx(id); };
            th.rand = [&](int lo, int hi)
            { return lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive
            th.seconds = [&]
            { return double(gameTicks) / 60.0; }; // gameTicks is +2 per 30 Hz step
            if (petTrick.tick(th))
            {
                trickStep = 0;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            }
        };

        // ---- the island map (world_map.hpp): a full-screen picture box with a marker where the pet is ----
        svc.showMap = [&]
        {
            const auto m = gp::mapMarker(cur->def->legacyId, pos.x, pos.y);
            gp::DialogueBox b;
            b.kind = gp::BoxKind::Picture;
            b.markX = m ? m->x : 0; // an area off the map keeps the last marker in the ROM; there is none to keep here
            b.markY = m ? m->y : 0;
            dialogue.show(std::move(b));
        };

        // ---- the spaceship's colour machine (activity 0x29); see color_machine.hpp ----
        // Step 1: turn to face front. Step 2: the colour cycles every 4 steps until Z; the new colour is the pet's base one.
        svc.startColorMachine = [&]
        {
            colorStep = 1;
            settlePhase = 0;
            walkTicks = 0;
        };
        auto tickColorMachine = [&](bool zHeld)
        {
            if (colorStep == 1)
            {
                if (!host.pet.settle())
                    return;
                if (!zHeld) // the button was let go before the pet was facing front: nothing happens
                {
                    colorStep = 0;
                    playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                    return;
                }
                if (const int i = gp::resyncIndex(sp, player.appearance); i >= 0)
                    colorIdx = i;
                colorDir = gp::nextDirection(colorDir);
                colorTicks = 0;
                colorStep = 2;
                sfx(gp::kColorMachineSound);
                return;
            }
            if (colorStep != 2)
                return;
            if (!zHeld) // letting go of the action button stops it on this colour
            {
                importPetAnims();
                saveGame();
                colorStep = 0;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                return;
            }
            if (colorTicks++ % gp::kColorStepTicks == 0)
            {
                colorIdx = gp::stepIndex(colorIdx, colorDir);
                const int pal = gp::machinePalette(sp, colorIdx);
                player.appearance = player.baseColour = petPal = pal;
                try
                {
                    gp::importAnim(rom, db, restIdleId(), petPal); // only the idle on show; the rest follows when it stops
                }
                catch (const std::exception &)
                {
                }
            }
        };

        // ---- washing the pet: bar button 4 starts activity 0x27 (HandlePetWashInteraction); see pet_wash.hpp ----
        int &washStep = S.pet.washStep; // 0 = not washing; 1 get up and start the bubble, 2 the bubble plays, 3 the happy reaction
        std::unique_ptr<gp::ReactionSeq> washSeq;

        auto startWash = [&]
        { washStep = 1; };

        auto tickWash = [&]
        {
            switch (washStep)
            {
            case 0:
                return;
            case 1: // TickActor_ReactionState: a sitting pet gets up first, then the bubble starts
            {
                const gp::SitPhase ph = gp::sitPhase(sp, animId, pose == Pose::Idle, pose == Pose::Special, animDone);
                if (ph == gp::SitPhase::Sitting)
                {
                    playAnim(gp::kSitAlt[sp], false, 14, Pose::Special);
                    break;
                }
                if (ph != gp::SitPhase::None) // sitting down or getting up: wait
                    break;
                // Always front-on: after walking sideways the pet settles facing the front, but our `facing` stays
                // "side" until something resets it, and the idle and the happy reaction would turn sideways.
                facing = 1;
                walkTicks = 0;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                overId = gp::kWashBubbleAnim;
                overTicks = 0;
                washStep = 2;
                break;
            }
            case 2: // the bubble plays once at the pet
            {
                const int frames = int(animRefById(reactAnimId(gp::kWashBubbleAnim, 0x28c)).size());
                if (overTicks < gp::bubbleTicks(frames))
                    break;
                overId = -1;
                gp::applyWash(player); // +100 hygiene, original colour back
                const int pal = gp::washedPalette(player, petPal);
                if (pal != petPal) // the potion colour comes off
                {
                    petPal = pal;
                    importPetAnims();
                }
                saveGame();
                washSeq = std::make_unique<gp::ReactionSeq>(gp::ReactionSeq::Kind::Positive, gp::kWashHappyRepeats, gp::kWashHappyVariant);
                washStep = 3;
                break;
            }
            case 3: // PlayPositiveReactionAnim(pet, 3, 2), then the mood top-up
                if (washSeq && washSeq->tick(host.pet))
                {
                    washSeq.reset();
                    gp::applyWashMood(player);
                    host.petIdle();
                    washStep = 0;
                    saveGame();
                }
                break;
            }
        };
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

        // ---- player homes and hotels (EnterActivityAndRunLoop 0x04ab19); see room_scene.hpp ----
        // Phase 1: the room's walls and floor, the pet walking in and out and along the floor with the indoor art, and the
        // door back to the street. Furniture, the house bar and the room's activities come later.
        // The 32 furnishing slots: a hotel's preset, the player's own equipment in their home, or another save slot's
        // (an unused slot's house is empty: "FOR RENT").
        auto roomSlots = [&](int location)
        {
            gp::SlotItems slots{};
            if (gp::isHotel(location))
                slots = gp::hotelSlotItems(rom, location);
            else
            {
                std::map<int, int> equip;
                if (location == slot)
                    equip = player.equip;
                else if (gp::SaveData other; gp::loadFromFile(gp::slotPath(location), other) && gp::peekSlot(location).full)
                    equip = other.has("equip") ? other.getMap("equip") : gp::starterEquip(other.getInt("species"));
                for (const auto &[s, item] : equip)
                    if (s >= 0 && s < gp::kInventorySlots)
                        slots[size_t(s)] = item;
            }
            return slots;
        };
        // Rebuilds what the room shows from its slots: the wall and floor themes (slots 0 and 1) and the sprite layers.
        auto refreshRoom = [&](RoomView &r)
        {
            auto themeOf = [&](int s)
            {
                const int item = r.slots[size_t(s)];
                return item > 0 && item < int(itemDefs.size()) ? itemDefs[size_t(item)].w9 : 0;
            };
            const gp::RoomTheme th = gp::roomTheme(rom, themeOf(gp::kInventoryWallSlot), themeOf(gp::kInventoryFloorSlot));
            const gp::Image img = gp::renderRoom(rom, db, th);
            if (r.tex)
                SDL_DestroyTexture(r.tex);
            r.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
            SDL_UpdateTexture(r.tex, nullptr, img.rgba.data(), img.w * 4);
            SDL_SetTextureBlendMode(r.tex, SDL_BLENDMODE_BLEND);
            r.sprites = gp::roomSprites(rom, gp::roomDecorations(rom, r.location), r.slots, itemDefs);
        };
        auto beginRoom = [&](int location)
        {
            auto r = std::make_unique<RoomView>();
            r->location = location;
            r->slots = roomSlots(location);
            r->table = gp::roomDecorations(rom, location);
            // Entering a home that isn't yours: its pet, in the colour saved there, or "FOR RENT" when the slot is unused.
            if (location >= 0 && location <= 2 && location != slot)
            {
                const gp::SlotInfo other = gp::peekSlot(location);
                r->forRent = !other.full;
                if (other.full)
                {
                    const gp::SpeciesDef *sd = db.speciesByLegacy(other.species);
                    if (sd)
                    {
                        r->hasVisitor = true;
                        r->visitSpecies = other.species;
                        r->visitPal = other.appearance >= 0 ? other.appearance : sd->defaultPalette;
                        try
                        {
                            gp::importAnim(rom, db, gp::kSettledStyle[other.species], r->visitPal);
                        }
                        catch (const std::exception &)
                        {
                        }
                    }
                }
            }
            refreshRoom(*r);
            r->scroll = gp::roomScroll(0, r->x, gp::kRoomWidthTiles, W);
            room = std::move(r);
            std::printf("room: location %d\n", location);
        };
        svc.enterRoom = [&](int location)
        {
            if (!gp::isRoomLocation(location) || room)
                return;
            sfx(gp::kRoomEnterSound); // PlaySoundEffect(0x13, 200)
            requestScreen([&, location]
                          { beginRoom(location); });
        };

        auto leaveRoom = [&]
        {
            const gp::RoomExitPoint p = gp::roomReturnPoint(room->location); // back at the door, by return state
            room.reset();
            pos = {p.x, p.y};
            prevPos = pos;
            facing = 1;
            walkTicks = 0;
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
        };

        // ---- eating at the food bowl: see the comment in room_scene.hpp ----
        // The food in the bowl is table entry 3's sprite; it changes while the pet eats.
        auto setBowl = [&](RoomView &r, int sprite)
        {
            if (sprite < 0 || int(r.table.size()) <= gp::kBowlEntry || r.table[gp::kBowlEntry].sprite == sprite)
                return;
            r.table[gp::kBowlEntry].sprite = sprite;
            r.sprites = gp::roomSprites(rom, r.table, r.slots, itemDefs);
        };
        // fromBowl: Z at the bowl, which gives item 0x1c's effect. From the item screen the chosen food has already had its
        // own effect (the ROM's menu result 2), and the bowl is filled and eaten from all the same.
        auto startEating = [&](RoomView &r, bool fromBowl)
        {
            if (fromBowl)
                gp::applyFoodEffect(player, itemDefs[gp::kBowlFoodItem], itemHost); // ApplyItemCategoryEffect(0x1c), at once
            setBowl(r, gp::kBowlFull);
            r.phase = RoomView::Phase::Eating;
            r.eatStep = 6;
            r.eatSX = gp::bowlApproachX(sp, r.y);
            r.eatGivesHappy = true;
            r.eatWash = false;
            r.animTicks = 0;
        };
        // Washing (HandlePetWashInteraction, pet_wash.hpp) in a room: the bubble at the pet, then +100 cleanliness and the
        // original colour back, then the indoor happy hop three times and the mood top-up.
        auto startRoomWash = [&](RoomView &r)
        {
            r.phase = RoomView::Phase::Eating;
            r.eatStep = 20;
            r.eatGivesHappy = false;
            r.eatWash = true;
            r.eatAnim = gp::kIdleStyle[sp]; // front-on, any facing
            r.eatFlip = false;
            r.eatLoop = true;
            r.animTicks = 0;
            r.overId = gp::kWashBubbleAnim;
            r.overTicks = 0;
        };
        // Sleeping (activity 5 from the bed or the bar; activity 0x25 when quitting): see room_scene.hpp.
        auto startSleep = [&](RoomView &r, bool quitting)
        {
            r.phase = RoomView::Phase::Eating;
            r.eatStep = 30;
            r.eatGivesHappy = false;
            r.eatWash = false;
            r.sleepQuit = quitting;
            r.eatAnim = gp::kBoredStyle[sp];
            r.eatFlip = false;
            r.eatLoop = false;
            r.eatFps = 7; // PlayBoredReactionAnim plays at 7
            r.animTicks = 0;
            petSound(8, 0xa0); // the yawn: the bored reaction's sound
        };
        roomQuit = [&]
        { startSleep(*room, true); };
        roomActivity = [&](int id)
        {
            if (id == 5) // the bar's sleep button
            {
                startSleep(*room, false);
                return true;
            }
            if (id != gp::kWashActivity)
                return false;
            startRoomWash(*room);
            return true;
        };
        // A cure (HandleCureIllnessEncounter 0x015ed8, activity 0x28): where the pet stands, the bubbles over it and the
        // indoor happy hop three times. The Happiness bonus is the bowl's only. A medicine that doesn't match does nothing.
        auto startRoomMedicine = [&](RoomView &r, int curedIllness)
        {
            r.phase = RoomView::Phase::Eating;
            r.eatStep = 17; // the hop, with the panda's intro and outro
            r.eatGivesHappy = false;
            r.eatWash = false;
            r.eatAnim = gp::kIdleStyle[sp]; // held for the one step before the hop starts
            r.eatFlip = false;
            r.eatLoop = true;
            r.eatFps = 14;
            r.eatReverse = false;
            r.animTicks = 0;
            r.overId = 137 + (curedIllness % 13); // the same bubble animation as outdoors
            r.overTicks = 0;
        };
        // The first-time messages (settings.hpp). Standing next to the bed or the bowl is enough: the pet stops, the event is
        // marked shown (saved with the player), and 0.25 s later the message opens.
        auto tutorialBegin = [&](RoomView &r)
        {
            player.tutorialsShown = gp::markShown(player.tutorialsShown, r.tutorialEvent);
            saveGame();
            r.eatAnim = gp::kIdleStyle[sp]; // front-on
            r.eatFlip = false;
            r.eatLoop = true;
            r.eatFps = 14;
            r.eatReverse = false;
            r.animTicks = 0;
            r.eatStep = 41;
        };
        auto startTutorial = [&](RoomView &r, int event)
        {
            const bool turn = r.phase == RoomView::Phase::Walking || r.phase == RoomView::Phase::Settling;
            r.phase = RoomView::Phase::Eating;
            r.tutorialEvent = event;
            r.eatGivesHappy = false;
            r.eatWash = false;
            r.sleepQuit = false;
            r.overId = -1;
            if (turn) // it was walking: it settles to face the front first
            {
                r.eatAnim = gp::kTurn90Style[sp];
                r.eatFlip = !r.faceLeft; // the art faces left
                r.eatLoop = false;
                r.eatFps = 14;
                r.eatReverse = false;
                r.animTicks = 0;
                r.eatStep = 40;
            }
            else
                tutorialBegin(r);
        };
        // Called every tick the pet is free: HandlePetStageEvents starts the message the first time it is by the bed or bowl.
        auto roomTutorial = [&]
        {
            RoomView &r = *room;
            using P = RoomView::Phase;
            if (r.phase != P::Idle && r.phase != P::Walking && r.phase != P::Settling && r.phase != P::Turning)
                return;
            const int event = r.nearby == gp::kDecorBed ? gp::kTutorialBed : r.nearby == gp::kDecorBowl ? gp::kTutorialBowl : -1;
            if (event >= 0 && gp::isFirstTime(settings.tutorials, player.tutorialsShown, event))
                startTutorial(r, event);
        };

        auto tickEat = [&](RoomView &r)
        {
            const gp::BowlTarget t = gp::bowlTarget(sp);
            auto done = [&](int id) // a one-shot animation has played to its end
            {
                const int n = int(getAnim(id).size());
                return n == 0 || r.animTicks * r.eatFps / 30 >= n;
            };
            auto play = [&](int id, bool flip, bool loop, int fps = 14) // 14 is kFrameRate
            {
                r.eatAnim = id;
                r.eatFlip = flip;
                r.eatLoop = loop;
                r.eatFps = fps;
                r.eatReverse = false;
                r.animTicks = 0;
            };
            // The end of every happy hop (bowl, cure, wash, bed): the Happiness top-ups, save, and back to normal.
            auto finishHappy = [&]
            {
                if (r.eatGivesHappy && player.care[gp::Happiness] < gp::kHappinessCap)
                    player.adjustCare(gp::Happiness, gp::kBowlHappiness);
                if (r.eatWash)
                    gp::applyWashMood(player);
                saveGame();
                r.phase = RoomView::Phase::Idle;
                r.eatStep = 0;
                r.animTicks = 0;
                r.overId = -1;
            };
            auto walk = [&] // the sideways walk, mirrored for left
            {
                if (r.eatAnim != gp::kWalkSideStyle[sp] || r.eatFlip != r.faceLeft)
                    play(gp::kWalkSideStyle[sp], r.faceLeft, true);
            };
            // The bite, chew and action art is played unmirrored: the dog's and monkey's faces left (the bowl is on their left)
            // and the panda's faces right (it stands to the bowl's left).
            auto bite = [&]
            {
                play(gp::kBowlBite[sp], false, false);
                r.eatStep = 9;
            };
            ++r.animTicks;
            switch (r.eatStep)
            {
            case 6: // along the floor to the foot of the diagonal
                if (r.x == r.eatSX)
                {
                    r.eatStep = 7;
                    break;
                }
                r.faceLeft = r.eatSX < r.x;
                walk();
                r.x += std::clamp(r.eatSX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                break;
            case 7: // up the diagonal to the bowl
                if (r.y > t.y)
                {
                    r.faceLeft = t.x < r.x;
                    walk();
                    r.x += std::clamp(t.x - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                    r.y -= 1;
                    break;
                }
                r.x = t.x;
                r.y = t.y;
                r.faceLeft = sp != 2; // the bowl is to the left of everyone but the panda
                r.eatRepeat = 2 + std::rand() % 2; // RandRange(2,4): 2 or 3 bites
                if (sp == 2) // the panda starts with an action animation
                {
                    play(gp::kActionStyle[sp], false, false);
                    r.eatStep = 8;
                }
                else
                    bite();
                break;
            case 8:
                if (done(gp::kActionStyle[sp]))
                    bite();
                break;
            case 9: // lean in; the food drops as the bites go
                setBowl(r, gp::bowlSpriteForBites(r.eatRepeat));
                if (done(gp::kBowlBite[sp]))
                {
                    --r.eatRepeat;
                    r.eatChews = 2 + std::rand() % 3; // RandRange(2,5): 2..4 chews
                    play(gp::kEatStyle[sp], false, false);
                    chewSound(); // GUESS: the bowl's chews sound like the outdoor ones
                    r.eatStep = 10;
                }
                break;
            case 10: // chew, then a short pause holding the last frame
                if (done(gp::kEatStyle[sp]))
                {
                    if (--r.eatChews > 0)
                    {
                        play(gp::kEatStyle[sp], false, false);
                        chewSound();
                    }
                    else
                    {
                        r.eatPause = gp::bowlPauseTicks(1 + std::rand() % 3); // RandRange(1,4) / 20 seconds
                        r.eatStep = 11;
                    }
                }
                break;
            case 11:
                if (--r.eatPause > 0)
                    break;
                if (r.eatRepeat < 1)
                {
                    r.eatSX = (gp::kRoomFloorY - r.y) * (sp == 2 ? -2 : 2) + r.x; // back to the floor
                    r.eatStep = 12;
                }
                else
                    bite();
                break;
            case 12: // down the diagonal
                if (r.y < gp::kRoomFloorY)
                {
                    r.faceLeft = r.eatSX < r.x;
                    walk();
                    r.x += std::clamp(r.eatSX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                    r.y += 1;
                    break;
                }
                r.x = r.eatSX;
                r.y = gp::kRoomFloorY;
                play(gp::kTurn90Style[sp], !r.faceLeft, false); // art faces left, so mirrored for right
                r.eatStep = 13;
                break;
            case 13: // turn to the front
                if (done(gp::kTurn90Style[sp]))
                    r.eatStep = 17;
                break;
            case 40: // a first-time message: the pet turns to the front first (TickActor_SettleFacing)
                if (done(gp::kTurn90Style[sp]))
                    tutorialBegin(r);
                break;
            case 41: // marked shown and idle: after 0.25 s the message opens (TickFirstNPCEncounterSequence)
                if (r.animTicks >= gp::kTutorialWaitTicks)
                {
                    const int event = r.tutorialEvent;
                    r.phase = RoomView::Phase::Idle;
                    r.eatStep = 0;
                    r.animTicks = 0;
                    r.tutorialEvent = -1;
                    dialogue.show(gp::messageBox(db.textOr("base:text/" + std::to_string(gp::tutorialText(event)))));
                }
                break;
            case 30: // the yawn (the bored reaction), then off to the bed
                if (done(gp::kBoredStyle[sp]))
                {
                    r.eatSX = gp::bedApproachX(r.y);
                    r.eatStep = 31;
                }
                break;
            case 31: // along the floor to the foot of the diagonal
                if (r.x == r.eatSX)
                {
                    r.eatStep = 32;
                    break;
                }
                r.faceLeft = r.eatSX < r.x;
                walk();
                r.x += std::clamp(r.eatSX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                break;
            case 32: // up the diagonal to the bed
                if (r.y > gp::kBedY)
                {
                    r.faceLeft = gp::kBedX < r.x;
                    walk();
                    r.x += std::clamp(gp::kBedX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                    r.y -= 1;
                    break;
                }
                r.x = gp::kBedX;
                r.y = gp::kBedY;
                play(gp::kBedGetIn[sp], false, false);
                r.eatStep = 33;
                break;
            case 33:
                if (done(gp::kBedGetIn[sp]))
                {
                    r.eatRepeat = gp::kSleepCycles;
                    play(gp::kSleepCycle[sp], false, false, gp::sleepCycleFps(sp));
                    sfx(6, 0xbe); // the sleeping sound: sound 6 (browser #11); GUESS: once per cycle
                    r.eatStep = 34;
                }
                break;
            case 34: // sleep cycles: +10 Tiredness each (not when quitting), on until 3 are done and the pet is rested
                if (done(gp::kSleepCycle[sp]))
                {
                    if (!r.sleepQuit)
                        player.adjustCare(gp::Tiredness, gp::kSleepTiredness);
                    --r.eatRepeat;
                    if (!gp::sleepOver(r.eatRepeat, player.care[gp::Tiredness], r.sleepQuit))
                    {
                        play(gp::kSleepCycle[sp], false, false, gp::sleepCycleFps(sp));
                        sfx(6, 0xbe);
                    }
                    else if (r.sleepQuit) // the ROM halts here; the game saves and goes back to the menu
                    {
                        saveGame();
                        S.flow.backToMenu = true;
                        S.flow.running = false;
                    }
                    else
                    {
                        play(gp::kBedGetOut[sp], false, false);
                        r.eatStep = 35;
                    }
                }
                break;
            case 35: // out of bed, back down the diagonal (step 12), turn to the front (13) and the happy hop (14)
                if (done(gp::kBedGetOut[sp]))
                {
                    r.eatSX = (gp::kRoomFloorY - r.y) * -2 + r.x;
                    r.eatWash = true; // the same +10 Happiness top-up as after a wash
                    r.eatStep = 12;
                }
                break;
            case 20: // the wash bubble plays once at the pet; then the wash itself
            {
                const int frames = int(animRefById(reactAnimId(gp::kWashBubbleAnim, 0x28c)).size());
                if (r.overTicks < gp::bubbleTicks(frames))
                    break;
                r.overId = -1;
                gp::applyWash(player); // +100 hygiene, original colour back
                const int pal = gp::washedPalette(player, petPal);
                if (pal != petPal) // the potion colour comes off
                {
                    petPal = pal;
                    importPetAnims();
                }
                saveGame();
                r.eatStep = 17;
                break;
            }
            case 17: // PlayPositiveReactionAnim(pet, 3, 2) begins: the panda's intro first, the others go straight to the hop
                r.eatHappy = 0;
                if (sp == 2)
                {
                    play(gp::kPandaFrameStyle, false, false);
                    r.eatStep = 15;
                }
                else
                {
                    play(gp::kPositiveStyle[sp], false, false), petSound(4, 0xbe); // PlayPositiveReactionAnim: the happy panting
                    r.eatStep = 14;
                }
                break;
            case 15: // the panda's intro is over
                if (done(gp::kPandaFrameStyle))
                {
                    play(gp::kPositiveStyle[sp], false, false), petSound(4, 0xbe); // PlayPositiveReactionAnim: the happy panting
                    r.eatStep = 14;
                }
                break;
            case 14: // the hop, three times
                if (done(r.eatAnim))
                {
                    if (++r.eatHappy < 3)
                        play(r.eatAnim, false, false), petSound(4, 0xbe);
                    else if (sp == 2) // then the panda's outro: its intro played last frame to first
                    {
                        play(gp::kPandaFrameStyle, false, false);
                        r.eatReverse = true;
                        r.eatStep = 16;
                    }
                    else
                        finishHappy();
                }
                break;
            case 16:
                if (done(gp::kPandaFrameStyle))
                    finishHappy();
                break;
            }
        };

        // Put the item in the slot (SetInventorySlot) and show the room with it. Only the player's own home has equipment to
        // change; it is saved with the player.
        auto equipSlot = [&](RoomView &r, int s, int item)
        {
            r.slots[size_t(s)] = item;
            if (item)
                player.equip[s] = item;
            else
                player.equip.erase(s);
            saveGame();
            refreshRoom(r);
        };

        // Back to the item screen, on the tab it was left on (while the screen is black, from requestScreen).
        // How many of each item the pet in this home has (LoadHeldItemQuantitiesFromSave(slot, 1)).
        auto loadTheirItems = [&](int location)
        {
            giveTheirs.clear();
            gp::SaveData s;
            if (!gp::loadFromFile(gp::slotPath(location), s))
                return;
            if (s.has("itemIds"))
            {
                const std::map<std::string, int> ids = s.getIdMap("itemIds");
                for (const gp::ItemDef &d : itemDefs)
                    if (auto it = ids.find(d.id); it != ids.end())
                        giveTheirs[d.index] = it->second;
            }
            else
                for (const auto &[n, c] : s.getMap("inventory"))
                    giveTheirs[n] = c;
        };
        // Z on the visiting pet: the item screen in give mode (HandlePetStageEvents sets the item menu mode to 2).
        auto startGive = [&](RoomView &r)
        {
            giveMode = true;
            giveLocation = r.location;
            loadTheirItems(r.location);
            requestScreen([&]
                          {
                im = {};
                im.open = true;
                im.category = barHost.category;
                im.filtered = buildFiltered(im.category);
                std::printf("give: %zu items they have room for\n", im.filtered.size()); });
        };

        // The room stays in its placing view until the screen is black, so the pet never shows during the fade out.
        auto reopenItemScreen = [&]
        {
            if (room)
            {
                room->phase = RoomView::Phase::Idle;
                room->animTicks = 0;
            }
            im = {};
            im.open = true;
            im.category = roomMenuCategory;
            im.filtered = buildFiltered(im.category);
        };

        auto tickRoom = [&](uint8_t dirs, uint8_t newly, bool zPressed, bool xPressed)
        {
            RoomView &r = *room;
            using P = RoomView::Phase;
            constexpr int kSettleTicks = 5; // the walk keeps playing for 0.15 s after the keys are released
            // The diagonal in and out of the door meets the floor line here (StartPetActivity(3) and Pet_WalkOutAndBack).
            const int floorX = gp::kRoomDoorX + (gp::kRoomFloorY - gp::kRoomDoorY) * 2;
            ++r.visitTicks; // the visiting pet's idle keeps playing whatever the player is doing
            if (r.phase == P::Placed) // the item is down: hold the room, then fade back to the inventory
            {
                if (++r.animTicks >= gp::kPlacedHoldTicks)
                    requestScreen(reopenItemScreen); // the phase goes back to Idle once the screen is black
                return;
            }
            if (r.phase == P::Placing) // UpdatePetWalkToItem / HandleMapNavigationInput
            {
                if (xPressed) // cancel: the ROM switches state straight back to the item screen
                {
                    requestScreen(reopenItemScreen);
                    return;
                }
                if (!r.placeConfirmed)
                {
                    int to = r.placeSlot;
                    if (newly & gp::DirLeft)
                        to = gp::neighbourSlot(rom, to, gp::SlotDir::Left);
                    else if (newly & gp::DirRight)
                        to = gp::neighbourSlot(rom, to, gp::SlotDir::Right);
                    else if (newly & gp::DirUp)
                        to = gp::neighbourSlot(rom, to, gp::SlotDir::Up);
                    else if (newly & gp::DirDown)
                        to = gp::neighbourSlot(rom, to, gp::SlotDir::Down);
                    r.placeSlot = to >= 0 && to < gp::kInventorySlots ? to : r.placeSlot;
                    if (zPressed)
                    {
                        sfx(gp::kPlaceSound);
                        r.placeConfirmed = true;
                    }
                }
                // The marker flies to the slot at 4 px per tick on each axis.
                const gp::SlotPos g = gp::slotPosition(rom, r.placeSlot);
                r.curX += std::clamp(g.x - r.curX, -gp::kPlaceFlightStep, gp::kPlaceFlightStep);
                r.curY += std::clamp(g.y - r.curY, -gp::kPlaceFlightStep, gp::kPlaceFlightStep);
                // The background scrolls to keep the marker near the middle of the screen.
                const int wantScroll = std::clamp(r.curX - 0x9f - (W - 320) / 2, 0, gp::kRoomWidthTiles * gp::kRoomTilePx - W);
                r.scroll += std::clamp(wantScroll - r.scroll, -gp::kPlaceFlightStep, gp::kPlaceFlightStep);
                if (r.placeConfirmed && r.curX == g.x && r.curY == g.y)
                {
                    equipSlot(r, r.placeSlot, r.placeItem);
                    r.phase = P::Placed; // SetInventorySlot, then back to the item screen after a moment
                    r.animTicks = 0;
                }
                return;
            }
            if (r.phase == P::Eating)
            {
                if (r.overId >= 0)
                    ++r.overTicks;
                tickEat(r);
                r.nearby = -1;
                r.scroll = gp::roomScroll(r.scroll, r.x, gp::kRoomWidthTiles, W);
                return;
            }
            if (r.phase == P::Entering)
            {
                r.faceLeft = false;
                ++r.walkTicks;
                r.x += gp::kRoomStepPx;
                r.y += 1;
                if (r.y >= gp::kRoomFloorY)
                {
                    r.x = floorX;
                    r.y = gp::kRoomFloorY;
                    r.phase = P::Idle;
                    r.animTicks = 0;
                }
            }
            else if (r.phase == P::Leaving)
            {
                ++r.walkTicks;
                if (r.y == gp::kRoomFloorY && r.x != floorX) // along the floor to the foot of the diagonal
                {
                    const int d = floorX - r.x;
                    r.faceLeft = d < 0;
                    r.x += std::clamp(d, -gp::kRoomStepPx, gp::kRoomStepPx);
                }
                else // then up and across to the door
                {
                    r.faceLeft = true;
                    r.x -= gp::kRoomStepPx;
                    r.y -= 1;
                    if (r.y <= gp::kRoomDoorY)
                    {
                        r.x = gp::kRoomDoorX;
                        r.y = gp::kRoomDoorY;
                        sfx(gp::kRoomEnterSound);
                        requestScreen(leaveRoom);
                    }
                }
            }
            else
            {
                const bool left = (dirs & gp::DirLeft) != 0, right = (dirs & gp::DirRight) != 0;
                if (left || right) // up and down do nothing here; left wins when both are held
                {
                    r.faceLeft = left;
                    r.x = gp::roomClampX(r.x + (left ? -gp::kRoomStepPx : gp::kRoomStepPx));
                    if (r.phase != P::Walking)
                        r.walkTicks = 0; // a new walk starts on frame 0
                    r.phase = P::Walking;
                    ++r.walkTicks;
                }
                else if (r.phase == P::Walking)
                {
                    r.phase = P::Settling;
                    r.settleTicks = 0;
                }
                if (r.phase == P::Settling)
                {
                    ++r.walkTicks;
                    if (++r.settleTicks >= kSettleTicks)
                    {
                        r.phase = P::Turning;
                        r.animTicks = 0;
                    }
                }
                else if (r.phase == P::Turning)
                {
                    const int n = int(getAnim(gp::kTurn90Style[sp]).size());
                    if (n == 0 || ++r.animTicks * kFrameRate / 30 >= n)
                    {
                        r.phase = P::Idle;
                        r.animTicks = 0;
                    }
                }
                else if (r.phase == P::Idle)
                    ++r.animTicks;
                // Z at the door walks the pet out (HandlePetStageEvents: the nearest decoration is the door -> activity 4).
                if (zPressed && r.phase != P::Walking && r.nearby == gp::kDecorDoor)
                {
                    r.phase = P::Leaving;
                    r.walkTicks = 0;
                }
                // Z at the food bowl: eat (ApplyItemCategoryEffect(0x1c), then activity 6).
                else if (zPressed && r.phase != P::Walking && r.nearby == gp::kDecorBowl)
                {
                    startEating(r, true);
                    return;
                }
                // Z on the visiting pet opens the item screen in mode 2 to give it items (not built yet).
                else if (zPressed && r.phase != P::Walking && r.nearby == gp::kDecorVisitor)
                {
                    startGive(r);
                    return;
                }
                // Z at the bed: sleep (activity 5).
                else if (zPressed && r.phase != P::Walking && r.nearby == gp::kDecorBed)
                {
                    startSleep(r, false);
                    return;
                }
            }
            // The decoration next to the pet, for the selector arrow and for Z (FindDecorationNearPetX). Only while the pet is
            // free to act: not walking in or out, and not while something else has the buttons.
            const bool free = r.phase == P::Idle || r.phase == P::Walking || r.phase == P::Settling || r.phase == P::Turning;
            r.nearby = free ? gp::nearbyDecoration(r.table, r.x,
                                                   [&](const gp::RoomDecoration &e)
                                                   {
                                                       const AnimRef D = animRefById(uiIdP(e.sprite, e.palette));
                                                       return D.size() ? D.anim->frames[0].anchorX - D.anim->frames[0].dx : 0;
                                                   })
                           : -1;
            if (free)
                r.nearby = gp::visitRules(r.nearby, r.x, r.hasVisitor, r.forRent); // the visiting pet; a house for rent has only the door
            r.scroll = gp::roomScroll(r.scroll, r.x, gp::kRoomWidthTiles, W);
        };

        // The room, then the pet in it (the indoor "style" art: no up or down walk, the front idle for any facing).
        auto drawRoom = [&]
        {
            RoomView &r = *room;
            using P = RoomView::Phase;
            // smooth mode glides the panning and the pet's walk between the 30 Hz steps (a jump of more than 16 px never glides)
            const float scrollF = smoothAt(r.pscroll, r.scroll), rx = smoothAt(r.px, r.x), ry = smoothAt(r.py, r.y);
            gp::clearFrame(ren);
            const SDL_FRect dst{-scrollF, 0.f, float(gp::kRoomWidthTiles * gp::kRoomTilePx),
                                float(gp::kRoomHeightTiles * gp::kRoomTilePx)};
            SDL_RenderCopyF(ren, r.tex, nullptr, &dst);
            auto drawLayer = [&](const std::vector<gp::RoomSprite> &layer)
            {
                for (const gp::RoomSprite &d : layer)
                {
                    const AnimRef D = animRefById(uiIdP(d.sprite, d.palette));
                    if (D.size() == 0)
                        continue;
                    const gp::SpriteFrame &s = D.anim->frames[0];
                    SDL_FRect rc{float(d.x) - scrollF - s.anchorX + s.dx, float(d.y) - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
                    SDL_RenderCopyF(ren, (*D.tex)[0], nullptr, &rc);
                }
            };
            drawLayer(r.sprites.behind); // the order EnterActivityAndRunLoop draws in
            drawLayer(r.sprites.items);
            // The flag-0 pieces are split by the pet's y every frame: those at or above its feet go behind it, those below
            // are drawn over it (the panda stands at y 0xa1, above the bowl food at 0xad, so the bowl is in front of it).
            auto petSide = [&](bool inFront)
            {
                std::vector<gp::RoomSprite> v;
                for (const gp::RoomSprite &s : r.sprites.beforePet)
                    if ((s.y > r.y) == inFront)
                        v.push_back(s);
                return v;
            };
            if (r.phase == P::Placing || r.phase == P::Placed)
                drawLayer(r.sprites.beforePet);
            else
                drawLayer(petSide(false));

            if (r.phase == P::Placed) // the finished room, no pet and no markers
            {
                drawLayer(r.sprites.afterPet);
                return;
            }
            if (r.phase == P::Placing) // RenderPetWalkToItem: a marker on every slot that takes the item, and the cursor
            {
                drawLayer(r.sprites.afterPet);
                const int type = r.placeItem > 0 && r.placeItem < int(itemDefs.size()) ? itemDefs[size_t(r.placeItem)].type : 0;
                const int markerSprite = type == 6 ? 0x319 : type == 5 ? 0x3b0 : 0x33d;
                const int dy = type == 6 ? 0x14 : 0;
                for (int s = 0; s < gp::kInventorySlots; ++s)
                    if (gp::slotCategory(rom, s) == type)
                    {
                        const gp::SlotPos p = gp::slotPosition(rom, s);
                        drawLayer({{p.x, p.y + dy, markerSprite, 0x28c}});
                    }
                if (const AnimRef C = animRefById(reactAnimId(gp::kMarkerIndoorItemAnim, gp::kMarkerPalette)); C.size() > 0)
                {
                    const gp::SpriteFrame &s = C.anim->frames[size_t(gp::markerFrame(gameTicks, int(C.size())))];
                    SDL_FRect rc{smoothAt(r.pcurX, r.curX) - scrollF - s.anchorX + s.dx, smoothAt(r.pcurY, r.curY) + float(dy) - s.anchorY + s.dy + 1, float(s.w),
                                 float(s.h)};
                    SDL_RenderCopyF(ren, (*C.tex)[size_t(gp::markerFrame(gameTicks, int(C.size())))], nullptr, &rc);
                }
                return;
            }

            // The pet whose home this is, in its settled-idle pose (drawn just before the player's pet, like the ROM does).
            if (r.hasVisitor)
            {
                const gp::AnimChoice ac = gp::settledIdle(r.visitSpecies, true);
                const AnimRef V = animRefById(reactAnimId(ac.id, r.visitPal));
                if (V.size() > 0)
                {
                    const int f = (r.visitTicks * ac.rate / 30) % int(V.size());
                    const gp::SpriteFrame &s = V.anim->frames[size_t(f)];
                    drawShadow(s.w, float(gp::kVisitorX), float(gp::kVisitorY), r.visitSpecies == 2, scrollF, 0.f);
                    SDL_FRect rc{float(gp::kVisitorX) - scrollF - s.anchorX + s.dx + offX, float(gp::kVisitorY) - s.anchorY + s.dy + offY,
                                 float(s.w), float(s.h)};
                    SDL_RenderCopyF(ren, (*V.tex)[size_t(f)], nullptr, &rc);
                }
            }

            const bool walking = r.phase == P::Entering || r.phase == P::Walking || r.phase == P::Settling || r.phase == P::Leaving;
            int id = gp::kIdleStyle[sp];
            bool flip = false;
            int frame = 0;
            auto frameOf = [&](int anim, int ticks, bool loop, int rate = 14) // 14 is kFrameRate
            {
                const int n = int(getAnim(anim).size());
                if (n <= 0)
                    return 0;
                const int k = ticks * rate / 30;
                return loop ? k % n : std::min(k, n - 1);
            };
            if (walking)
            {
                id = gp::kWalkSideStyle[sp]; // art faces right: mirrored for left
                flip = r.faceLeft;
                frame = frameOf(id, r.walkTicks, true);
            }
            else if (r.phase == P::Turning)
            {
                id = gp::kTurn90Style[sp]; // art faces left: mirrored for right
                flip = !r.faceLeft;
                frame = frameOf(id, r.animTicks, false);
            }
            else if (r.phase == P::Eating && r.eatAnim >= 0)
            {
                id = r.eatAnim;
                flip = r.eatFlip;
                frame = frameOf(id, r.animTicks, r.eatLoop, r.eatFps);
                if (r.eatReverse) // last frame to first
                    frame = std::max(0, int(getAnim(id).size()) - 1 - frame);
            }
            else
                frame = frameOf(id, r.animTicks, true);
            if (const AnimRef A = getAnim(id); A.size() > 0)
            {
                const int n = int(A.size());
                const int f = std::min(frame, n - 1);
                const gp::SpriteFrame &s = A.anim->frames[size_t(f)];
                drawShadow(s.w, rx, ry, sp == 2, scrollF, 0.f);
                const float px = rx - scrollF;
                float x = flip ? px + s.anchorX - s.dx - s.w : px - s.anchorX + s.dx;
                float y = ry - s.anchorY + s.dy;
                x += offX;
                y += offY;
                SDL_FRect rc{x, y, float(s.w), float(s.h)};
                SDL_RenderCopyExF(ren, (*A.tex)[size_t(f)], nullptr, &rc, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
            }
            if (r.overId >= 0) // the cure's bubbles over the pet
            {
                const AnimRef A = animRefById("base:anim/" + std::to_string(r.overId) + "@28c");
                if (A.size())
                {
                    const int fps = r.overId == gp::kWashBubbleAnim ? gp::kWashBubbleFps : kBubbleFps;
                    const int f = std::min(r.overTicks * fps / 30, int(A.size()) - 1);
                    const gp::SpriteFrame &s = A.anim->frames[size_t(f)];
                    SDL_FRect rc{rx - scrollF - s.anchorX + s.dx, ry - s.anchorY + s.dy, float(s.w), float(s.h)};
                    SDL_RenderCopyF(ren, (*A.tex)[size_t(f)], nullptr, &rc);
                }
            }
            drawLayer(petSide(true));
            drawLayer(r.sprites.afterPet);

            // The selector arrow over the decoration the pet is next to (anim 0x11 at 18 fps, as for objects outside), hidden
            // while the bar or a box has the buttons.
            if (r.nearby >= 0 && barBusy == 0 && !dialogue.open())
            {
                // The arrow goes over the sprite it marks: a table piece, or the visiting pet (index 5).
                bool have = false;
                gp::SelectorPos mp{0, 0};
                if (r.nearby == gp::kDecorVisitor && r.hasVisitor)
                {
                    const gp::AnimChoice ac = gp::settledIdle(r.visitSpecies, true);
                    const AnimRef V = animRefById(reactAnimId(ac.id, r.visitPal));
                    if (V.size() > 0)
                    {
                        const int f = (r.visitTicks * ac.rate / 30) % int(V.size());
                        mp = gp::selectorPosition({gp::kVisitorX, gp::kVisitorY, 0, 0, 0, 0}, V.anim->frames[size_t(f)].h);
                        have = true;
                    }
                }
                else if (r.nearby < int(r.table.size()))
                {
                    const gp::RoomDecoration &e = r.table[size_t(r.nearby)];
                    const AnimRef D = animRefById(uiIdP(e.sprite, e.palette));
                    if (D.size() > 0)
                    {
                        mp = gp::selectorPosition(e, D.anim->frames[0].h);
                        have = true;
                    }
                }
                const AnimRef M = animRefById(reactAnimId(gp::kMarkerArrowAnim, gp::kMarkerPalette));
                if (have && M.size() > 0)
                {
                    const int f = gp::markerFrame(gameTicks, int(M.size()));
                    const gp::SpriteFrame &s = M.anim->frames[size_t(f)];
                    SDL_FRect rc{float(mp.x) - scrollF - s.anchorX + s.dx, float(mp.y) - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
                    SDL_RenderCopyF(ren, (*M.tex)[size_t(f)], nullptr, &rc);
                }
            }
        };

        // PlayDigForItemRoutine, simplified: settle, bubble + sounded task, then the hit test.
        int &digAnimDY = S.pet.digAnimDY; // 0 = anchor sits at the pet's feet; tune with 9 / 0
        int &digStep = S.pet.digStep, &digTicks = S.pet.digTicks;
        constexpr int kDigAnimId[3] = {0x1c, 0x80, 0xe9}; // PlayTaskAnimationVariant table, by species
        constexpr int kDigTicks = 30;                     // placeholder: length of sound 0xbe (SFX category 9)

        // The oldest hole filling in when a sixth is dug (UpdateDigHoleVisuals 0x03fa5f): the dig animation (bubble 0x16) played
        // backwards, at 28 fps, where that hole was.
        struct HoleFill
        {
            int x, y, ticks;
        };
        std::vector<HoleFill> holeFills;
        constexpr int kHoleFillFps = 0x1c;
        auto tickHoleFills = [&]
        {
            if (holeFills.empty())
                return;
            const int frames = int(animRefById("base:anim/22@28c").size());
            for (HoleFill &f : holeFills)
                ++f.ticks;
            holeFills.erase(std::remove_if(holeFills.begin(), holeFills.end(), [&](const HoleFill &f)
                                           { return f.ticks * kHoleFillFps / 30 >= frames; }),
                            holeFills.end());
        };

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
                sfx(9, 0xbe); // RunSoundedAnimationTask: PlaySoundEffect(9, 0xbe)
                digTicks = 0;
                digStep = 3;
                break;
            case 3: // wait for sound 9 to end (IsSoundIdActive); without an audio device, a fixed time
                if (++digTicks >= (gp::audio().ready() ? 1 : kDigTicks) && !gp::audio().sfxActive(9))
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
                if (r.filledHole && r.filledHole->area == cur->def->legacyId) // the oldest hole fills in
                    holeFills.push_back({r.filledHole->x, r.filledHole->y, 0});

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
                    playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
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
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            }
        };

        // ItemMenuScreen, furnishings (confirmed in MAME): "Display this in your room?" for an item that isn't in the room
        // (Yes starts placing it), "Remove this item from your room?" for one that is (Yes clears every slot holding it),
        // and a furniture or bed that is already shown can only be replaced. Nothing changes how many you own.
        auto useFurnishing = [&](const gp::ItemDef &d)
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
        };

        // An item chosen in give mode: the picker ("How many do you want to give?"), then GiveItemToOtherSave: their count goes
        // up (never past the item's most) and is written to their save, and mine goes down.
        auto giveSelected = [&](const gp::ItemDef &d)
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
        };

        // ItemMenuScreen mode 1: the bed and food dish in your room can't be sold; otherwise ask how many (1 up to what you
        // hold), then Item_ProcessSale: the items go and int(qty * price * 0.8) Bucks come in.
        auto sellSelected = [&](const gp::ItemDef &d)
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
        };
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

        auto useSelectedItem = [&]
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
            if (scripts.fireItemUse(d.id).suppress)
            {
                im.open = false;
                barBusy = 0;
                barAnim.visible = false;
                return;
            }

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
                im.open = false;
                barBusy = 0;
                barAnim.visible = false;
                if (!room)
                    startPotionReaction();
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
                                        clampCam(pos.y - kCamY, cur->img.h, H), W, H);
            loadNpcs();
            exitsArmed = false;
            return true;
        };
        // The outdoor bar's Go Home (HandleMenuBarInput, button 6): the sounds stop, the area becomes area 0 (where the houses are) and
        // the game state becomes the house's room state (MapLocationToGameState), the same state the door sends the pet to: the pet
        // walks in from the door into its home. Leaving the room puts it back at the door in area 0. Unlike the door there is no
        // door sound; the bar's click was the only one.
        goHome = [&](int location)
        {
            requestScreen([&, location]
                          {
                const gp::RoomExitPoint p = gp::roomReturnPoint(location);
                enc.reset();
                encNpc = nullptr;
                if (cur->def->legacyId != 0)
                {
                    const gp::AreaDef *home = db.areaByLegacy(0);
                    auto next = home ? loadArea(db, home->id, showCollision) : nullptr;
                    if (!next)
                    {
                        std::printf("go home: area 0 could not be loaded\n");
                        return;
                    }
                    const int from = cur->def->legacyId;
                    cur = std::move(next);
                    if (gp::shopRowForArea(from) >= 0)
                        pending.consumeUnflaggedForArea(from); // leaving a shop
                    enterAreaEvents(*cur->def, true);
                    refreshBits();
                    saveGame();
                    world = std::make_unique<AreaWorld>(*cur->ground, *cur->runtime, cur->room);
                    SDL_DestroyTexture(tex);
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cur->img.w, cur->img.h);
                    SDL_UpdateTexture(tex, nullptr, cur->img.rgba.data(), cur->img.w * 4);
                    loadNpcs();
                }
                pos = {p.x, p.y}; // (leaving the room puts it here again)
                prevPos = pos;
                cur->runtime->updateVisible(clampCam(pos.x - W / 2, cur->img.w, W), clampCam(pos.y - kCamY, cur->img.h, H), W, H);
                exitsArmed = false;
                if (!room)
                    beginRoom(location); });
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
                player.adjustMoney(-qty * itemDefs[item].price); // Item_ProcessPurchase: AdjustPlayerMoney(-qty * price)
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
        mayorDef = npcAt(gp::kNudgeMayorNpc);

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
            if (d && !d->stationary && (d->behaviorFlags & 0xe0) && d->speciesOrAnim <= 2)
            {
                const int s = d->speciesOrAnim;
                std::vector<int> ids{gp::TrickShowcase::turnAroundAnim(s), gp::TrickShowcase::directionalAnim(s), gp::TrickShowcase::actorReactionAnim(s)};
                for (int level = 1; level <= 3; ++level)
                    for (int id : {gp::TrickShowcase::introAnim(level, s), gp::TrickShowcase::mainAnim(level, s), gp::TrickShowcase::closingAnim(level, s)})
                        ids.push_back(id);
                if (s == 2)
                    for (int n : {2, 3, 4})
                        ids.push_back(gp::TrickShowcase::reactionAnim(n));
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
                const bool gone = !o.goneOnFlag.empty() && player.hasFlag(o.goneOnFlag);
                cur->runtime->setHidden(o.id, (needsOffer && !offered) || !o.availableAt(quests.counter) || gone);
            }
            cur->runtime->updateVisible(camNowX(), camNowY(), W, H);
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
        qh.hold = [&](int ticks, std::function<void()> done) // a quest reward activity holds the pet
        {
            holdTicks = std::max(ticks, 1);
            holdDone = std::move(done);
            // TickActor_BefriendSuccessRewardSequence / RunNPCBefriendEncounter: the NPC is locked for the wait, so it stands
            // still and does not wander off or start something new (UpdateNPCIdleActivity, SetAnimalEncounterLockFlag)
            if (holdNpc)
                holdNpc->locked = false;
            holdNpc = nullptr;
            const gp::NpcActor *near = nearestNpc();
            for (gp::NpcActor &n : npcs)
                if (&n == near && !n.locked && !n.def->stationary)
                {
                    n.locked = true;
                    holdNpc = &n;
                }
        };
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
        qh.onDecline = [&]
        { nudge.restart(long(gameTicks)); }; // DeclineQuest: the 30 s reminder timer
        qh.onNextQuest = [&]
        {
            player.questsCompleted = quests.counter;
            loadNpcs(); // only ever runs from a dialogue callback, never inside talkToNpc's own NpcActor reference
            refreshBits();
            saveGame();
        };
        // Game.hooks().onQuestComplete: true = the script took the bonus rewards over (see QuestHooks::onComplete).
        qh.onComplete = [&]
        {
            const gp::QuestDef &d = quests.q.def;
            gp::QuestCompleteInfo info;
            info.questId = d.generic ? gp::Id() : gp::legacyQuestId(d.id);
            info.number = quests.counter;
            info.generic = d.generic;
            info.type = quests.world.type(d.typeMode).id;
            if (const gp::NpcDef *g = npcAt(d.giver))
                info.giverId = g->id;
            if (const gp::NpcDef *r = npcAt(d.receiver))
                info.receiverId = r->id;
            return scripts.fireQuestComplete(info).suppress;
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
                    const AnimRef A = animRefById(uiIdP(d.worldSprite, gp::kMenuPalette)); // ROM uses item record word 7 as the palette
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

            while (acc >= 1.0 / kTickHz)
            {
                acc -= 1.0 / kTickHz;
                gameTicks += 2;
                // remember where everything was before this tick, for the smooth renderer
                prevPos = pos;
                statNotice.prevOffset = statNotice.offset; // (so a tick that doesn't run it, a dialogue say, doesn't make it shake)
                if (room) // the room before this tick, for the smooth renderer (every tick, so a paused room holds still)
                {
                    room->px = room->x;
                    room->py = room->y;
                    room->pscroll = room->scroll;
                    room->pcurX = room->curX;
                    room->pcurY = room->curY;
                }
                npcPrev.resize(npcs.size());
                for (size_t ni = 0; ni < npcs.size(); ++ni)
                    npcPrev[ni] = {npcs[ni].x(), npcs[ni].y()};
                if (gameTicks - lastSaveTick >= 1800)
                {
                    lastSaveTick = gameTicks;
                    saveGame();
                }
                const Uint8 *k = SDL_GetKeyboardState(nullptr);
                const uint8_t rawDirs = enc ? 0 : gp::pollHeld(gp::keys().held(k, gp::Action::Left), gp::keys().held(k, gp::Action::Right), gp::keys().held(k, gp::Action::Up), gp::keys().held(k, gp::Action::Down));
                const uint8_t newlyPressed = uint8_t(rawDirs & ~prevDirs);

                prevDirs = rawDirs;

                // Z is read once per tick, as an edge. prevZ updates even while a box is open, so the press
                // that opens a box can never also close it.
                const bool zDown = gp::keys().held(k, gp::Action::Select);
                const bool zPressed = zDown && !prevZ;
                const bool zReleased = !zDown && prevZ;
                const bool xDown = gp::keys().held(k, gp::Action::Cancel);
                const bool xPressed = xDown && !prevX;
                static bool prevMenu = false;
                const bool menuDown = gp::keys().held(k, gp::Action::Menu);
                const bool menuPressed = menuDown && !prevMenu;
                prevMenu = menuDown;
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
                if (panelAnim.visible) // the tricks panel (anim 4) and the hand-over from the bar (anim 3)
                {
                    const AnimRef P = barAnimRef(panelAnim.id);
                    const int n = int(P.size());
                    if (n == 0)
                        panelAnim.done = true;
                    else if (!panelAnim.done)
                    {
                        const int idx = ++panelAnim.ticks * kBarRate / 30;
                        panelAnim.frame = panelClosing ? std::max(0, n - 1 - idx) : std::min(idx, n - 1);
                        if (idx >= n)
                            panelAnim.done = true;
                    }
                    if (panelClosing && panelAnim.done)
                    {
                        panelAnim.visible = false;
                        panelClosing = false;
                        bar.forceClose();
                        barBusy = 0;
                    }
                }
                if (barBusy == 5 && (!barAnim.visible || barAnim.done)) // AdvancePetBusyStateMachine: 5 -> 4 once the bar is gone
                    barBusy = 4;
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
                if (!im.open)
                    sellMode = false; // left with X (or nothing left to sell): sell mode is over
                if (im.open && !dialogue.open()) // a box asked from the item screen is answered first
                {
                    itemMenuTick(newlyPressed, xPressed, menuPressed, zPressed, zReleased);
                    if (zPressed && im.open && im.tabs.inGrid())
                        useSelectedItem();
                    continue; // world frozen while the item screen is up
                }
                walk::showNextSay(svc); // text a script queued with say(), once nothing else is being said
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
                    else if (zPressed || (b.kind == gp::BoxKind::Picture && xPressed)) // the map goes with any button
                        dialogue.choose(1);
                    continue;
                }
                if (room)
                {
                    if (!im.open) // the item screen is closed (it was left with X): give mode is over
                        giveMode = false;
                    if (bar.isOpen()) // the house bar: the same keys as outside
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
                    // The pet stands still, and Z and X belong to the bar, while it is opening, open or closing.
                    const bool barHasKeys = barBusy != 0 || bar.isOpen();
                    if (!barHasKeys && !dialogue.open())
                        roomTutorial(); // the first time by the bed or bowl: a message instead of whatever Z would have done
                    tickRoom(barHasKeys ? 0 : rawDirs, barHasKeys ? 0 : newlyPressed, !barHasKeys && zPressed, !barHasKeys && xPressed);
                    tickStatNotice(); // the panel works inside houses too
                    continue;
                }

                if (barBusy == 4 || barBusy == 5) // the tricks panel takes the keys (nothing while it opens or closes)
                {
                    if (barBusy == 4 && !panelClosing)
                    {
                        int ev = -1;
                        if (newlyPressed & gp::DirLeft)
                            ev = 0;
                        else if (newlyPressed & gp::DirRight)
                            ev = 1;
                        else if (newlyPressed & gp::DirUp)
                            ev = 2;
                        else if (newlyPressed & gp::DirDown)
                            ev = 3;
                        if (zPressed)
                            ev = 4;
                        else if (xPressed)
                            ev = 5;
                        if (gp::trickClosesPanel(ev))
                            closeTrickPanel();
                        else if (const int slot = gp::trickSlotForEvent(ev); slot >= 0)
                            pickTrick(slot);
                    }
                }
                else if (bar.isOpen())
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
                const bool petBusyElsewhere = barBusy != 0 || feedStep != 0 || digStep != 0 || swiftDropStep != 0 || washStep != 0 || showNpc || tutStep != 0 || potionStep != 0 || barkStep != 0 || trickStep != 0 || questionStep != 0 || colorStep != 0 || holdTicks > 0;
                // Sitting: a direction or the action button gets the pet up first (activity 8) instead of moving it; the
                // action press is swallowed. Nothing is accepted while it sits down or gets up (see pet_sit.hpp).
                auto sitNow = [&]
                { return gp::sitPhase(sp, animId, pose == Pose::Idle, pose == Pose::Special, animDone); };
                if (!petBusyElsewhere && gp::startsGetUp(sitNow(), rawDirs != 0, zPressed))
                    playAnim(gp::kSitAlt[sp], false, 14, Pose::Special); // PlayActorReactionAnimation: one-shot at 14
                const bool petLocked = petBusyElsewhere || gp::blocksInput(sitNow());
                const uint8_t dirs = petLocked ? 0 : rawDirs;
                if (xPressed && !petLocked && !bar.isOpen() && !dialogue.open() && !enc && !im.open && !pendingExit && !pendingScreen)
                    pressCancelInWorld();
                if (showNpc)
                {
                    gp::NpcActor &t = *showNpc;
                    const int species = t.def->speciesOrAnim == 0xfffe || t.def->speciesOrAnim > 2 ? sp : t.def->speciesOrAnim;
                    if (!showStarted)
                    {
                        if (t.settled()) // wait for any walk or turn it was in the middle of
                        {
                            showStarted = true;
                            showcase.start(showLevel, species, sp);
                        }
                    }
                    else
                    {
                        gp::TrickShowcase::Host sh;
                        sh.play = [&](int id, int rate)
                        {
                            const gp::Id full = reactAnimId(id, t.def->appearancePalette);
                            if (animRefById(full).size() == 0)
                                return false;
                            t.forceAnim(full);
                            t.forcedRate = rate;
                            t.forcedFlip = false; // trick art is drawn facing the camera
                            return true;
                        };
                        sh.busy = [&]
                        { return !t.forcedAnimId.empty() && t.animBusy(); };
                        if (showcase.tick(sh))
                        {
                            t.locked = false;
                            t.clearForced();
                            showNpc = nullptr;
                            std::function<void()> done = std::move(showDone);
                            showDone = nullptr;
                            if (done)
                                done();
                        }
                    }
                }
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
                        playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                        if (friend_ && gp::befriendRoll(npcRng, gp::befriendChance(d.befriendOdds, player.personality[gp::Stars], d.stars)))
                            walk::grantFriendship(svc, d, false);
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
                // TickAllPets: the NPC the pet can talk to (nearestNpc, the same test as pressing Z) stops and turns to
                // face the pet; the pet's side is (npcX <= petX), as in the ROM.
                const gp::NpcActor *talkNpc = nearestNpc();
                // An idle NPC performing a trick (activity 0x13, TickPetNPCBehavior): the same sequence the pet and the teachers
                // run, with the sounds only when the NPC is on screen.
                auto tickNpcTrick = [&](gp::NpcActor &n)
                {
                    if (n.activity != gp::kActivityTrick)
                        return;
                    const int s = n.def->speciesOrAnim == 0xfffe ? sp : n.def->speciesOrAnim;
                    if (n.locked || n.def->stationary || s < 0 || s > 2 || n.trickLevel < 1 || n.trickLevel > 3)
                    {
                        n.endTrick(npcRng);
                        return;
                    }
                    if (!n.trickStarted)
                    {
                        n.trickStarted = true;
                        n.body.facing = gp::FaceDown;
                        n.trick.startWith(n.trickLevel, s, gp::trickRepeatCount(n.trickLevel, s, npcRng));
                    }
                    const bool seen = std::abs(n.x() - pos.x) < W / 2 + 40 && std::abs(n.y() - pos.y) < 150;
                    auto start = [&](int id, int rate, bool reverse)
                    {
                        const gp::Id full = reactAnimId(id, n.def->appearancePalette);
                        if (animRefById(full).size() == 0)
                            return false;
                        n.forceAnim(full);
                        n.forcedRate = rate;
                        n.forcedReverse = reverse;
                        // the monkey's turn-around art faces left, so it is mirrored when it turns to its right
                        n.forcedFlip = (id == gp::TrickShowcase::turnAroundAnim(s) || id == gp::TrickShowcase::directionalAnim(s)) && n.trickFlip;
                        n.trickAnimId = id;
                        return true;
                    };
                    gp::TrickShowcase::Host th;
                    th.full = true;
                    th.play = [&](int id, int rate)
                    { return start(id, rate, false); };
                    th.playReverse = [&](int id, int rate, bool)
                    { return start(id, rate, true); };
                    th.busy = [&]
                    { return !n.forcedAnimId.empty() && n.animBusy(); };
                    th.anim = [&]
                    { return n.trickAnimId; };
                    th.frame = [&]
                    { return n.anim.currentFrame; };
                    th.setFacing = [&](bool faceRight)
                    { n.trickFlip = faceRight; };
                    th.cameraPan = [&]
                    {
                        if (seen)
                            cameraShake.start();
                    };
                    th.sound = [&](int id, int vol)
                    {
                        if (seen)
                            sfx(id, vol);
                    };
                    th.soundActive = [&](int id)
                    { return gp::audio().sfxActive(id); };
                    th.stopSound = [&](int id)
                    { gp::audio().stopSfx(id); };
                    th.rand = [&](int lo, int hi)
                    { return lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive
                    th.seconds = [&]
                    { return double(gameTicks) / 60.0; };
                    if (n.trick.tick(th))
                        n.endTrick(npcRng);
                };
                // An idle NPC's call, happy hop or yawn (activities 1, 2 and 7): the same ReactionSeq the encounters use, run on
                // this NPC by pointing the encounter NPC's hooks at it for the tick.
                auto tickNpcReaction = [&](gp::NpcActor &n)
                {
                    if (n.activity != gp::kActivityNoise && n.activity != gp::kActivityHappy && n.activity != gp::kActivityBored)
                        return;
                    if (n.locked || n.def->stationary || n.def == (encNpc ? encNpc->def : nullptr))
                    {
                        n.endTrick(npcRng);
                        return;
                    }
                    if (!n.react)
                    {
                        n.body.facing = gp::FaceDown;
                        using K = gp::ReactionSeq::Kind;
                        n.react.emplace(n.activity == gp::kActivityNoise ? K::Noise : n.activity == gp::kActivityHappy ? K::Positive : K::Bored,
                                        n.reactCount, n.reactVariant);
                    }
                    gp::NpcActor *saved = encNpc;
                    encNpc = &n;
                    npcMute = !(std::abs(n.x() - pos.x) < W / 2 + 40 && std::abs(n.y() - pos.y) < 150);
                    const bool done = n.react->tick(host.npc);
                    npcMute = false;
                    encNpc = saved;
                    if (done)
                        n.endTrick(npcRng);
                };
                for (gp::NpcActor &n : npcs)
                {
                    // A "sick NPC" quest (typeMode 2) makes its receiver sick for as long as it is accepted and not handed in.
                    n.questSick = quests.q.def.typeMode == 2 && quests.q.phase != 0 && quests.q.def.receiver == n.def->index;
                    n.selected = &n == talkNpc;
                    n.petOnRight = n.x() <= pos.x;
                    n.tick(npcRng);
                    if (n.def->stationary && n.def->behaviorFlags != gp::kNoOverheadAnim) // TickAllPets: the overhead animation
                    {
                        if (n.overheadTick >= 0)
                        {
                            const int frames = int(animRefById(reactAnimId(n.def->behaviorFlags, n.def->appearancePalette)).size());
                            if (++n.overheadTick >= gp::overheadTicks(frames))
                                n.overheadTick = -1;
                        }
                        else if (gp::overheadStarts(npcRng))
                            n.overheadTick = 0;
                    }
                    tickNpcTrick(n);
                    tickNpcReaction(n);
                    updateNpcAnim(n);
                }
                // The stink spreads to whoever the pet stands beside (npc_stink.hpp).
                {
                    gp::NpcActor *beside = nullptr;
                    for (gp::NpcActor &n : npcs)
                        if (&n == talkNpc)
                            beside = &n;
                    const bool roamer = beside && !beside->def->stationary; // a stationary NPC's flags are not moods
                    switch (gp::stinkTransferStep(stinkXfer, quests.counter, player.care[gp::Cleanliness], roamer ? beside->def->index : -1,
                                                  roamer && beside->stinky))
                    {
                    case gp::StinkEvent::NpcStinks:
                        beside->stinky = true;
                        break;
                    case gp::StinkEvent::PetStinks:
                        player.adjustCare(gp::Cleanliness, -gp::kStinkHygieneDrop);
                        saveGame();
                        break;
                    case gp::StinkEvent::None:
                        break;
                    }
                }

                // ---- movement + facing + walk animation ----
                if (dirs)
                {
                    medStuck = false;  // real input ends the stuck walk
                    pose = Pose::Move; // any direction input cancels idle/special animations
                    settleTicks = 0;
                    speedMul = (gp::debugMode() && (SDL_GetModState() & KMOD_SHIFT)) ? 4 : 1; // debug mode: Shift sprints
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
                        playAnim(restIdleId(), true, restIdleRate(), Pose::Idle); // facing down: straight to the base idle, no wait, no turn
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
                            playAnim(gp::kTurn180[sp], false, 14, Pose::Settle, {{gp::kTurn90[sp], false, 14}, {restIdleId(), true, restIdleRate()}});
                        }
                        else
                            playAnim(gp::kTurn90[sp], false, 14, Pose::Settle, {{restIdleId(), true, restIdleRate()}});
                    }
                }

                // The idle follows how the pet is: it changes when the pet falls sick or unhappy, or gets better.
                if (pose == Pose::Idle && isRestIdle(animId) && animId != restIdleId() && animLoop)
                    playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);

                // ---- idle timer: after 3-6 s of base idle the pet sits (action 7: 46 once, then 45 looping) ----
                if (pose == Pose::Idle && isRestIdle(animId) && !enc && !digStep && !washStep && ++idleTicks >= idleThreshold)
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
                        // don't drop to idle between steps (the wash's happy reaction restarts it itself)
                        else if (!enc && !feedStep && !digStep && !washStep && !potionStep && !colorStep && !trickStep) // (a trick plays its own steps)
                            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                    }
                }
                if (medStuck)
                {
                    ++walkTicks; // keep the walk loop animating in place
                    if (++medStuckTicks >= medStuckLimit)
                        playAnim(restIdleId(), true, restIdleRate(), Pose::Idle); // playAnim clears medStuck
                }
                if (overId >= 0)
                    ++overTicks;
                tickFeed();
                tickDig();
                tickHoleFills();
                tickWash();
                tickSwiftDrop();
                if (cur->def->legacyId == gp::kGateArea && gp::updateStatGates(player)) // CheckStatsAndMoneyGate
                {
                    refreshBits();
                    saveGame();
                }
                if (holdTicks > 0 && (--holdTicks == 0 || zPressed)) // the reward activity's wait; Z skips it
                {
                    holdTicks = 0;
                    if (holdNpc)
                        holdNpc->locked = false;
                    holdNpc = nullptr;
                    std::function<void()> d = std::move(holdDone);
                    holdDone = nullptr;
                    if (d)
                        d();
                }
                tickStatNotice();
                tickWorldTutorial();
                tickPotion();
                tickBark();
                tickTrick();
                tickQuestion();
                shakeDy = cameraShake.step(); // AdvanceScriptedCameraPanStep
                tickColorMachine(zDown);
                tickThoughtBubble();
                quests.tick(int(gameTicks));
                if (!petBusyElsewhere && !enc && !dialogue.open() && !im.open && !statsOpen && !pendingExit && !pendingScreen && !room && !showNpc)
                {
                    const gp::NpcActor *nb = nearestNpc();
                    if (nudge.due(long(gameTicks), player.questsCompleted, quests.q.phase, nb ? nb->def->index : -1))
                    {
                        tutNudge = true; // activity 0x26 with event -1
                        startWorldTutorial(-1);
                    }
                }

                // ---- first-time messages: by an item, an object or a pet (HandlePetStageEvents), the first rest while sick,
                // unhappy or dirty (TickPlayerActorIdleBehavior), and the first pickup (ProcessStoryModeObjectInteraction) ----
                if (!petBusyElsewhere && !enc && !dialogue.open() && !pendingExit && !pendingScreen)
                {
                    const gp::ShopEvent *shopT = nearestShopItem();
                    const int itemIdx = events.nearestPrompt(cur->def->legacyId, pos.x, pos.y);
                    const gp::ObjectDef *objT = cur->runtime->nearestInteractable(pos.x, pos.y);
                    const bool itemNear = shopT || itemIdx >= 0;
                    const int item = shopT ? shopT->item : itemIdx >= 0 ? int(events.events()[size_t(itemIdx)].item) : -1;
                    int event = gp::worldTutorial(settings.tutorials, player.tutorialsShown, itemNear, item, objT ? objT->legacySlot : -1,
                                                  nearestNpc() != nullptr);
                    if (event < 0 && pose == Pose::Idle && !afterPickup)
                        event = gp::idleTutorial(settings.tutorials, player.tutorialsShown, !player.isHealthy(),
                                                 player.care[gp::Happiness] < gp::kUnhappyBelow,
                                                 player.care[gp::Cleanliness] <= gp::kDirtyAtMost);
                    if (event < 0 && afterPickup)
                    {
                        afterPickup = false;
                        if (gp::isFirstTime(settings.tutorials, player.tutorialsShown, gp::kTutorialStuff))
                            event = gp::kTutorialStuff;
                    }
                    if (event >= 0)
                        startWorldTutorial(event);
                }

                // ---- exit trigger / talk to NPC ----
                const gp::ObjectDef *nearObj = cur->runtime->nearestInteractable(pos.x, pos.y);
                const bool zWorld = zPressed && !petLocked && tutStep == 0;
                const int curArea = cur->def->legacyId;
                const gp::ShopEvent *shopItem = zWorld ? nearestShopItem() : nullptr;
                const int pickIdx = (zWorld && !shopItem) ? events.nearestPrompt(curArea, pos.x, pos.y) : -1;
                const gp::ExitDef *ie = (zWorld && nearObj && !shopItem && pickIdx < 0) // not while the pet is busy
                                            ? db.findExit(cur->def->id, nearObj->id, false)
                                            : nullptr;
                const gp::ExitDef *ex = nearObj ? db.findExit(cur->def->id, nearObj->id, true) : nullptr;

                // debug: press O to dump the nearest interactable object
                static bool prevO = false;
                const bool oDown = gp::debugMode() && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_O];
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

                if (zWorld && !ie && !enc && !showNpc)
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
            const float camY = clampCamF(petFY - kCamY - shakeDy, cur->img.h, H); // the belly flop moves the camera's lead
            cur->runtime->updateVisible(int(std::floor(camX)), int(std::floor(camY)), W, H);

            // ---- DRAW ----
            gp::clearFrame(ren);
            SDL_FRect dst{-camX, -camY, float(cur->img.w), float(cur->img.h)};
            SDL_RenderCopyF(ren, tex, nullptr, &dst);

            for (const gp::Hole &h : events.holes())
                if (h.area == cur->def->legacyId)
                    drawGroundSprite(gp::uiSpriteId(kHoleSprite, kEvPal), h.x, h.y, camX, camY);
            if (!holeFills.empty()) // a hole filling in: the dig animation backwards
            {
                const AnimRef FA = animRefById("base:anim/22@28c");
                for (const HoleFill &hf : holeFills)
                    if (FA.size())
                    {
                        const int fi = std::max(0, int(FA.size()) - 1 - hf.ticks * kHoleFillFps / 30);
                        const gp::SpriteFrame &fs = FA.anim->frames[size_t(fi)];
                        SDL_FRect fr{hf.x - camX - fs.anchorX + fs.dx, hf.y - camY + digAnimDY - fs.anchorY + fs.dy, float(fs.w), float(fs.h)};
                        SDL_RenderCopyF(ren, (*FA.tex)[size_t(fi)], nullptr, &fr);
                    }
            }

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
            float headX = 0, headY = 0;
            auto drawPet = [&]()
            {
                const bool faceRight = !(facing == 2 && faceLeft);
                const bool panda = sp == 2;
                const bool walking = pose == Pose::Move || pose == Pose::Wait;
                bool flip = false;
                const bool negClass = animId == gp::reactId(gp::ReactAnim::Negative, sp, 1, true) ||
                                      animId == gp::reactId(gp::ReactAnim::Negative, sp, 2, true) ||
                                      animId == gp::reactId(gp::ReactAnim::NegIntro, sp, 0, true);
                // Includes the intro/outro art around the happy hop, which the ROM mirrors with the same rule.
                const bool sideClass = animId == gp::kIdleSide[sp] || animId == gp::kCallSide[sp] ||
                                       gp::isSideReactArt(sp, animId);
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
                headX = x + s.w / 2; // top centre of the drawn frame: where the thought bubble hangs (an approximation)
                headY = y;

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
            else if (overId == gp::kWashBubbleAnim)
                items.push_back({pos.y + 1, 5, nullptr}); // the wash bubble sits over the pet
            items.push_back({pos.y, 2, nullptr});
            std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b)
                             { return a.y < b.y; });

            for (const gp::ShopEvent &e : pending.v)
                if (e.type == 3 && e.area == cur->def->legacyId)
                    items.push_back({e.y, 3, &e});
            // The pet's facing as the bubble offsets read it: 0 left, 1 right, 2 up or down.
            // The ROM's facing flag follows the walk and snaps to "front" when the pet stops, so bubbles re-centre on the pet.
            const int romDir = (pose == Pose::Move || pose == Pose::Wait) && facing == 2 ? (faceLeft ? 0 : 1) : 2;
            // The stink cloud, the personality bubble, the potion's exclamation and the "?" hang over the pet at its depth: objects
            // in front of the pet cover them (the thought bubbles further down are drawn over everything).
            auto drawPetBubbles = [&]()
            {
                if (!enc && !showNpc)
                {
                    int anim = -1;
                    int fps = gp::kStinkFps;
                    if (player.care[gp::Cleanliness] < gp::kStinkBelow)
                        anim = gp::kStinkAnim; // the stink cloud, looping over the pet
                    else
                    {
                        anim = gp::personalityBubble(player.personality[gp::Halos], player.personality[gp::Pitchforks], player.personality[gp::Stars]);
                        fps = gp::kPersonalityBubbleFps;
                    }
                    const AnimRef K = anim >= 0 ? animRefById("base:anim/" + std::to_string(anim) + "@28c") : AnimRef{};
                    if (anim >= 0 && K.size())
                    {
                        const gp::BubbleOffset bo = gp::overheadOffset(sp, romDir, animId);
                        const int f = (stinkTicks * fps / 30) % int(K.size());
                        const gp::SpriteFrame &bs = K.anim->frames[size_t(f)];
                        SDL_FRect rc{petFX - camX + bo.dx - bs.anchorX + bs.dx, petFY - camY + bo.dy + gp::bubbleLower(sp) - bs.anchorY + bs.dy, float(bs.w), float(bs.h)};
                        SDL_RenderCopyF(ren, (*K.tex)[size_t(f)], nullptr, &rc);
                    }
                }
                if (exclaimTicks >= 0) // the exclamation over the head after a potion (bubble 0xb, then 0xc)
                {
                    const AnimRef I = animRefById("base:anim/" + std::to_string(gp::kExclaimIntroAnim) + "@28c");
                    const AnimRef O = animRefById("base:anim/" + std::to_string(gp::kExclaimOutroAnim) + "@28c");
                    const gp::ExclaimView v = gp::exclaimView(exclaimTicks, int(I.size()), int(O.size()));
                    if (v.done)
                        exclaimTicks = -1;
                    else
                    {
                        const AnimRef &A = v.anim == gp::kExclaimOutroAnim ? O : I;
                        if (A.size())
                        {
                            const size_t f = size_t(std::min(v.frame, int(A.size()) - 1));
                            const gp::SpriteFrame &s = A.anim->frames[f];
                            const gp::BubbleOffset bo = gp::overheadOffset(sp, romDir, animId);
                            SDL_FRect rc{petFX - camX + bo.dx - s.anchorX + s.dx, petFY - camY + bo.dy + gp::bubbleLower(sp) - s.anchorY + s.dy, float(s.w), float(s.h)};
                            SDL_RenderCopyF(ren, (*A.tex)[f], nullptr, &rc);
                        }
                    }
                }
                if (questionStep) // the "?" over the pet when it has not learned the trick (activity 0x14)
                {
                    const AnimRef Q = animRefById("base:anim/" + std::to_string(questionBubble) + "@28c");
                    if (Q.size())
                    {
                        const size_t f = questionStep == 2 ? Q.size() - 1 : size_t(std::min(questionTicks * gp::kExclaimFps / 30, int(Q.size()) - 1));
                        const gp::SpriteFrame &s = Q.anim->frames[f];
                        const gp::BubbleOffset bo = gp::overheadOffset(sp, romDir, animId);
                        SDL_FRect rc{petFX - camX + bo.dx - s.anchorX + s.dx, petFY - camY + bo.dy + gp::bubbleLower(sp) - s.anchorY + s.dy, float(s.w), float(s.h)};
                        SDL_RenderCopyF(ren, (*Q.tex)[f], nullptr, &rc);
                    }
                }
            };
            for (const Item &it : items)
            {
                if (it.kind == 0)
                    drawObject(*static_cast<const gp::ObjectDef *>(it.p), camX, camY);
                else if (it.kind == 1)
                    drawNpc(*static_cast<const gp::NpcActor *>(it.p), camX, camY);

                else if (it.kind == 3)
                {
                    const auto &e = *static_cast<const gp::ShopEvent *>(it.p);
                    const AnimRef A = animRefById(uiIdP(itemDefs[e.item].worldSprite, gp::kMenuPalette));
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
                        const int fps = overId == gp::kWashBubbleAnim ? gp::kWashBubbleFps : kBubbleFps;
                        const int f = std::min(overTicks * fps / 30, int(A.size()) - 1);
                        const gp::SpriteFrame &s = A.anim->frames[f];
                        const float ox = petFX - camX, oy = petFY - camY + digAnimDY;
                        SDL_FRect r{ox - s.anchorX + s.dx, oy - s.anchorY + s.dy, float(s.w), float(s.h)};
                        SDL_RenderCopyF(ren, (*A.tex)[f], nullptr, &r);
                    }
                }
                else
                {
                    drawPet();
                    drawPetBubbles();
                }
            }

            if (thoughtIcon >= 0) // the hungry / tired bubble over the pet (DrawActiveBubbles)
            {
                const AnimRef G = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimGrow) + "@28c");
                const AnimRef H = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimHold) + "@28c");
                const gp::ThoughtView v = gp::thoughtView(thoughtTicks, int(G.size()), int(H.size()));
                const AnimRef &A = v.anim == gp::kThoughtAnimHold ? H : G;
                if (!v.done && A.size())
                {
                    const gp::SpriteFrame &s = A.anim->frames[size_t(std::min(v.frame, int(A.size()) - 1))];
                    const gp::BubbleOffset bo = gp::thoughtOffset(sp, romDir, animId);
                    const float bx = petFX - camX + bo.dx, by = petFY - camY + bo.dy + gp::bubbleLower(sp);
                    SDL_FRect rc{bx - s.anchorX + s.dx, by - s.anchorY + s.dy, float(s.w), float(s.h)};
                    SDL_Texture *bt = (*A.tex)[size_t(std::min(v.frame, int(A.size()) - 1))];
                    SDL_SetTextureBlendMode(bt, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(bt, 175); // spawned with layer 0x4000, the shadow layer, which is translucent (as the shadows are)
                    SDL_RenderCopyF(ren, bt, nullptr, &rc);
                    if (v.icon) // DrawTile(icon, 0x28c, x + 0x20, y - 0x23)
                    {
                        const AnimRef I = animRefById(gp::uiSpriteId(thoughtIcon, 0x28c));
                        if (I.size())
                        {
                            const gp::SpriteFrame &t = I.anim->frames[0];
                            // drawn like the sprites: from the tile's own anchor
                            SDL_FRect ic{bx + gp::kThoughtIconDX - t.anchorX + t.dx, by + gp::kThoughtIconDY - t.anchorY + t.dy, float(t.w), float(t.h)};
                            SDL_SetTextureBlendMode((*I.tex)[0], SDL_BLENDMODE_BLEND);
                            SDL_RenderCopyF(ren, (*I.tex)[0], nullptr, &ic);
                        }
                    }
                }
            }

            if (!dialogue.open() && !enc && !showNpc)
            {
                // The map marker (RenderRoomObjects, see selector_icon.hpp): one bobbing arrow or ring over what the action
                // button would act on. A ground item first, else the nearest interactable object, else the selected NPC.
                const int area = cur->def->legacyId;
                const gp::ShopEvent *shopT = nearestShopItem();
                const int itemIdx = events.nearestPrompt(area, pos.x, pos.y);
                const gp::ObjectDef *objT = cur->runtime->nearestInteractable(pos.x, pos.y);
                const gp::NpcActor *npcT = nearestNpc();
                const gp::MarkerKind kind = gp::chooseMarker(shopT || itemIdx >= 0, objT != nullptr,
                                                             objT ? objT->legacySlot : -1, area, npcT != nullptr);
                auto spriteH = [&](const gp::Id &id) // GetSpriteHeight of a tile: its first frame
                {
                    const AnimRef S = animRefById(id);
                    return S.size() ? S.anim->frames[0].h : 0;
                };
                int anim = -1;
                gp::MarkerPos mp{0, 0};
                if (kind == gp::MarkerKind::Item)
                {
                    const int item = shopT ? shopT->item : int(events.events()[size_t(itemIdx)].item);
                    const int ix = shopT ? shopT->x : events.events()[size_t(itemIdx)].x;
                    const int iy = shopT ? shopT->y : events.events()[size_t(itemIdx)].y;
                    const int h = item >= 0 && item < int(itemDefs.size())
                                      ? spriteH(uiIdP(itemDefs[size_t(item)].worldSprite, gp::kMenuPalette))
                                      : 0;
                    anim = gp::itemMarkerAnim(area);
                    mp = gp::itemMarkerPos(area, int(ix - camX), int(iy - camY), h);
                }
                else if (kind == gp::MarkerKind::Npc)
                {
                    const gp::NpcActor &n = *npcT;
                    const AnimRef A = npcAnim(n, std::max(n.anim.animId, 0));
                    const int h = A.size() ? A.anim->frames[std::min(n.anim.currentFrame, int(A.size()) - 1)].h : 0;
                    const auto np = npcPosF(n);
                    anim = gp::kMarkerArrowAnim;
                    mp = gp::npcMarkerPos(n.def->index, int(np.first - camX), int(np.second - camY), h);
                }
                else if (kind == gp::MarkerKind::Object)
                {
                    const gp::ObjectType *t = db.objectType(objT->type);
                    const int w = t ? t->art.w : 0, h = t ? t->art.h : 0;
                    const int left = int(objT->x - camX) - (t ? t->art.anchorX : 0) + (t ? t->art.dx : 0);
                    const int top = int(objT->y - camY) - (t ? t->art.anchorY : 0) + (t ? t->art.dy : 0);
                    mp = gp::objectMarkerPos(left, top, w, h);
                    anim = gp::objectMarkerOnScreen(mp.y) ? gp::kMarkerObjectAnim : -1;
                }
                if (anim >= 0)
                {
                    const AnimRef A = animRefById(reactAnimId(anim, gp::kMarkerPalette));
                    if (A.size())
                    {
                        const int f = gp::markerFrame(gameTicks, int(A.size()));
                        const gp::SpriteFrame &s = A.anim->frames[size_t(f)];
                        SDL_FRect r{float(mp.x) - s.anchorX + s.dx, float(mp.y) - s.anchorY + s.dy + 1, float(s.w), float(s.h)}; // DrawTile's +1
                        SDL_RenderCopyF(ren, (*A.tex)[size_t(f)], nullptr, &r);
                    }
                }
            }
            if (W > 320) // 16:9: the 320 x 240 screens (and the HUD, dialogue and bar) sit in the middle of the wide world
            {
                if (room) // a house is a 1152 px room: it fills the whole wide screen and scrolls with the pet
                    drawRoom();
                gp::useUiScreen(ren);
                const bool wholeBox = dialogue.open() && dialogue.box->kind == gp::BoxKind::Picture; // (a sign is drawn over the world)
                if (im.open || statsOpen || minigames.active() || wholeBox) // full-screen screens: the world is not behind them
                    gp::clearFrame(ren);
            }
            else if (room)
                drawRoom();
            drawQuestHud(barBusy == 0 && feedStep == 0 && digStep == 0 && !im.open && !statsOpen, true);
            if (statNotice.visible() && !im.open && !statsOpen) // DrawStatChangeNotification
            {
                const gp::StatNoticeSlot &sl = gp::kStatNoticeSlots[statNotice.idx];
                const int shift = gp::statNoticeShift(statNotice.shown);
                // The panel is drawn 1 px right and down of the ROM's numbers (seen against the real game); the number uses the
                // dialogue font (font 0).
                const float nOff = statNotice.offsetAt(smoothCam, renderAlpha); // glides between the 5 px steps in smooth mode
                drawUiF(gp::kStatNoticeBg, float(gp::kStatNoticeBgX + 1), nOff + float(gp::kStatNoticeBgDY + 1), 0x28c);
                // The icon's coordinates are its lower right corner (it then sits at the panel's left with the number at the
                // right, as in the game); GUESS from "too low and too far right" with the anchor as the origin.
                if (const AnimRef I = animRefById(gp::uiSpriteId(sl.tile, sl.palette)); I.size())
                {
                    const gp::SpriteFrame &f = I.anim->frames[0];
                    SDL_FRect ir{float(gp::kStatNoticeIconX + shift - f.w + f.dx + gp::kStatNoticeIconNudgeX),
                                 nOff - float(f.h) + float(f.dy + gp::kStatNoticeIconNudgeY), float(f.w), float(f.h)};
                    SDL_RenderCopyF(ren, (*I.tex)[0], nullptr, &ir);
                }
                text.setYShift(nOff - float(statNotice.offset));
                text.draw(std::to_string(statNotice.shown), 0, statNotice.offset + gp::kStatNoticeTextDY, gp::kStatNoticeTextRight - shift,
                          gp::Align::Right);
                text.setYShift(0.f);
            }
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
                gp::drawMoney(itemGfx, player.money); // the money icon and amount, drawn before the grid like the ROM does
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
                {
                    const gp::ItemTabs &tb = im.tabs;
                    if (tb.sliding())
                    {
                        const AnimRef A = animRefById(tabAnimId(tb.sub == gp::MenuSub::ToTabs));
                        if (A.size())
                        {
                            const gp::SpriteFrame &f = A.anim->frames[size_t(std::min(tb.animFrame(), int(A.size()) - 1))];
                            const SDL_Rect r{gp::kTabAnimX - f.anchorX + f.dx, gp::kTabAnimY - f.anchorY + f.dy + 1, f.w, f.h};
                            SDL_Texture *t = (*A.tex)[size_t(std::min(tb.animFrame(), int(A.size()) - 1))];
                            menuCmds.push_back({0x3000, [=]
                                                { SDL_RenderCopy(ren, t, nullptr, &r); }});
                        }
                    }
                    gp::drawTabMarker(itemGfx, rom, tb.inTabs() || tb.sliding() ? tb.tab : im.category, tb.pressed, tb.sub, tb.sliding() ? tb.animFrame() : -1);
                }
                std::stable_sort(menuCmds.begin(), menuCmds.end(), [](const Cmd &a, const Cmd &b)
                                 { return a.layer < b.layer; });
                for (Cmd &c : menuCmds)
                    c.draw();
                if (dialogue.open()) // a question about the selected item, over the item screen
                    drawDialogue(*dialogue.box);
            }
            if (statsOpen)
            {
                gp::clearFrame(ren); // wipe the world drawing
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
            if (barAnim.visible && barBusy != 4 && !im.open && !statsOpen) // (the bar is gone once the panel is up)
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
            if (panelAnim.visible && !im.open && !statsOpen)
            {
                const AnimRef P = barAnimRef(panelAnim.id);
                if (P.size())
                {
                    const int f = std::min(panelAnim.frame, int(P.size()) - 1);
                    const gp::SpriteFrame &s = P.anim->frames[f];
                    SDL_Rect r{gp::kTrickPanelX - s.anchorX + s.dx, gp::kTrickPanelY - s.anchorY + s.dy, s.w, s.h};
                    SDL_RenderCopy(ren, (*P.tex)[f], nullptr, &r);
                }
            }
            if (barBusy == 4 && !panelClosing && !im.open && !statsOpen)
                for (int slot = 0; slot < gp::kTrickSlots; ++slot)
                {
                    const gp::TrickIcon ic = gp::trickIcon(sp, slot, player.tricksKnown);
                    barGfx.tile(ic.sprite, 0x28c, ic.x, ic.y);
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
            gp::useWholeScreen(ren); // the fade covers the whole window
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