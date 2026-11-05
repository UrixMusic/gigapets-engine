// quest_type_test.cpp - the `quest_type` record (a kind of generated quest a mod adds): fields, errors, and its own text lines.
#include "../src/mods/mod_loader.hpp"
#include "../src/game/quest_types.hpp"
#include "../src/game/quest_text.hpp"
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
        root = fs::temp_directory_path() / ("gp_questtype_" + n);
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
    db.put(TextDef{"base:text/1", "Hello"});
    for (const char *i : {"base:item/173", "base:item/174"})
    {
        ItemDef it;
        it.id = i;
        it.maxQty = 5;
        db.put(std::move(it));
    }
    NpcDef n;
    n.id = "base:npc/12";
    n.anim = "base:anim/139";
    db.put(std::move(n));
    for (const QuestTypeDef &t : baseQuestTypes())
        db.put(t);
    return db;
}

static const char *kTexts = R"({"type":"text","mode":"add","id":"q:str/req1","text":"Please fetch {list} for {npc}."},
  {"type":"text","mode":"add","id":"q:str/req2","text":"Bring {item}."},
  {"type":"text","mode":"add","id":"q:str/done","text":"Lovely!"},)";

static std::vector<std::string> load(const std::string &name, const std::string &records, ContentDB &db)
{
    TmpMods t(name);
    t.data(std::string("[") + kTexts + records + "]");
    return importMods(db, t.root).errors;
}

