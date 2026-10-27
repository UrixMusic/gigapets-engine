// mod_loader_test.cpp - manifest, ordering, and the four modes on the text record.
#include "../src/mods/mod_loader.hpp"
#include <iostream>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                          \
    do                                                                    \
    {                                                                     \
        if (!(c))                                                         \
        {                                                                 \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n";        \
            ++g_fail;                                                     \
        }                                                                 \
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
        root = fs::temp_directory_path() / ("gp_modtest_" + n);
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TmpMods()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void mod(const std::string &id, const std::string &data, const std::string &extra = "", const std::string &api = "1")
    {
        writeFile(root / id / "manifest.json",
                  "{\"id\":\"" + id + "\",\"version\":\"1.0\",\"game_api\":\"" + api + "\"" + extra + "}");
        if (!data.empty())
            writeFile(root / id / "data" / "t.json", data);
    }
};

static bool mentions(const std::vector<std::string> &v, const std::string &s)
{
    for (const auto &e : v)
        if (e.find(s) != std::string::npos)
            return true;
    return false;
}

static ContentDB baseDb()
{
    ContentDB db;
    db.put(TextDef{"base:text/5", "Old Name"});
    db.put(TextDef{"base:text/6", "Other"});
    return db;
}

int main()
{
    { // replace renames an NPC's text
        TmpMods t("replace");
        t.mod("rename", R"([{"type":"text","mode":"replace","id":"base:text/5","text":"New Name"}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        CHECK(r.ok());
        CHECK(db.textOr("base:text/5") == "New Name");
        CHECK(r.lastWriter["base:text/5"] == "rename");
    }
    { // add: own prefix ok, foreign prefix and duplicate rejected
        TmpMods t("add");
        t.mod("pie", R"([
          {"type":"text","mode":"add","id":"pie:text/a","text":"Pie"},
          {"type":"text","mode":"add","id":"other:text/a","text":"x"},
          {"type":"text","mode":"add","id":"pie:text/a","text":"again"}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        CHECK(!r.ok());
        CHECK(db.textOr("pie:text/a") == "Pie");
        CHECK(!db.hasText("other:text/a"));
        CHECK(mentions(r.errors, "prefix"));
        CHECK(mentions(r.errors, "duplicate"));
    }
    { // remove, and replace of a missing id
        TmpMods t("remove");
        t.mod("rm", R"([{"type":"text","mode":"remove","id":"base:text/6"},
                        {"type":"text","mode":"replace","id":"base:text/99","text":"x"}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        CHECK(!db.hasText("base:text/6"));
        CHECK(mentions(r.errors, "no such record"));
        CHECK(!r.warnings.empty());
    }
    { // later mod wins; requires orders them regardless of folder names
        TmpMods t("order");
        t.mod("zzz_first", R"([{"type":"text","mode":"replace","id":"base:text/5","text":"first"}])");
        t.mod("aaa_second", R"([{"type":"text","mode":"replace","id":"base:text/5","text":"second"}])",
              R"(,"requires":["zzz_first"])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        CHECK(r.ok());
        CHECK(r.loaded.size() == 2 && r.loaded[0] == "zzz_first");
        CHECK(db.textOr("base:text/5") == "second");
    }
    { // cycle, missing dependency, api mismatch
        TmpMods t("bad");
        t.mod("a", "", R"(,"requires":["b"])");
        t.mod("b", "", R"(,"requires":["a"])");
        ContentDB db = baseDb();
        CHECK(mentions(importMods(db, t.root).errors, "cycle"));

        TmpMods t2("missing");
        t2.mod("a", "", R"(,"requires":["ghost"])");
        CHECK(mentions(importMods(db, t2.root).errors, "not installed"));

        TmpMods t3("api");
        t3.mod("a", "", "", "2");
        CHECK(mentions(importMods(db, t3.root).errors, "game_api"));
    }
    { // typos are loud
        TmpMods t("typo");
        t.mod("typo", R"([{"type":"txet","mode":"add","id":"typo:x","text":"x"},
                          {"type":"text","mode":"add","id":"typo:y","txt":"x"}])");
        ContentDB db = baseDb();
        ModReport r = importMods(db, t.root);
        CHECK(mentions(r.errors, "unknown record type"));
        CHECK(mentions(r.errors, "unknown field"));
    }

    // ---- items. A 1x1 transparent PNG for anim records.
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
        0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
        0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78,
        0x9C, 0x63, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0xA5, 0xF6,
        0x45, 0x40, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    auto writePng = [&](const TmpMods &t, const std::string &mod)
    {
        fs::create_directories(t.root / mod / "assets");
        std::ofstream(t.root / mod / "assets" / "dot.png", std::ios::binary)
            .write(reinterpret_cast<const char *>(kPng), sizeof kPng);
    };
    auto itemDb = [&]
    {
        ContentDB db = baseDb();
        ItemDef b;
        b.id = "base:item/1";
        b.name = "base:text/5";
        b.type = 9;
        b.price = 1;
        b.maxQty = 7;
        b.iconSprite = 0x40;
        b.worldSprite = 0x41;
        db.put(b);
        return db;
    };
    { // a good item; the item record comes first in the file and still finds the text and anims
        TmpMods t("item_ok");
        t.mod("pie", R"([
          {"type":"item","mode":"add","id":"pie:item/pie","name":"pie:text/pie","plural":"pie:text/pies",
           "category":"food","maxQty":5,"price":30,"icon":"pie:anim/icon","world":"pie:anim/ground",
           "use":{"action":"food","num":{"hunger":4,"happiness":2},"str":{"stat":"halos"}}},
          {"type":"item","mode":"add","id":"pie:item/plain","name":"pie:text/pie","category":"quest","icon":"pie:anim/icon"},
          {"type":"text","mode":"add","id":"pie:text/pie","text":"Pie"},
          {"type":"text","mode":"add","id":"pie:text/pies","text":"Pies"},
          {"type":"anim","mode":"add","id":"pie:anim/icon","frames":[{"png":"assets/dot.png","anchorX":0,"anchorY":0}]},
          {"type":"anim","mode":"add","id":"pie:anim/ground","frames":[{"png":"assets/dot.png"}]}])");
        writePng(t, "pie");
        ContentDB db = itemDb();
        ModReport r = importMods(db, t.root);
        for (auto &e : r.errors)
            std::cerr << e << "\n";
        CHECK(r.ok());
        const ItemDef *d = db.item("pie:item/pie");
        CHECK(d && d->name == "pie:text/pie" && d->plural == "pie:text/pies");
        CHECK(d && d->type == int(ItemType::Food) && d->maxQty == 5 && d->price == 30);
        CHECK(d && d->use.action == "food" && d->use.num.at("hunger") == 4 && d->use.str.at("stat") == "halos");
        CHECK(d && d->index == 1);                                   // after base:item/1
        CHECK(d && isModSprite(d->iconSprite) && isModSprite(d->worldSprite) && d->iconSprite != d->worldSprite);
        CHECK(d && db.anim(uiSpriteId(d->iconSprite, kMenuPalette)) && db.anim(uiSpriteId(d->worldSprite, kMenuPalette)));
        CHECK(d && db.anim(uiSpriteId(d->iconSprite, kMenuPalette))->frames[0].anchorX == 0);
        const ItemDef *q = db.item("pie:item/plain"); // quest items default to doing nothing; world defaults to icon
        CHECK(q && q->use.action == "none" && q->type == int(ItemType::GenericQuestItem) && q->worldSprite == q->iconSprite);
        CHECK(r.lastWriter["pie:item/pie"] == "pie");
    }
    { // replace keeps the index and every field not given; icon swaps in mod art
        TmpMods t("item_replace");
        t.mod("tweak", R"([
          {"type":"item","mode":"replace","id":"base:item/1","price":99},
          {"type":"item","mode":"replace","id":"base:item/1","icon":"tweak:anim/a"},
          {"type":"anim","mode":"add","id":"tweak:anim/a","frames":[{"png":"assets/dot.png"}]}])");
        writePng(t, "tweak");
        ContentDB db = itemDb();
        ModReport r = importMods(db, t.root);
        CHECK(r.ok());
        const ItemDef *d = db.item("base:item/1");
        CHECK(d && d->index == 0 && d->price == 99 && d->maxQty == 7 && d->type == 9 && d->name == "base:text/5");
        CHECK(d && isModSprite(d->iconSprite) && d->worldSprite == 0x41); // only the icon changed
    }
    { // the same id twice: the first wins, the second is a loud duplicate error
        TmpMods t("item_dup");
        t.mod("dup", R"([
          {"type":"text","mode":"add","id":"dup:text/n","text":"N"},
          {"type":"anim","mode":"add","id":"dup:anim/a","frames":[{"png":"assets/dot.png"}]},
          {"type":"item","mode":"add","id":"dup:item/a","name":"dup:text/n","category":"quest","icon":"dup:anim/a","price":1},
          {"type":"item","mode":"add","id":"dup:item/a","name":"dup:text/n","category":"quest","icon":"dup:anim/a","price":2}])");
        writePng(t, "dup");
        ContentDB db = itemDb();
        ModReport r = importMods(db, t.root);
        CHECK(!r.ok() && mentions(r.errors, "duplicate"));
        CHECK(db.item("dup:item/a") && db.item("dup:item/a")->price == 1 && db.items().size() == 2);
    }
    { // every kind of mistake is a loud error and adds nothing
        TmpMods t("item_bad");
        t.mod("bad", R"([
          {"type":"anim","mode":"add","id":"bad:anim/a","frames":[{"png":"assets/dot.png"}]},
          {"type":"text","mode":"add","id":"bad:text/n","text":"N"},
          {"type":"item","mode":"add","id":"bad:item/noanim","name":"bad:text/n","category":"food","icon":"bad:anim/zzz","use":{"action":"none"}},
          {"type":"item","mode":"add","id":"bad:item/notext","name":"bad:text/zzz","category":"food","icon":"bad:anim/a","use":{"action":"none"}},
          {"type":"item","mode":"add","id":"bad:item/cat","name":"bad:text/n","category":"gadget","icon":"bad:anim/a","use":{"action":"none"}},
          {"type":"item","mode":"add","id":"bad:item/nouse","name":"bad:text/n","category":"food","icon":"bad:anim/a"},
          {"type":"item","mode":"add","id":"bad:item/action","name":"bad:text/n","category":"food","icon":"bad:anim/a","use":{"action":"explode"}},
          {"type":"item","mode":"add","id":"bad:item/param","name":"bad:text/n","category":"food","icon":"bad:anim/a","use":{"action":"food","num":{"hungr":1}}},
          {"type":"item","mode":"add","id":"bad:item/stat","name":"bad:text/n","category":"food","icon":"bad:anim/a","use":{"action":"food","str":{"stat":"luck"},"num":{"raise":1}}},
          {"type":"item","mode":"add","id":"bad:item/both","name":"bad:text/n","category":"food","icon":"bad:anim/a","use":{"action":"food","str":{"stat":"halos"},"num":{"raise":1,"lower":1}}},
          {"type":"item","mode":"add","id":"bad:item/qty","name":"bad:text/n","category":"food","icon":"bad:anim/a","maxQty":0,"use":{"action":"none"}},
          {"type":"item","mode":"add","id":"bad:item/noicon","name":"bad:text/n","category":"food","use":{"action":"none"}},
          {"type":"item","mode":"add","id":"bad:item/noname","category":"food","icon":"bad:anim/a","use":{"action":"none"}},
          {"type":"item","mode":"add","id":"other:item/x","name":"bad:text/n","category":"food","icon":"bad:anim/a","use":{"action":"none"}},
          {"type":"item","mode":"add","id":"bad:item/field","name":"bad:text/n","category":"food","icon":"bad:anim/a","use":{"action":"none"},"colour":"red"},
          {"type":"item","mode":"replace","id":"base:item/404","price":1},
          {"type":"item","mode":"remove","id":"base:item/1"},
          {"type":"item","mode":"patch","id":"base:item/1","price":1}])");
        writePng(t, "bad");
        ContentDB db = itemDb();
        ModReport r = importMods(db, t.root);
        CHECK(!r.ok());
        CHECK(mentions(r.errors, "unknown anim 'bad:anim/zzz'"));
        CHECK(mentions(r.errors, "unknown text 'bad:text/zzz'"));
        CHECK(mentions(r.errors, "'category' must be"));
        CHECK(mentions(r.errors, "missing 'use'"));
        CHECK(mentions(r.errors, "unknown use action 'explode'"));
        CHECK(mentions(r.errors, "has no number 'hungr'"));
        CHECK(mentions(r.errors, "use.str.stat must be"));
        CHECK(mentions(r.errors, "'raise' or 'lower', not both"));
        CHECK(mentions(r.errors, "'maxQty' must be an integer from 1 to 99"));
        CHECK(mentions(r.errors, "missing 'icon'"));
        CHECK(mentions(r.errors, "missing 'name'"));
        CHECK(mentions(r.errors, "prefix"));
        CHECK(mentions(r.errors, "unknown field 'colour'"));
        CHECK(mentions(r.errors, "no such item"));
        CHECK(mentions(r.errors, "remove on item"));
        CHECK(mentions(r.errors, "patch on item"));
        CHECK(db.items().size() == 1); // only base:item/1; no bad record was added
        CHECK(db.item("base:item/1")->price == 1);
    }

    std::cout << (g_fail ? "FAILED\n" : "mod_loader_test: all passed\n");
    return g_fail ? 1 : 0;
}