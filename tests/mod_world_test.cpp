// mod_world_test.cpp - the world records a mod can add: tileset, collision, objtype, area, object, exit; npc.area as an area id.
#include "../src/mods/mod_loader.hpp"
#include "../src/engine/ground.hpp"
#include "../src/loaders/png.hpp"
#include <iostream>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                   \
    do                                                             \
    {                                                              \
        if (!(c))                                                  \
        {                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                              \
        }                                                          \
    } while (0)

static void writeFile(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

// A w x h picture, every pixel (r, g, b, a).
static void solidPng(const fs::path &p, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    fs::create_directories(p.parent_path());
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4)
    {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    writePng(p.string(), w, h, px);
}

struct TmpMods
{
    fs::path root;
    explicit TmpMods(const std::string &n)
    {
        root = fs::temp_directory_path() / ("gp_worldtest_" + n);
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TmpMods()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void manifest(const std::string &id)
    {
        writeFile(root / id / "manifest.json", "{\"id\":\"" + id + "\",\"version\":\"1.0\",\"game_api\":\"1\"}");
    }
    void data(const std::string &id, const std::string &file, const std::string &json)
    {
        writeFile(root / id / "data" / file, json);
    }
};

static bool mentions(const std::vector<std::string> &v, const std::string &s)
{
    for (const auto &e : v)
        if (e.find(s) != std::string::npos)
            return true;
    return false;
}

// A base game with one area (so a mod can put a door in it), a text and nothing else.
static ContentDB baseDb()
{
    ContentDB db;
    db.put(TextDef{"base:text/1", "Hello"});
    AreaDef a;
    a.id = "base:area/0";
    a.legacyId = 0;
    a.tilesW = a.tilesH = 2;
    db.put(std::move(a));
    ItemDef gem;
    gem.id = "base:item/173";
    db.put(std::move(gem));
    db.put(FindWeightDef{"base:find/265", "base:item/173", 100});
    NpcDef pet;
    pet.id = "base:npc/12";
    pet.area = 0;
    pet.x = 10;
    pet.y = 20;
    pet.speciesOrAnim = 1;
    pet.befriendOdds = 7;
    pet.path = {{5, 0}, {-5, 0}};
    pet.anim = "base:anim/139";
    db.put(std::move(pet));
    MinigameDef pools;
    pools.id = "base:minigame/prize_pools";
    pools.handler = "pools";
    pools.num = {{"special", 177}};
    pools.lists = {{"first", {1, 2}}, {"general", {3, 4}}, {"prizeA", {5}}};
    pools.itemLists = {"first", "general", "prizeA"};
    db.put(std::move(pools));
    MinigameDef mm;
    mm.id = "base:minigame/memory_match";
    mm.handler = "memory_match";
    mm.num = {{"cost", 20}};
    db.put(std::move(mm));
    MinigameDef fp;
    fp.id = "base:minigame/friend_pools";
    fp.handler = "pools";
    fp.lists = {{"gifts", {0, 0}}};
    fp.itemLists = {"gifts"};
    fp.idLists = {{"advice", {"base:text/1"}}, {"defaultLines", {"base:text/1"}}};
    db.put(std::move(fp));
    MinigameDef cmach;
    cmach.id = "base:minigame/colour_machine";
    cmach.handler = "colour_machine";
    cmach.num = {{"stepTicks", 4}};
    cmach.lists = {{"presets", {1, 2, 3}}};
    cmach.paletteLists = {"presets"};
    db.put(std::move(cmach));
    return db;
}

// tiles.png = two tiles side by side (64x32), solid.png = a 16x16 picture
static void assets(TmpMods &t, const std::string &mod)
{
    solidPng(t.root / mod / "assets" / "tiles.png", 64, 32, 10, 200, 10);
    solidPng(t.root / mod / "assets" / "door.png", 16, 24, 200, 100, 0);
}

static const char *kGoodData = R"([
  {"type":"tileset","mode":"add","id":"cave:tileset/rock","png":"assets/tiles.png"},
  {"type":"collision","mode":"add","id":"cave:collision/rock","count":2,"blocked_tiles":[1]},
  {"type":"objtype","mode":"add","id":"cave:objtype/door","png":"assets/door.png","box":[-8,-4,8,0]},
  {"type":"text","mode":"add","id":"cave:str/name","text":"The Cave"},
  {"type":"text","mode":"add","id":"cave:str/sign","text":"Mind your head."},
  {"type":"area","mode":"add","id":"cave:area/main","name":"cave:str/name","tileset":"cave:tileset/rock","collision":"cave:collision/rock",
   "map":[[1,1,1,1],[1,0,0,1],[1,0,0,1]],"music":5,"spawn_points":[[40,40],[60,60]],"max_events":3},
  {"type":"object","mode":"add","id":"cave:obj/door_in","area":"base:area/0","objtype":"cave:objtype/door","x":100,"y":200,"reach":[24,24]},
  {"type":"object","mode":"add","id":"cave:obj/door_out","area":"cave:area/main","objtype":"cave:objtype/door","x":64,"y":80,"reach":[20,20]},
  {"type":"object","mode":"add","id":"cave:obj/sign","area":"cave:area/main","objtype":"cave:objtype/door","x":40,"y":60,"sign":"cave:str/sign"},
  {"type":"exit","mode":"add","id":"cave:exit/in","from":"base:area/0","object":"cave:obj/door_in","to":"cave:area/main","spawn":[64,70]},
  {"type":"exit","mode":"add","id":"cave:exit/out","from":"cave:area/main","object":"cave:obj/door_out","to":"base:area/0","spawn":[100,230],"on_touch":false}
])";

