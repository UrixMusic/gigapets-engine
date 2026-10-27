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

    std::cout << (g_fail ? "FAILED\n" : "mod_loader_test: all passed\n");
    return g_fail ? 1 : 0;
}