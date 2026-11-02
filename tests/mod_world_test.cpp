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
    // ---- mistakes are errors that say what is wrong ----
    auto bad = [&](const char *name, const std::string &data, const char *expect)
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
    bad("patchboth", R"([{"type":"object","mode":"patch","id":"cave:obj/plain","area":"cave:area/main","sign":"cave:str/a","message":"cave:str/b"}])", "not both");
    bad("removenone", R"([{"type":"object","mode":"remove","id":"cave:obj/none","area":"cave:area/main"}])", "has no such object");
    bad("removefields", R"([{"type":"object","mode":"remove","id":"cave:obj/plain","area":"cave:area/main","x":3}])", "takes only");
    bad("objreplace", R"([{"type":"object","mode":"replace","id":"cave:obj/plain","area":"cave:area/main"}])", "'add', 'patch' and 'remove'");
    bad("areapatchnone", R"([{"type":"area","mode":"patch","id":"cave:area/none","music":1}])", "no such area");
    bad("areapatchtile", R"([{"type":"area","mode":"patch","id":"cave:area/main","set_tiles":[[0,0,7]]}])", "set_tiles");
    bad("areapatchpos", R"([{"type":"area","mode":"patch","id":"cave:area/main","set_tiles":[[5,0,1]]}])", "outside the 2x2 map");
    bad("areapatchmusic", R"([{"type":"area","mode":"patch","id":"cave:area/main","music":9}])", "'music' must be 0 to 8");
    bad("areapatchfield", R"([{"type":"area","mode":"patch","id":"cave:area/main","bogus":1}])", "unknown field");
    bad("npcarea", R"([{"type":"npc","mode":"add","id":"cave:npc/t","area":"cave:area/none","x":1,"y":1,"anim":"cave:anim/none"}])", "unknown area");

    if (g_fail)
        std::cerr << g_fail << " check(s) failed\n";
    else
        std::cout << "mod_world_test: all passed\n";
    return g_fail ? 1 : 0;
}
