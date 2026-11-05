// room_record_test.cpp - the `room` patch (a home or hotel's exit point, a hotel's furnishing, its table of pieces): fields and errors.
#include "../src/mods/mod_loader.hpp"
#include "../src/game/room_scene.hpp"
#include "../src/game/item.hpp"
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

struct TmpMods
{
    fs::path root;
    explicit TmpMods(const std::string &n)
    {
        root = fs::temp_directory_path() / ("gp_roomrec_" + n);
        fs::remove_all(root);
        fs::create_directories(root);
        writeFile(root / "q" / "manifest.json", R"({"id":"q","version":"1.0","game_api":"1"})");
    }
    ~TmpMods()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void data(const std::string &json) { writeFile(root / "q" / "data" / "a.json", json); }
};

static ContentDB baseDb()
{
    ContentDB db;
    ItemDef it;
    it.id = "q:item/rug";
    it.index = 77;
    it.maxQty = 1;
    db.put(std::move(it));
    return db;
}

static std::vector<std::string> load(const std::string &name, const std::string &records, ContentDB &db)
{
    TmpMods t(name);
    t.data(std::string("[") + records + "]");
    return importMods(db, t.root).errors;
}

int main()
{
    { // a whole patch
        ContentDB db = baseDb();
        const auto errs = load("good", R"({"type":"room","mode":"patch","id":"base:room/4","return":{"x":100,"y":200},"slots":{"2":"q:item/rug","5":""},
          "decorations":[{"index":3,"x":520,"sprite":99},{"index":0,"type":0,"flag":1}]})", db);
        CHECK(errs.empty());
        for (const auto &e : errs)
            std::cerr << e << "\n";
        const RoomDef *r = db.room(4);
        CHECK(r && r->hasReturn && r->returnX == 100 && r->returnY == 200);
        CHECK(r && r->slots.size() == 2 && r->slots.at(2) == "q:item/rug" && r->slots.at(5).empty());
        CHECK(r && r->decor.size() == 2 && r->decor.at(3).has == 0b101 && r->decor.at(3).v[0] == 520 && r->decor.at(3).v[2] == 99 && r->decor.at(0).has == 0b110000);
        CHECK(db.room(5) == nullptr);
        // the exit point: the game's own unless patched
        CHECK(roomReturnPoint(4, r).x == 100 && roomReturnPoint(4, r).y == 200);
        CHECK(roomReturnPoint(5, nullptr).x == 0x7da && roomReturnPoint(5, db.room(5)).y == 0x2ff);
    }
    { // a second patch adds to the first (another mod's, say)
        ContentDB db = baseDb();
        TmpMods t("two");
        t.data(R"([{"type":"room","mode":"patch","id":"base:room/0","return":{"x":1,"y":2}},{"type":"room","mode":"patch","id":"base:room/0","decorations":[{"index":1,"x":5}]}])");
        const auto errs = importMods(db, t.root).errors;
        CHECK(errs.empty());
        const RoomDef *r = db.room(0);
        CHECK(r && r->hasReturn && r->returnX == 1 && r->decor.size() == 1);
    }
    { // errors: each is said, nothing changes
        const char *bad[] = {
            R"({"type":"room","mode":"add","id":"base:room/4","return":{"x":1,"y":1}})",
            R"({"type":"room","mode":"patch","id":"base:room/3","return":{"x":1,"y":1}})",
            R"({"type":"room","mode":"patch","id":"base:room/9","return":{"x":1,"y":1}})",
            R"({"type":"room","mode":"patch","id":"base:room/4"})",
            R"({"type":"room","mode":"patch","id":"base:room/4","return":{"x":1}})",
            R"({"type":"room","mode":"patch","id":"base:room/0","slots":{"2":"q:item/rug"}})",
            R"({"type":"room","mode":"patch","id":"base:room/4","slots":{"40":"q:item/rug"}})",
            R"({"type":"room","mode":"patch","id":"base:room/4","slots":{"2":"q:item/nope"}})",
            R"({"type":"room","mode":"patch","id":"base:room/4","decorations":[{"x":1}]})",
            R"({"type":"room","mode":"patch","id":"base:room/4","decorations":[{"index":9}]})",
            R"({"type":"room","mode":"patch","id":"base:room/4","decorations":[{"index":1,"colour":3}]})",
            R"({"type":"room","mode":"patch","id":"base:room/4","bogus":1})",
        };
        for (const char *b : bad)
        {
            ContentDB db = baseDb();
            CHECK(!load("bad", b, db).empty());
            CHECK(db.room(4) == nullptr && db.room(0) == nullptr);
        }
    }
    { // a mod's wall and floor: an item with a 1152 x 256 picture of its layer of the room
        auto run = [&](const std::string &name, const std::string &records, const char *png, ContentDB &db)
        {
            TmpMods t(name);
            fs::create_directories(t.root / "q" / "assets");
            fs::copy_file(fs::path("tests/assets") / png, t.root / "q" / "assets" / "layer.png", fs::copy_options::overwrite_existing);
            fs::copy_file(fs::path("tests/assets") / "room_small.png", t.root / "q" / "assets" / "icon.png", fs::copy_options::overwrite_existing);
            t.data(std::string("[") + R"({"type":"text","mode":"add","id":"q:str/n","text":"Wall"},
              {"type":"anim","mode":"add","id":"q:anim/i","frames":[{"png":"assets/icon.png","anchorX":0,"anchorY":0}]},)" + records + "]");
            return importMods(db, t.root).errors;
        };
        ContentDB db = baseDb();
        const auto errs = run("wall", R"({"type":"item","mode":"add","id":"q:item/wall","name":"q:str/n","category":"wall","icon":"q:anim/i","room_art":"assets/layer.png","maxQty":1})", "room_layer.png", db);
        CHECK(errs.empty());
        for (const auto &e : errs)
            std::cerr << e << "\n";
        const ItemDef *it = db.item("q:item/wall");
        CHECK(it && it->type == int(ItemType::Wall) && it->use.action == "none");
        const RoomArt *a = db.roomArt("q:item/wall");
        CHECK(a && a->w == 1152 && a->h == 256 && a->rgba.size() == size_t(1152) * 256 * 4 && a->rgba[0] == 40 && a->rgba[2] == 160);
        CHECK(db.roomArt("q:item/rug") == nullptr);
        { // a floor too
            ContentDB d2 = baseDb();
            CHECK(run("floor", R"({"type":"item","mode":"add","id":"q:item/floor","name":"q:str/n","category":"floor","icon":"q:anim/i","room_art":"assets/layer.png"})", "room_layer.png", d2).empty());
            CHECK(d2.item("q:item/floor") && d2.item("q:item/floor")->type == int(ItemType::Floor) && d2.roomArt("q:item/floor"));
        }
        const char *bad[] = {
            R"({"type":"item","mode":"add","id":"q:item/w","name":"q:str/n","category":"wall","icon":"q:anim/i"})",                                                         // needs room_art
            R"({"type":"item","mode":"add","id":"q:item/w","name":"q:str/n","category":"quest","icon":"q:anim/i","room_art":"assets/layer.png"})",                          // only walls and floors
            R"({"type":"item","mode":"add","id":"q:item/w","name":"q:str/n","category":"wall","icon":"q:anim/i","room_art":5})",                                           // not a path
            R"({"type":"item","mode":"add","id":"q:item/w","name":"q:str/n","category":"wall","icon":"q:anim/i","room_art":"assets/missing.png"})",                        // no such file
        };
        for (const char *b : bad)
        {
            ContentDB d3 = baseDb();
            CHECK(!run("badwall", b, "room_layer.png", d3).empty());
            CHECK(d3.roomArt("q:item/w") == nullptr);
        }
        ContentDB d4 = baseDb(); // the wrong size
        CHECK(!run("small", R"({"type":"item","mode":"add","id":"q:item/w","name":"q:str/n","category":"wall","icon":"q:anim/i","room_art":"assets/layer.png"})", "room_small.png", d4).empty());
        CHECK(d4.roomArt("q:item/w") == nullptr);
    }
    { // a mod's window, hanging and decoration: an item with `decor_art` (an anim)
        auto run = [&](const std::string &name, const std::string &records, ContentDB &db)
        {
            TmpMods t(name);
            fs::create_directories(t.root / "q" / "assets");
            fs::copy_file(fs::path("tests/assets") / "room_small.png", t.root / "q" / "assets" / "icon.png", fs::copy_options::overwrite_existing);
            t.data(std::string("[") + R"({"type":"text","mode":"add","id":"q:str/n","text":"Lamp"},
              {"type":"anim","mode":"add","id":"q:anim/i","frames":[{"png":"assets/icon.png","anchorX":0,"anchorY":0}]},
              {"type":"anim","mode":"add","id":"q:anim/lamp","frames":[{"png":"assets/icon.png","anchorX":10,"anchorY":50}]},)" + records + "]");
            return importMods(db, t.root).errors;
        };
        for (const char *cat : {"window", "hanging", "decor"})
        {
            ContentDB db = baseDb();
            const auto errs = run(cat, std::string(R"({"type":"item","mode":"add","id":"q:item/d","name":"q:str/n","icon":"q:anim/i","decor_art":"q:anim/lamp","maxQty":3,"category":")") + cat + "\"}", db);
            CHECK(errs.empty());
            for (const auto &e : errs)
                std::cerr << e << "\n";
            const ItemDef *it = db.item("q:item/d");
            const int want = std::string(cat) == "window" ? int(ItemType::Window) : std::string(cat) == "hanging" ? int(ItemType::HangingDecor) : int(ItemType::PlacedDecor);
            CHECK(it && it->type == want && isFurnishingType(it->type) && it->use.action == "none");
            CHECK(it && it->decorSprite != 0xffff && isModSprite(it->decorSprite) && it->decorSprite != it->iconSprite && it->w7 == kMenuPalette && it->w9 == 0xffff);
        }
        const char *bad[] = {
            R"({"type":"item","mode":"add","id":"q:item/d","name":"q:str/n","icon":"q:anim/i","category":"decor"})",                                         // needs decor_art
            R"({"type":"item","mode":"add","id":"q:item/d","name":"q:str/n","icon":"q:anim/i","category":"decor","decor_art":"q:anim/nope"})",                // no such anim
            R"({"type":"item","mode":"add","id":"q:item/d","name":"q:str/n","icon":"q:anim/i","category":"quest","decor_art":"q:anim/lamp"})",               // only furnishings
            R"({"type":"item","mode":"add","id":"q:item/d","name":"q:str/n","icon":"q:anim/i","category":"decor","decor_art":7})",                            // not an anim id
        };
        for (const char *b : bad)
        {
            ContentDB db = baseDb();
            CHECK(!run("baddecor", b, db).empty());
            CHECK(db.item("q:item/d") == nullptr);
        }
    }
    if (g_fail)
    {
        std::cerr << "room_record_test: " << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "room_record_test: all passed\n";
    return 0;
}