int main()
{
    { // a whole cave
        TmpMods t("good");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "all.json", kGoodData);
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const Tileset *ts = db.tileset("cave:tileset/rock");
        CHECK(ts && ts->count == 2 && ts->tileW == 32 && ts->rgba.size() == 2u * 32 * 32 * 4 && ts->rgba[1] == 200 && ts->rgba[3] == 255);
        const CollisionSet *cs = db.collision("cave:collision/rock");
        CHECK(cs && cs->count == 2 && !cs->isBlocked(0, 5, 5) && cs->isBlocked(1, 5, 5) && cs->isBlocked(2, 0, 0)); // past the end blocks
        const ObjectType *ot = db.objectType("cave:objtype/door");
        CHECK(ot && ot->hasArt && ot->hasMarker && ot->art.w == 16 && ot->art.anchorX == 8 && ot->art.anchorY == 23);
        CHECK(ot && ot->mask.w == 16 && ot->mask.h == 4 && ot->mask.ox == -8 && ot->mask.oy == -4 && ot->mask.blocked.size() == 64);
        const AreaDef *a = db.area("cave:area/main");
        CHECK(a && a->legacyId == 1000 && a->tilesW == 4 && a->tilesH == 3 && a->tiles.size() == 12 && a->tiles[0] == 1 && a->tiles[5] == 0);
        CHECK(a && a->marginL == 42 && a->marginT == 62 && a->music == 5 && a->maxEvents == 3 && a->spawnPoints.size() == 2 && a->name == "cave:str/name");
        CHECK(a && a->objects.size() == 2 && a->objects[0].interactive() && a->objects[1].contextText == "cave:str/sign" && a->objects[1].reachX == 28);
        CHECK(a && a->objects[0].boxLeft == 64 - 8 && a->objects[0].boxTop == 80 - 23 && a->objects[0].boxRight == 64 - 8 + 16);
        CHECK(db.areaByLegacy(1000) == a);
        const AreaDef *base = db.area("base:area/0");
        CHECK(base && base->objects.size() == 1 && base->objects[0].id == "cave:obj/door_in");
        const ExitDef *in = db.findExit("base:area/0", "cave:obj/door_in", true);
        CHECK(in && in->toArea == "cave:area/main" && in->spawnOverride.x == 64 && in->spawnOverride.y == 70 && in->hasSpawnOverride);
        const ExitDef *out = db.findExit("cave:area/main", "cave:obj/door_out", false);
        CHECK(out && out->toArea == "base:area/0" && out->spawnOverride.y == 230 && !db.findExit("cave:area/main", "cave:obj/door_out", true));
        CHECK(db.transition("base:area/0", "cave:area/main") && db.transition("cave:area/main", "base:area/0"));
        // the game's own machinery takes it from here
        GroundField ground(*a, *cs);
        CHECK(ground.walkable(40, 40) && !ground.walkable(10, 10)); // tile (1,1) is 0 = free; tile (0,0) is 1 = blocked
    }
    { // file order does not matter: the area file sorts first, the npc file last, and an npc can name the mod's area
        TmpMods t("order");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a_npc.json", R"([{"type":"npc","mode":"add","id":"cave:npc/bat","name":"cave:str/name","area":"cave:area/main",
                                          "x":10,"y":20,"stationary":true,"anim":"cave:anim/bat","lines":["cave:str/name","cave:str/name","cave:str/name"]}])");
        t.data("cave", "b_area.json", R"([{"type":"area","mode":"add","id":"cave:area/main","name":"cave:str/name","tileset":"cave:tileset/rock",
                                           "collision":"cave:collision/rock","map":[[0,0],[0,0]]}])");
        t.data("cave", "c_stuff.json", R"([
          {"type":"tileset","mode":"add","id":"cave:tileset/rock","png":"assets/tiles.png"},
          {"type":"collision","mode":"add","id":"cave:collision/rock","count":2},
          {"type":"text","mode":"add","id":"cave:str/name","text":"Cave"},
          {"type":"anim","mode":"add","id":"cave:anim/bat","frames":[{"png":"assets/door.png"}]}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const NpcDef *bat = db.npc("cave:npc/bat");
        CHECK(bat && bat->area == 1000);
        CHECK(db.npcsInArea(1000).size() == 1);
    }
    { // a collision sheet in the tileset's own cell size (64): the top-left quarter of tile 1 blocked, scaled to the 32 x 32 grid
        TmpMods t("col64");
        t.manifest("big");
        std::vector<uint8_t> px(128 * 64 * 4, 0); // two 64x64 cells side by side
        for (int y = 0; y < 32; ++y)
            for (int x = 64; x < 96; ++x)
                px[(size_t(y) * 128 + x) * 4 + 3] = 255;
        fs::create_directories(t.root / "big" / "assets");
        writePng((t.root / "big" / "assets" / "col.png").string(), 128, 64, px);
        solidPng(t.root / "big" / "assets" / "tiles.png", 128, 64, 1, 2, 3);
        t.data("big", "d.json", R"([{"type":"tileset","mode":"add","id":"big:tileset/t","png":"assets/tiles.png","tile":64},
                                    {"type":"collision","mode":"add","id":"big:collision/c","png":"assets/col.png","tile":64}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const CollisionSet *cs = db.collision("big:collision/c");
        CHECK(cs && cs->count == 2 && !cs->isBlocked(0, 5, 5) && cs->isBlocked(1, 5, 5) && cs->isBlocked(1, 15, 15) && !cs->isBlocked(1, 16, 5) && !cs->isBlocked(1, 5, 16));
        CHECK(db.tileset("big:tileset/t") && db.tileset("big:tileset/t")->tileW == 64);
    }
    { // two mods with areas get different numbers; a base area id works for an npc too
        TmpMods t("two");
        t.manifest("one");
        t.manifest("two");
        assets(t, "one");
        assets(t, "two");
        for (const char *m : {"one", "two"})
            t.data(m, "d.json", std::string(R"([
              {"type":"tileset","mode":"add","id":")") + m + R"(:tileset/t","png":"assets/tiles.png"},
              {"type":"collision","mode":"add","id":")" + m + R"(:collision/c","count":2},
              {"type":"area","mode":"add","id":")" + m + R"(:area/a","tileset":")" + m + R"(:tileset/t","collision":")" + m + R"(:collision/c","map":[[0]]}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        CHECK(r.ok());
        const AreaDef *a1 = db.area("one:area/a"), *a2 = db.area("two:area/a");
        CHECK(a1 && a2 && a1->legacyId != a2->legacyId && a1->legacyId >= 1000 && a2->legacyId >= 1000);
    }
    { // patch moves a base object (keeping its slot), remove deletes one, and another mod's object can be patched too
        TmpMods t("edit");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", R"([
          {"type":"objtype","mode":"add","id":"cave:objtype/door","png":"assets/door.png"},
          {"type":"text","mode":"add","id":"cave:str/sign","text":"Mind your head."}])");
        t.data("cave", "b.json", R"([
          {"type":"object","mode":"patch","id":"base:area/0/obj/5","area":"base:area/0","x":80,"y":90,"sign":"cave:str/sign"},
          {"type":"object","mode":"patch","id":"base:area/0/obj/6","area":"base:area/0","objtype":"cave:objtype/door","reach":[30,12],"quest_min":2},
          {"type":"object","mode":"remove","id":"base:area/0/obj/7","area":"base:area/0"}])");
        ContentDB db = baseDb();
        for (int i = 5; i <= 7; ++i)
        {
            ObjectDef o;
            o.id = "base:area/0/obj/" + std::to_string(i);
            o.type = "base:objtype/1";
            o.x = 50;
            o.y = 60;
            o.boxLeft = 40;
            o.boxRight = 60;
            o.boxTop = 30;
            o.boxBottom = 60;
            o.legacySlot = i;
            db.areaMutable("base:area/0")->objects.push_back(o);
        }
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const AreaDef *a = db.area("base:area/0");
        CHECK(a && a->objects.size() == 2);
        const ObjectDef &moved = a->objects[0], &retyped = a->objects[1];
        CHECK(moved.id == "base:area/0/obj/5" && moved.x == 80 && moved.y == 90 && moved.legacySlot == 5);
        CHECK(moved.boxLeft == 70 && moved.boxRight == 90 && moved.boxTop == 60 && moved.boxBottom == 90 && moved.type == "base:objtype/1");
        CHECK(moved.contextText == "cave:str/sign" && moved.interactive());
        CHECK(retyped.id == "base:area/0/obj/6" && retyped.type == "cave:objtype/door" && retyped.reachX == 30 && retyped.reachY == 12 && retyped.questMin == 2);
        CHECK(retyped.x == 50 && retyped.boxRight - retyped.boxLeft == 16); // re-measured from the 16x24 art
    }
    { // patch an area: single tiles, then music and spawn points; a whole new map resizes it
        TmpMods t("areapatch");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "b.json", R"([
          {"type":"area","mode":"patch","id":"cave:area/main","set_tiles":[[0,0,0],[3,2,0]],"music":2,"spawn_points":[[7,8]],"margins":[1,2,3,4]}])");
        t.manifest("cave2");
        assets(t, "cave2");
        t.data("cave2", "a.json", R"([
          {"type":"tileset","mode":"add","id":"cave2:tileset/t","png":"assets/tiles.png"},
          {"type":"collision","mode":"add","id":"cave2:collision/c","count":2}])");
        t.data("cave2", "b.json", R"([
          {"type":"area","mode":"patch","id":"cave:area/main","map":[[0,1],[1,0]],"tileset":"cave2:tileset/t","collision":"cave2:collision/c"}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const AreaDef *a = db.area("cave:area/main");
        CHECK(a && a->tilesW == 2 && a->tilesH == 2 && a->tileset == "cave2:tileset/t" && a->tiles[1] == 1 && a->tiles[2] == 1 && a->tiles[3] == 0);
        CHECK(a && a->music == 2 && a->spawnPoints.size() == 1 && a->marginL == 1 && a->marginB == 4);
    }
    { // object shadows: none / auto / a picture on add, and a patch on a base type and on a mod type
        TmpMods t("shadow");
        t.manifest("cave");
        assets(t, "cave");
        solidPng(t.root / "cave" / "assets" / "shade.png", 20, 8, 0, 0, 0, 200);
        t.data("cave", "a.json", R"([
          {"type":"objtype","mode":"add","id":"cave:objtype/plain","png":"assets/door.png"},
          {"type":"objtype","mode":"add","id":"cave:objtype/none","png":"assets/door.png","shadow":"none"},
          {"type":"objtype","mode":"add","id":"cave:objtype/auto","png":"assets/door.png","shadow":"auto"},
          {"type":"objtype","mode":"add","id":"cave:objtype/pic","png":"assets/door.png","shadow_png":"assets/shade.png","shadow_anchorY":2}])");
        t.data("cave", "b.json", R"([
          {"type":"objtype","mode":"patch","id":"base:objtype/7","shadow_png":"assets/shade.png"},
          {"type":"objtype","mode":"patch","id":"base:objtype/8","shadow":"none"},
          {"type":"objtype","mode":"patch","id":"cave:objtype/pic","shadow":"auto"}])");
        ContentDB db = baseDb();
        for (int i = 7; i <= 8; ++i)
        {
            ObjectType ot;
            ot.id = "base:objtype/" + std::to_string(i);
            ot.hasArt = ot.hasMarker = true;
            db.put(std::move(ot));
        }
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        CHECK(db.objectType("cave:objtype/plain")->shadow == ObjectType::ShadowDefault);
        CHECK(db.objectType("cave:objtype/none")->shadow == ObjectType::ShadowNone);
        CHECK(db.objectType("cave:objtype/auto")->shadow == ObjectType::ShadowAuto);
        const ObjectType *b7 = db.objectType("base:objtype/7"), *b8 = db.objectType("base:objtype/8");
        CHECK(b7 && b7->shadow == ObjectType::ShadowPicture && b7->hasMarker && b7->hasArt); // the rest of the type is untouched
        const Anim *sa = db.anim(ObjectType::shadowAnim("base:objtype/7"));
        CHECK(sa && sa->frames.size() == 1 && sa->frames[0].w == 20 && sa->frames[0].h == 8 && sa->frames[0].anchorX == 10 && sa->frames[0].anchorY == 4);
        CHECK(b8 && b8->shadow == ObjectType::ShadowNone);
        CHECK(db.objectType("cave:objtype/pic")->shadow == ObjectType::ShadowAuto); // a later patch wins
        const Anim *pa = db.anim(ObjectType::shadowAnim("cave:objtype/pic"));
        CHECK(pa && pa->frames[0].anchorY == 2); // (the anim stays; the shadow mode decides whether it is used)
    }
    { // resume: off unless an area asks for it; a patch can change it, on a mod's area or a base one
        TmpMods t("resume");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "b.json", R"([
          {"type":"area","mode":"add","id":"cave:area/home","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0,0],[0,0]],"resume":true},
          {"type":"area","mode":"patch","id":"base:area/0","resume":true}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        CHECK(!db.area("cave:area/main")->resume);
        CHECK(db.area("cave:area/home")->resume);
        CHECK(db.area("base:area/0")->resume);
    }
    { // diggable + spot: base areas 0..9 dig, a mod area opts in; spots name an area and an item and may come before them in file order
        TmpMods t("spots");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "0_spots.json", R"([
          {"type":"spot","mode":"add","id":"cave:spot/gem","area":"cave:area/yard","item":"base:item/173","x":40,"y":50,"state":"ground","not_while_held":true},
          {"type":"spot","mode":"add","id":"cave:spot/buried","area":"base:area/0","item":"base:item/173","x":9,"y":8,"requires_item":"base:item/173","not_offered":true},
          {"type":"spot","mode":"remove","id":"base:spot/228"}])");
        t.data("cave", "b.json", R"([
          {"type":"area","mode":"add","id":"cave:area/yard","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0,0],[0,0]],"diggable":true},
          {"type":"area","mode":"patch","id":"base:area/12","diggable":true}])");
        ContentDB db = baseDb();
        db.areaMutable("base:area/0")->diggable = true; // what the ROM import does for areas 0..9
        AreaDef a12;
        a12.id = "base:area/12";
        a12.legacyId = 12;
        db.put(std::move(a12));
        FixedSpotDef old;
        old.id = "base:spot/228";
        old.item = "base:item/173";
        db.put(std::move(old));
        const bool hadBaseSpot = db.spot("base:spot/228") != nullptr;
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        CHECK(db.area("base:area/0")->diggable && db.area("base:area/12")->diggable && !db.area("cave:area/main")->diggable);
        CHECK(db.area("cave:area/yard")->diggable);
        const FixedSpotDef *g = db.spot("cave:spot/gem"), *b = db.spot("cave:spot/buried");
        CHECK(g && g->state == 2 && g->x == 40 && g->y == 50 && g->notWhileHeld && g->area == db.area("cave:area/yard")->legacyId);
        CHECK(b && b->state == 1 && b->notOffered && b->prereqItem == "base:item/173" && b->area == 0);
        CHECK(db.fixedSpotsIn(g->area).size() == 1);
        CHECK(hadBaseSpot && db.spot("base:spot/228") == nullptr);
    }
    { // find rows: add, patch weight / item, remove; the base game's rows are records too
        TmpMods t("finds");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "0_finds.json", R"([
          {"type":"find","mode":"add","id":"cave:find/gem","item":"base:item/173","weight":40},
          {"type":"find","mode":"patch","id":"base:find/265","weight":7},
          {"type":"find","mode":"patch","id":"base:find/6","item":"base:item/173"},
          {"type":"find","mode":"remove","id":"base:find/10"}])");
        ContentDB db = baseDb();
        db.put(FindWeightDef{"base:find/6", "base:item/6", 25});
        db.put(FindWeightDef{"base:find/10", "base:item/10", 25});

        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const FindWeightDef *g = db.findWeight("cave:find/gem");
        CHECK(g && g->weight == 40 && g->item == "base:item/173");
        CHECK(db.findWeight("base:find/265")->weight == 7 && db.findWeight("base:find/6")->item == "base:item/173" && db.findWeight("base:find/6")->weight == 25);
        CHECK(!db.findWeight("base:find/10"));
        const auto rows = db.findWeights();
        CHECK(rows.size() == 3 && rows.back()->id == "cave:find/gem"); // new rows go last, in order
    }
    { // minigame patch: numbers, replace / add / remove in lists, item ids become item numbers
        TmpMods t("minigames");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "0_mg.json", R"([
          {"type":"minigame","mode":"patch","id":"base:minigame/prize_pools","lists":{"first":["base:item/173", 0]},"list_add":{"general":[0,0]},"list_remove":{"general":[3]},"num":{"special":0}},
          {"type":"minigame","mode":"patch","id":"base:minigame/memory_match","num":{"cost":35}}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const MinigameDef &p = db.minigameOr("base:minigame/prize_pools");
        CHECK(p.list("first") == std::vector<int>({db.item("base:item/173")->index, 0}));
        CHECK(p.list("general") == std::vector<int>({4, 0, 0}) && p.list("prizeA") == std::vector<int>({5}) && p.get("special") == 0);
        CHECK(db.minigameOr("base:minigame/memory_match").get("cost") == 35);
    }
    { // text-id lists and the colour presets
        TmpMods t("idlists");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "0_mg.json", R"([
          {"type":"minigame","mode":"patch","id":"base:minigame/friend_pools","list_add":{"advice":["cave:str/sign","cave:str/name"]},"list_remove":{"defaultLines":["base:text/1"]},"lists":{"gifts":["base:item/173"]}},
          {"type":"minigame","mode":"patch","id":"base:minigame/colour_machine","lists":{"presets":[9,8,7,6]},"num":{"stepTicks":2}}])");
        ContentDB db = baseDb();
        db.put(TextDef{"base:text/2", "x"});
        // removing the only default line would empty it: that is the error case below; here keep one
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(!r.ok()); // defaultLines would be empty
    }
    { // the same without the emptying removal
        TmpMods t("idlists2");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "0_mg.json", R"([
          {"type":"minigame","mode":"patch","id":"base:minigame/friend_pools","list_add":{"advice":["cave:str/sign","cave:str/name"]},"lists":{"gifts":["base:item/173"]}},
          {"type":"minigame","mode":"patch","id":"base:minigame/colour_machine","lists":{"presets":[9,8,7,6]},"num":{"stepTicks":2}}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const MinigameDef &fp = db.minigameOr("base:minigame/friend_pools");
        CHECK(fp.idList("advice") == std::vector<Id>({"base:text/1", "cave:str/sign", "cave:str/name"}));
        CHECK(fp.list("gifts") == std::vector<int>({db.item("base:item/173")->index}));
        CHECK(db.minigameOr("base:minigame/colour_machine").list("presets") == std::vector<int>({9, 8, 7, 6}) && db.minigameOr("base:minigame/colour_machine").get("stepTicks") == 2);
    }
    { // palettes: both forms, and an npc that uses one (a copy of a base pet in a new place, and a recoloured base pet)
        TmpMods t("palettes");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "b.json", R"([
          {"type":"npc","mode":"add","id":"cave:npc/pal_pet","based_on":"base:npc/12","area":"cave:area/main","x":100,"y":120,"palette":"cave:pal/ember"},
          {"type":"npc","mode":"replace","id":"base:npc/12","palette":"cave:pal/mono"},
          {"type":"palette","mode":"add","id":"cave:pal/ember","based_on":480,"set":{"3":[255,80,20],"4":null}},
          {"type":"palette","mode":"add","id":"cave:pal/mono","colors":[null,[0,0,0],[255,255,255]]}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const PaletteDef *em = db.palette("cave:pal/ember"), *mo = db.palette("cave:pal/mono");
        CHECK(em && em->basedOn == 480 && em->set.size() == 2 && em->set[0].first == 3 && em->set[0].second == uint16_t((31 << 10) | (10 << 5) | 2) && em->set[1].second == 0x8000);
        CHECK(mo && mo->colors == std::vector<uint16_t>({0x8000, 0, uint16_t((31 << 10) | (31 << 5) | 31)}) && mo->number != em->number);
        const NpcDef *np = db.npc("cave:npc/pal_pet"), *base = db.npc("base:npc/12");
        CHECK(np && np->palette == "cave:pal/ember" && np->x == 100 && np->y == 120 && np->befriendOdds == 7 && np->path.size() == 2 && np->speciesOrAnim == 1);
        CHECK(np && np->area == db.area("cave:area/main")->legacyId && np->index >= 1000 && np->anim == "base:anim/139");
        CHECK(base && base->palette == "cave:pal/mono" && base->x == 10 && base->befriendOdds == 7); // a replace keeps the rest
    }
    { // colour machine presets take palette ids, and a palette can name its species
        TmpMods t("colours");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "b.json", R"([
          {"type":"palette","mode":"add","id":"cave:pal/ember","based_on":480,"species":"monkey"},
          {"type":"palette","mode":"add","id":"cave:pal/any","colors":[null,[1,2,3]]},
          {"type":"minigame","mode":"patch","id":"base:minigame/colour_machine","list_add":{"presets":["cave:pal/ember", 7, "cave:pal/any"]},"list_remove":{"presets":[2]}}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        CHECK(db.palette("cave:pal/ember")->species == 1 && db.palette("cave:pal/any")->species == -1);
        CHECK(db.minigameOr("base:minigame/colour_machine").list("presets") ==
              std::vector<int>({1, 3, db.palette("cave:pal/ember")->number, 7, db.palette("cave:pal/any")->number}));
        CHECK(db.paletteByNumber(db.palette("cave:pal/ember")->number)->id == "cave:pal/ember" && !db.paletteByNumber(5));
    }
    { // an object that opens a scene of a script: Z -> the "scene" interaction; patching an object to one replaces its message
        TmpMods t("scenes");
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "a.json", kGoodData);
        t.data("cave", "b.json", R"([
          {"type":"object","mode":"add","id":"cave:obj/arcade","area":"cave:area/main","objtype":"cave:objtype/door","x":20,"y":30,"scene":"cave:scene/catch"},
          {"type":"object","mode":"add","id":"cave:obj/talker","area":"cave:area/main","objtype":"cave:objtype/door","x":50,"y":30,"message":"cave:str/sign"},
          {"type":"object","mode":"patch","id":"cave:obj/talker","area":"cave:area/main","scene":"cave:scene/other"}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const InteractionDef *i = db.findInteraction("cave:area/main", "cave:obj/arcade");
        CHECK(i && i->action == "scene" && i->str.at("scene") == "cave:scene/catch");
        const InteractionDef *j = db.findInteraction("cave:area/main", "cave:obj/talker");
        CHECK(j && j->action == "scene" && j->str.at("scene") == "cave:scene/other" && !j->str.count("text")); // the message was replaced
        const AreaDef *a = db.area("cave:area/main");
        const auto it = std::find_if(a->objects.begin(), a->objects.end(), [](const ObjectDef &o) { return o.id == "cave:obj/arcade"; });
        CHECK(it != a->objects.end() && it->interactive()); // a scene object can be used without giving a reach
        bool found = false;
        for (const InteractionDef *d : db.allInteractions())
            found = found || d->id == "cave:obj/arcade/interaction";
        CHECK(found);
    }
    // ---- mistakes are errors that say what is wrong ----
    auto bad =[&](const char *name, const std::string &data, const char *expect)
    {
        TmpMods t(name);
        t.manifest("cave");
        assets(t, "cave");
        t.data("cave", "prelude.json", R"([
          {"type":"tileset","mode":"add","id":"cave:tileset/rock","png":"assets/tiles.png"},
          {"type":"collision","mode":"add","id":"cave:collision/rock","count":2},
          {"type":"objtype","mode":"add","id":"cave:objtype/door","png":"assets/door.png"},
          {"type":"area","mode":"add","id":"cave:area/main","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0,0],[0,0]]},
          {"type":"object","mode":"add","id":"cave:obj/plain","area":"cave:area/main","objtype":"cave:objtype/door","x":5,"y":5}])");
        t.data("cave", "z_test.json", data);
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        const bool ok = mentions(r.errors, expect);
        if (!ok)
        {
            std::cerr << "FAIL " << name << ": no error mentioning \"" << expect << "\"; got:\n";
            for (auto &e : r.errors)
                std::cerr << "  " << e << "\n";
            ++g_fail;
        }
    };
    bad("prefix", R"([{"type":"tileset","mode":"add","id":"other:tileset/x","png":"assets/tiles.png"}])", "own prefix");
    bad("dup", R"([{"type":"tileset","mode":"add","id":"cave:tileset/rock","png":"assets/tiles.png"}])", "duplicate");
    bad("mode", R"([{"type":"area","mode":"replace","id":"cave:area/main"}])", "only support mode 'add'");
    bad("tilesize", R"([{"type":"tileset","mode":"add","id":"cave:tileset/t","png":"assets/tiles.png","tile":24}])", "multiple of the tile size");
    bad("nopng", R"([{"type":"tileset","mode":"add","id":"cave:tileset/t","png":"assets/nope.png"}])", "missing file");
    bad("dotdot", R"([{"type":"tileset","mode":"add","id":"cave:tileset/t","png":"../x.png"}])", "'..'");
    bad("colcount", R"([{"type":"collision","mode":"add","id":"cave:collision/t"}])", "'png' or 'count'");
    bad("colboth", R"([{"type":"collision","mode":"add","id":"cave:collision/t","png":"assets/tiles.png","count":2}])", "not both");
    bad("colblocked", R"([{"type":"collision","mode":"add","id":"cave:collision/t","count":2,"blocked_tiles":[5]}])", "outside 0 to 1");
    bad("colpng", R"([{"type":"collision","mode":"add","id":"cave:collision/t","png":"assets/door.png"}])", "multiple of the tile size 32");
    bad("boxes", R"([{"type":"objtype","mode":"add","id":"cave:objtype/t","png":"assets/door.png","box":[0,0,4,4],"collision_png":"assets/door.png"}])", "not both");
    bad("boxorder", R"([{"type":"objtype","mode":"add","id":"cave:objtype/t","png":"assets/door.png","box":[4,0,0,4]}])", "right > left");
    bad("areatileset", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/none","collision":"cave:collision/rock","map":[[0]]}])", "unknown tileset");
    bad("areacol", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/rock","collision":"cave:collision/none","map":[[0]]}])", "unknown collision");
    bad("ragged", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0,0],[0]]}])", "map row 1 has 1 tiles");
    bad("tilerange", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0,2]]}])", "row 0, column 1");
    bad("emptymap", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[]}])", "1 to 255 rows");
    bad("music", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0]],"music":9}])", "'music' must be 0 to 8");
    bad("objarea", R"([{"type":"object","mode":"add","id":"cave:obj/t","area":"cave:area/none","objtype":"cave:objtype/door","x":1,"y":1}])", "unknown area");
    bad("objtype", R"([{"type":"object","mode":"add","id":"cave:obj/t","area":"cave:area/main","objtype":"cave:objtype/none","x":1,"y":1}])", "unknown objtype");
    bad("objxy", R"([{"type":"object","mode":"add","id":"cave:obj/t","area":"cave:area/main","objtype":"cave:objtype/door"}])", "'x' and 'y' are required");
    bad("objtext", R"([{"type":"object","mode":"add","id":"cave:obj/t","area":"cave:area/main","objtype":"cave:objtype/door","x":1,"y":1,"sign":"cave:str/none"}])", "unknown text");
    bad("objdup", R"([{"type":"object","mode":"add","id":"cave:obj/plain","area":"cave:area/main","objtype":"cave:objtype/door","x":1,"y":1}])", "duplicate");
    bad("exitarea", R"([{"type":"exit","mode":"add","id":"cave:exit/t","from":"cave:area/none","object":"cave:obj/plain","to":"cave:area/main","spawn":[1,1]}])", "unknown area");
    bad("exitobj", R"([{"type":"exit","mode":"add","id":"cave:exit/t","from":"cave:area/main","object":"cave:obj/none","to":"base:area/0","spawn":[1,1]}])", "has no object");
    bad("exitreach", R"([{"type":"exit","mode":"add","id":"cave:exit/t","from":"cave:area/main","object":"cave:obj/plain","to":"base:area/0","spawn":[1,1]}])", "no reach");
    bad("exitspawn", R"([{"type":"exit","mode":"add","id":"cave:exit/t","from":"cave:area/main","object":"cave:obj/plain","to":"base:area/0"}])", "no reach"); // (the reach is checked first)
    bad("patchnone", R"([{"type":"object","mode":"patch","id":"cave:obj/none","area":"cave:area/main","x":3}])", "has no such object");
    bad("patcharea", R"([{"type":"object","mode":"patch","id":"cave:obj/plain","area":"cave:area/none","x":3}])", "unknown area");
    bad("patchnoarea", R"([{"type":"object","mode":"patch","id":"cave:obj/plain","x":3}])", "'area'");
    bad("patchrange", R"([{"type":"object","mode":"patch","id":"cave:obj/plain","area":"cave:area/main","x":9999999}])", "'x' must be");
    bad("patchboth", R"([{"type":"object","mode":"patch","id":"cave:obj/plain","area":"cave:area/main","sign":"cave:str/a","message":"cave:str/b"}])", "give one of");
    bad("removenone", R"([{"type":"object","mode":"remove","id":"cave:obj/none","area":"cave:area/main"}])", "has no such object");
    bad("removefields", R"([{"type":"object","mode":"remove","id":"cave:obj/plain","area":"cave:area/main","x":3}])", "takes only");
    bad("objreplace", R"([{"type":"object","mode":"replace","id":"cave:obj/plain","area":"cave:area/main"}])", "'add', 'patch' and 'remove'");
    bad("areapatchnone", R"([{"type":"area","mode":"patch","id":"cave:area/none","music":1}])", "no such area");
    bad("areapatchtile", R"([{"type":"area","mode":"patch","id":"cave:area/main","set_tiles":[[0,0,7]]}])", "set_tiles");
    bad("areapatchpos", R"([{"type":"area","mode":"patch","id":"cave:area/main","set_tiles":[[5,0,1]]}])", "outside the 2x2 map");
    bad("areapatchmusic", R"([{"type":"area","mode":"patch","id":"cave:area/main","music":9}])", "'music' must be 0 to 8");
    bad("areapatchfield", R"([{"type":"area","mode":"patch","id":"cave:area/main","bogus":1}])", "unknown field");
    bad("shadowmode", R"([{"type":"objtype","mode":"add","id":"cave:objtype/s","png":"assets/door.png","shadow":"big"}])", "'shadow' must be");
    bad("shadowboth", R"([{"type":"objtype","mode":"add","id":"cave:objtype/s","png":"assets/door.png","shadow":"none","shadow_png":"assets/door.png"}])", "goes with shadow \"picture\"");
    bad("shadowpicnopng", R"([{"type":"objtype","mode":"add","id":"cave:objtype/s","png":"assets/door.png","shadow":"picture"}])", "needs 'shadow_png'");
    bad("shadowanchor", R"([{"type":"objtype","mode":"add","id":"cave:objtype/s","png":"assets/door.png","shadow_anchorX":3}])", "need 'shadow_png'");
    bad("shadowmissing", R"([{"type":"objtype","mode":"add","id":"cave:objtype/s","png":"assets/door.png","shadow_png":"assets/nope.png"}])", "missing file");
    bad("shadowpatchnone", R"([{"type":"objtype","mode":"patch","id":"cave:objtype/none","shadow":"none"}])", "no such objtype");
    bad("shadowpatchfield", R"([{"type":"objtype","mode":"patch","id":"cave:objtype/door","png":"assets/door.png"}])", "can only change the shadow");
    bad("shadowpatchempty", R"([{"type":"objtype","mode":"patch","id":"cave:objtype/door"}])", "nothing to change");
    bad("resumetype", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0]],"resume":"yes"}])", "'resume' must be true or false");
    bad("resumepatchtype", R"([{"type":"area","mode":"patch","id":"cave:area/main","resume":1}])", "'resume' must be true or false");
    bad("diggabletype", R"([{"type":"area","mode":"add","id":"cave:area/t","tileset":"cave:tileset/rock","collision":"cave:collision/rock","map":[[0]],"diggable":"yes"}])", "'diggable' must be true or false");
    bad("spotarea", R"([{"type":"spot","mode":"add","id":"cave:spot/t","area":"cave:area/none","item":"base:item/173","x":1,"y":1}])", "unknown area");
    bad("spotitem", R"([{"type":"spot","mode":"add","id":"cave:spot/t","area":"base:area/0","item":"cave:item/none","x":1,"y":1}])", "unknown item");
    bad("spotstate", R"([{"type":"spot","mode":"add","id":"cave:spot/t","area":"base:area/0","item":"base:item/173","x":1,"y":1,"state":"hidden"}])", "'state' must be");
    bad("spotpos", R"([{"type":"spot","mode":"add","id":"cave:spot/t","area":"base:area/0","item":"base:item/173","x":1}])", "'x' and 'y' are required");
    bad("spotremove", R"([{"type":"spot","mode":"remove","id":"cave:spot/none"}])", "no such spot");
    bad("findunknown", R"([{"type":"find","mode":"add","id":"cave:find/t","item":"cave:item/none","weight":5}])", "unknown item");
    bad("findweight", R"([{"type":"find","mode":"add","id":"cave:find/t","item":"base:item/173"}])", "'weight' is required");
    bad("findweightrange", R"([{"type":"find","mode":"add","id":"cave:find/t","item":"base:item/173","weight":0}])", "'weight'");
    bad("findpatchnone", R"([{"type":"find","mode":"patch","id":"base:find/none","weight":3}])", "no such find row");
    bad("findpatchempty", R"([{"type":"find","mode":"patch","id":"base:find/265"}])", "nothing to change");
    bad("findremovefield", R"([{"type":"find","mode":"remove","id":"base:find/265","weight":3}])", "unknown field");
    bad("mgunknown", R"([{"type":"minigame","mode":"patch","id":"base:minigame/none","num":{"a":1}}])", "no such minigame");
    bad("mgmode", R"([{"type":"minigame","mode":"add","id":"base:minigame/memory_match","num":{"cost":1}}])", "only support mode 'patch'");
    bad("mgempty", R"([{"type":"minigame","mode":"patch","id":"base:minigame/memory_match"}])", "nothing to change");
    bad("mgnumkey", R"([{"type":"minigame","mode":"patch","id":"base:minigame/memory_match","num":{"cots":1}}])", "it has: cost");
    bad("mgnumtype", R"([{"type":"minigame","mode":"patch","id":"base:minigame/memory_match","num":{"cost":1.5}}])", "whole number");
    bad("mglistkey", R"([{"type":"minigame","mode":"patch","id":"base:minigame/prize_pools","lists":{"firts":[1]}}])", "it has: first");
    bad("mgempty2", R"([{"type":"minigame","mode":"patch","id":"base:minigame/prize_pools","lists":{"prizeA":[]}}])", "cannot be left empty");
    bad("mgemptyrm", R"([{"type":"minigame","mode":"patch","id":"base:minigame/prize_pools","list_remove":{"prizeA":[5]}}])", "cannot be left empty");
    bad("mgitem", R"([{"type":"minigame","mode":"patch","id":"base:minigame/prize_pools","list_add":{"first":["cave:item/none"]}}])", "unknown item");
    bad("mgrange", R"([{"type":"minigame","mode":"patch","id":"base:minigame/prize_pools","list_add":{"first":[99999]}}])", "not an item number");
    bad("mgentry", R"([{"type":"minigame","mode":"patch","id":"base:minigame/prize_pools","list_add":{"first":[-1]}}])", "every entry");
    bad("mgtextunknown", R"([{"type":"minigame","mode":"patch","id":"base:minigame/friend_pools","list_add":{"advice":["cave:str/none"]}}])", "unknown text id");
    bad("mgtextnum", R"([{"type":"minigame","mode":"patch","id":"base:minigame/friend_pools","list_add":{"advice":[5]}}])", "not a text id");
    bad("mgtextempty", R"([{"type":"minigame","mode":"patch","id":"base:minigame/friend_pools","list_remove":{"advice":["base:text/1"]}}])", "cannot be left empty");
    bad("mggiftitem", R"([{"type":"minigame","mode":"patch","id":"base:minigame/friend_pools","list_add":{"gifts":[99999]}}])", "not an item number");
    bad("palboth", R"([{"type":"palette","mode":"add","id":"cave:pal/t","based_on":480,"colors":[[1,2,3]]}])", "exactly one of");
    bad("palnone", R"([{"type":"palette","mode":"add","id":"cave:pal/t"}])", "exactly one of");
    bad("palbase", R"([{"type":"palette","mode":"add","id":"cave:pal/t","based_on":5}])", "ROM palette number");
    bad("palsetidx", R"([{"type":"palette","mode":"add","id":"cave:pal/t","based_on":480,"set":{"x":[1,2,3]}}])", "set index");
    bad("palsetrange", R"([{"type":"palette","mode":"add","id":"cave:pal/t","based_on":480,"set":{"3":[1,2,300]}}])", "[r, g, b]");
    bad("palsetcolors", R"([{"type":"palette","mode":"add","id":"cave:pal/t","colors":[[1,2,3]],"set":{"1":[1,2,3]}}])", "'set' goes with 'based_on'");
    bad("palcolors", R"([{"type":"palette","mode":"add","id":"cave:pal/t","colors":[]}])", "1 to 256");
    bad("npcpalette", R"([{"type":"npc","mode":"replace","id":"base:npc/12","palette":"cave:pal/none"}])", "palette record");
    bad("npcbasedon", R"([{"type":"npc","mode":"add","id":"cave:npc/t","based_on":"base:npc/none"}])", "unknown npc");
    bad("npcbasedonmode", R"([{"type":"npc","mode":"replace","id":"base:npc/12","based_on":"base:npc/12"}])", "only goes with mode add");
    bad("palspecies", R"([{"type":"palette","mode":"add","id":"cave:pal/t","based_on":480,"species":"cat"}])", "'species' must be");
    bad("colourunknown", R"([{"type":"minigame","mode":"patch","id":"base:minigame/colour_machine","list_add":{"presets":["cave:pal/none"]}}])", "unknown palette");
    bad("colourentry", R"([{"type":"minigame","mode":"patch","id":"base:minigame/colour_machine","list_add":{"presets":[5000]}}])", "palette id or an offset");
    bad("scenesign", R"([{"type":"object","mode":"patch","id":"cave:obj/plain","area":"cave:area/main","sign":"cave:str/a","scene":"cave:scene/x"}])", "give one of");
    bad("scenemsg", R"([{"type":"object","mode":"patch","id":"cave:obj/plain","area":"cave:area/main","message":"cave:str/a","scene":"cave:scene/x"}])", "give one of");
    bad("sceneid", R"([{"type":"object","mode":"add","id":"cave:obj/t","area":"cave:area/main","objtype":"cave:objtype/door","x":1,"y":1,"scene":"has space"}])", "scene id");
    bad("scenetype", R"([{"type":"object","mode":"add","id":"cave:obj/t","area":"cave:area/main","objtype":"cave:objtype/door","x":1,"y":1,"scene":5}])", "'scene' must be a string");
    bad("npcarea",R"([{"type":"npc","mode":"add","id":"cave:npc/t","area":"cave:area/none","x":1,"y":1,"anim":"cave:anim/none"}])", "unknown area");

    if (g_fail)
        std::cerr << g_fail << " check(s) failed\n";
    else
        std::cout << "mod_world_test: all passed\n";
    return g_fail ? 1 : 0;
}
