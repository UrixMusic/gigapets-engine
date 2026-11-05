// side_quest_test.cpp - the `side_quest` record (fields, errors) and the state of a side quest on the player: offer rules, accept, hand-in, reward, repeat.
#include "../src/mods/mod_loader.hpp"
#include "../src/game/side_quest.hpp"
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
        root = fs::temp_directory_path() / ("gp_sidequest_" + n);
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
    for (const char *t : {"base:text/1"})
        db.put(TextDef{t, "Hello"});
    for (const char *i : {"base:item/173", "base:item/174"})
    {
        ItemDef it;
        it.id = i;
        it.maxQty = 5;
        db.put(std::move(it));
    }
    for (const char *n : {"base:npc/12", "base:npc/13"})
    {
        NpcDef p;
        p.id = n;
        p.anim = "base:anim/139";
        db.put(std::move(p));
    }
    return db;
}

static const char *kTexts = R"({"type":"text","mode":"add","id":"q:str/offer","text":"Bring me gems?"},
  {"type":"text","mode":"add","id":"q:str/done","text":"Thanks!"},
  {"type":"text","mode":"add","id":"q:str/ok","text":"Great."},)";

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
        const auto errs = load("good", R"({"type":"side_quest","mode":"add","id":"q:sidequest/gems","giver":"base:npc/12","receiver":"base:npc/13",
            "objectives":[{"item":"base:item/173","count":3},{"item":"base:item/174"}],"offer_text":"q:str/offer","complete_text":"q:str/done","accept_text":"q:str/ok",
            "reward_money":50,"reward_items":[{"item":"base:item/174","count":2}],"quest_min":2,"quest_max":9,"requires":"q:met","repeatable":true})", db);
        for (auto &e : errs)
            std::cerr << e << std::endl;
        CHECK(errs.empty());
        const SideQuestDef *q = db.sideQuest("q:sidequest/gems");
        CHECK(q && q->giver == "base:npc/12" && q->receiver == "base:npc/13" && q->objectives.size() == 2 && q->objectives[0].count == 3 && q->objectives[1].count == 1);
        CHECK(q && q->rewardMoney == 50 && q->rewardItems.size() == 1 && q->questMin == 2 && q->questMax == 9 && q->requires_ == "q:met" && q->repeatable && q->acceptText == "q:str/ok");

        // the player's side of it
        Player p;
        p.questsCompleted = 1;
        CHECK(!sideOfferable(p, *q)); // before quest_min
        p.questsCompleted = 3;
        CHECK(!sideOfferable(p, *q)); // the flag it requires is not set
        p.setFlag("q:met");
        CHECK(sideOfferable(p, *q) && sideState(p, *q) == SideState::Idle);
        sideStart(p, *q);
        CHECK(sideState(p, *q) == SideState::Active && !sideOfferable(p, *q));
        CHECK(p.objectives.size() == 2 && p.objective("sq:q:sidequest/gems#0")->item == "base:item/173" && p.objective("sq:q:sidequest/gems#0")->need == 3);
        CHECK(!sideHasAll(p, *q));
        p.addItem("base:item/173", 3);
        CHECK(!sideHasAll(p, *q)); // the second item is missing
        p.addItem("base:item/174", 1);
        CHECK(sideHasAll(p, *q));
        sideFinish(p, *q);
        CHECK(sideState(p, *q) == SideState::Done && p.objectives.empty() && p.itemCount("base:item/173") == 0 && p.itemCount("base:item/174") == 0);
        sideReward(p, *q, [](const Id &) { return 5; });
        CHECK(p.money == 50 && p.itemCount("base:item/174") == 2);
        CHECK(sideOfferable(p, *q)); // repeatable: offered again
        sideStart(p, *q);
        CHECK(sideState(p, *q) == SideState::Active); // and the done flag is gone while it runs
        p.questsCompleted = 9;
        CHECK(sideState(p, *q) == SideState::Active && !sideOfferable(p, *q));
        sideFinish(p, *q);
        CHECK(!sideOfferable(p, *q)); // past quest_max
        // a full bag takes only what fits
        Player f;
        f.addItem("base:item/174", 4);
        sideReward(f, *q, [](const Id &) { return 5; });
        CHECK(f.itemCount("base:item/174") == 5);
    }
    { // defaults: the receiver is the giver, no objectives, a one-shot
        ContentDB db = baseDb();
        const auto errs = load("min", R"({"type":"side_quest","mode":"add","id":"q:sidequest/hi","giver":"base:npc/12","offer_text":"q:str/offer","complete_text":"q:str/done"})", db);
        CHECK(errs.empty());
        const SideQuestDef *q = db.sideQuest("q:sidequest/hi");
        CHECK(q && q->receiver == "base:npc/12" && q->objectives.empty() && !q->repeatable);
        Player p;
        sideStart(p, *q);
        CHECK(sideHasAll(p, *q)); // nothing to bring
        sideFinish(p, *q);
        CHECK(!sideOfferable(p, *q)); // a one-shot is done for good
    }
    auto bad = [&](const char *name, const std::string &rec, const char *expect)
    {
        ContentDB db = baseDb();
        const auto errs = load(name, rec, db);
        bool found = false;
        for (const auto &e : errs)
            found = found || e.find(expect) != std::string::npos;
        if (!found)
        {
            std::cerr << "FAIL " << name << ": expected '" << expect << "', got:\n";
            for (const auto &e : errs)
                std::cerr << "  " << e << "\n";
            ++g_fail;
        }
    };
    const std::string ok = R"("giver":"base:npc/12","offer_text":"q:str/offer","complete_text":"q:str/done")";
    bad("nogiver", R"({"type":"side_quest","mode":"add","id":"q:sidequest/x","offer_text":"q:str/offer","complete_text":"q:str/done"})", "'giver' is required");
    bad("badnpc", R"({"type":"side_quest","mode":"add","id":"q:sidequest/x","giver":"base:npc/99","offer_text":"q:str/offer","complete_text":"q:str/done"})", "npc that exists");
    bad("badreceiver", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"receiver\":\"nope\"," + ok + "}", "'receiver' must be the id of an npc");
    bad("notext", R"({"type":"side_quest","mode":"add","id":"q:sidequest/x","giver":"base:npc/12","complete_text":"q:str/done"})", "'offer_text' is required");
    bad("badtext", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"accept_text\":\"q:str/none\"," + ok + "}", "'accept_text' must be the id of a text");
    bad("baditem", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"item\":\"base:item/999\"}]," + ok + "}", "needs an 'item' that exists");
    bad("dupitem", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"item\":\"base:item/173\"},{\"item\":\"base:item/173\"}]," + ok + "}", "listed twice");
    bad("badcount", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"item\":\"base:item/173\",\"count\":0}]," + ok + "}", "'count' must be 1 to 99");
    bad("manyitems", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"item\":\"base:item/173\"},{\"item\":\"base:item/174\"},{\"item\":\"base:item/173\"},{\"item\":\"base:item/173\"},{\"item\":\"base:item/173\"}]," + ok + "}", "up to 4");
    bad("window", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"quest_min\":5,\"quest_max\":5," + ok + "}", "'quest_max' must be greater");
    bad("money", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"reward_money\":100000," + ok + "}", "'reward_money' must be 0 to 99999");
    bad("flag", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"requires\":\"a b\"," + ok + "}", "cannot contain spaces");
    bad("unknown", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"colour\":1," + ok + "}", "unknown field 'colour'");
    bad("prefix", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"other:sidequest/x\"," + ok + "}", "own prefix");
    std::cout << (g_fail ? "side_quest_test: FAILED\n" : "side_quest_test: all passed\n");
    return g_fail ? 1 : 0;
}