int main()
{
    { // a whole record
        ContentDB db = baseDb();
        const size_t before = db.questTypes().size();
        const auto errs = load("good", R"({"type":"quest_type","mode":"add","id":"q:questtype/delivery",
          "pool":[{"item":"base:item/173","weight":3,"buried":true},{"item":"base:item/174"}],
          "count_lo":1,"count_hi":2,"objective_count":[{"from":0,"lo":1,"hi":2}],"bonus_count":[{"from":0,"lo":0,"hi":1}],
          "spawn":[{"item":"base:item/174","count":2}],"fixed_area_tier":1,"excluded_tiers":[5,6],"timed":true,"spawns_on_accept":false,
          "receiver":"other","roll_lo":10,"roll_hi":12,"roll_min_total":3,
          "request_text":["q:str/req1","q:str/req2"],"complete_text":["q:str/done"],"reminder_text":["q:str/done"]})", db);
        CHECK(errs.empty());
        for (const auto &e : errs)
            std::cerr << e << "\n";
        CHECK(db.questTypes().size() == before + 1);
        const QuestTypeDef *t = nullptr;
        for (const QuestTypeDef &x : db.questTypes())
            if (x.id == "q:questtype/delivery")
                t = &x;
        CHECK(t != nullptr);
        if (t)
        {
            CHECK(t->mode >= 100 && t->pool.size() == 2 && t->pool[0].weight == 3 && t->pool[0].buried == 1 && t->countHi == 2 && t->timed && !t->spawnsOnAccept);
            CHECK(t->receiver == QuestReceiver::Other && t->rollLo == 10 && t->rollHi == 12 && t->rollMinTotal == 3 && t->fixedAreaTier == 1 && t->excludedTiers.size() == 2);
            CHECK(t->requestTexts.size() == 2 && t->completeTexts.size() == 1 && t->reminderTexts.size() == 1 && t->spawn.size() == 1 && t->spawn[0].count == 2);
            // the same id always gets the same number, whatever else is loaded
            ContentDB db2 = baseDb();
            load("again", R"({"type":"quest_type","mode":"add","id":"q:questtype/delivery"})", db2);
            int mode2 = -1;
            for (const QuestTypeDef &x : db2.questTypes())
                if (x.id == "q:questtype/delivery")
                    mode2 = x.mode;
            CHECK(mode2 == t->mode);
        }
    }
    { // a fixed receiver, and a pool entry that depends on the pet's kind
        ContentDB db = baseDb();
        const auto errs = load("fixed", R"({"type":"quest_type","mode":"add","id":"q:questtype/fx","receiver":"fixed","fixed_npc":"base:npc/12",
          "pool":[{"by_species":[{"species":0,"item":"base:item/173","alt_item":"base:item/174","alt_buried":true},{"species":-1,"item":"base:item/174"}],"weight":2}]})", db);
        CHECK(errs.empty());
        for (const auto &e : errs)
            std::cerr << e << "\n";
        const QuestTypeDef *t = nullptr;
        for (const QuestTypeDef &x : db.questTypes())
            if (x.id == "q:questtype/fx")
                t = &x;
        CHECK(t && t->receiver == QuestReceiver::Fixed && t->fixedNpcId == "base:npc/12");
        CHECK(t && t->pool.size() == 1 && t->pool[0].bySpecies.size() == 2 && t->pool[0].bySpecies[0].species == 0 && t->pool[0].bySpecies[0].altItem == "base:item/174" &&
              t->pool[0].bySpecies[0].altBuried == 1 && t->pool[0].bySpecies[1].species == -1);
        for (const char *b : {R"({"type":"quest_type","mode":"add","id":"q:questtype/a","receiver":"fixed"})",
                              R"({"type":"quest_type","mode":"add","id":"q:questtype/a","fixed_npc":"base:npc/12"})",
                              R"({"type":"quest_type","mode":"add","id":"q:questtype/a","pool":[{"item":"base:item/173","by_species":[]}]})",
                              R"({"type":"quest_type","mode":"add","id":"q:questtype/a","pool":[{"by_species":[{"species":7,"item":"base:item/173"}]}]})"})
        {
            ContentDB d2 = baseDb();
            CHECK(!load("bad2", b, d2).empty());
        }
    }
    { // errors: each is said, nothing is added
        const char *bad[] = {
            R"({"type":"quest_type","mode":"add","id":"q:questtype/a","pool":[{"item":"nope:item/x"}]})",
            R"({"type":"quest_type","mode":"add","id":"q:questtype/a","count_lo":3,"count_hi":2})",
            R"({"type":"quest_type","mode":"add","id":"q:questtype/a","roll_lo":3})",
            R"({"type":"quest_type","mode":"add","id":"q:questtype/a","receiver":"fixed"})",
            R"({"type":"quest_type","mode":"add","id":"q:questtype/a","request_text":["q:str/missing"]})",
            R"({"type":"quest_type","mode":"add","id":"q:questtype/a","objective_count":[{"from":0,"lo":3,"hi":1}]})",
            R"({"type":"quest_type","mode":"add","id":"q:questtype/a","bogus":1})",
            R"({"type":"quest_type","mode":"add","id":"other:questtype/a"})",
        };
        for (const char *b : bad)
        {
            ContentDB db = baseDb();
            const size_t before = db.questTypes().size();
            const auto errs = load("bad", b, db);
            CHECK(!errs.empty());
            CHECK(db.questTypes().size() == before);
        }
    }
    { // the type's own lines are used by the text builders; types without them use the game's
        QuestTextEnv e;
        e.rnd = [](int lo, int) { return lo; };
        e.npcName = [](int) { return std::string("Pip"); };
        e.areaName = [](int) { return std::string("the Park"); };
        e.itemName = [](const Id &i, int) { return i == "base:item/173" ? std::string("pie") : std::string("cake"); };
        e.str = [](const std::string &k) { return k == "request_default_1" ? std::string("Default {list}") : std::string(); };
        e.typeLines = [](int mode, int kind) {
            return mode == 123 && kind == 0 ? std::vector<std::string>{"Please fetch {list} for {npc} in {area}: {item}."} : std::vector<std::string>{};
        };
        QuestDef d;
        d.typeMode = 123;
        d.objectives = {{2, "base:item/173", 0}, {1, "base:item/174", 0}};
        CHECK(requestText(d, e) == "Please fetch 2 pie, 1 cake for Pip in the Park: pie.");
        d.typeMode = 0;
        CHECK(requestText(d, e).rfind("Default", 0) == 0);
        CHECK(completeText(123, e) == "(complete_default_1)"); // no own complete line: the game's (a missing string shows its key)
        e.typeLines = [](int, int kind) { return kind == 1 ? std::vector<std::string>{"Lovely!"} : std::vector<std::string>{}; };
        CHECK(completeText(5, e) == "Lovely!");
    }
    if (g_fail)
    {
        std::cerr << "quest_type_test: " << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "quest_type_test: all passed\n";
    return 0;
}
