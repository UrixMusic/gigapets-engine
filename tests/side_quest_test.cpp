// side_quest_test.cpp - the `side_quest` record (fields, errors) and the state of a side quest on the player: offer rules, accept, hand-in, reward, repeat.
#include "../src/mods/mod_loader.hpp"
#include "../src/game/quests/side_quest.hpp"
#include "../src/game/data/save_player.hpp"
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
        CHECK(q && q->giver == "base:npc/12" && q->receiver == "base:npc/13" && q->objectives.size() == 2 && q->objectives[0].count == 3 && q->objectives[1].count == 1 && q->turnIn == "npc");
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
        CHECK(!sideHasAll(p, *q));
        p.addItem("base:item/173", 3);
        CHECK(!sideHasAll(p, *q)); // the second item is missing
        p.addItem("base:item/174", 1);
        CHECK(sideHasAll(p, *q));
        sideFinish(p, *q);
        CHECK(sideState(p, *q) == SideState::Done && p.itemCount("base:item/173") == 0 && p.itemCount("base:item/174") == 0);
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
    { // flag and counter objectives, auto / script turn-in, no reward: a boss, a puzzle
        ContentDB db = baseDb();
        const auto errs = load("kinds", R"({"type":"side_quest","mode":"add","id":"q:sidequest/boss","giver":"base:npc/12","turn_in":"auto",
            "objectives":[{"flag":"q:boss_dead","text":"q:str/ok"},{"counter":"q:slimes","need":3,"text":"q:str/ok"},{"item":"base:item/173"}],"offer_text":"q:str/offer"},
          {"type":"side_quest","mode":"add","id":"q:sidequest/puzzle","giver":"base:npc/12","turn_in":"script","offer_text":"q:str/offer"})", db);
        for (auto &e : errs)
            std::cerr << e << std::endl;
        CHECK(errs.empty());
        const SideQuestDef *q = db.sideQuest("q:sidequest/boss"), *z = db.sideQuest("q:sidequest/puzzle");
        CHECK(q && q->turnIn == "auto" && q->objectives.size() == 3 && q->objectives[0].kind == SideObjective::Flag && q->objectives[1].kind == SideObjective::Counter && q->objectives[1].count == 3);
        CHECK(z && z->turnIn == "script" && z->objectives.empty() && z->completeText.empty() && z->rewardMoney == 0);
        Player p;
        p.setFlag("q:boss_dead"); // set before the quest was taken: starting it asks for the boss afresh
        p.counters["q:slimes"] = 9;
        sideStart(p, *q);
        CHECK(!p.hasFlag("q:boss_dead") && counterOf(p, "q:slimes") == 0 && !sideHasAll(p, *q));
        p.setFlag("q:boss_dead");
        p.counters["q:slimes"] = 2;
        p.addItem("base:item/173");
        CHECK(!sideHasAll(p, *q) && sideProgress(p, q->objectives[1]) == std::make_pair(2, 3) && sideProgress(p, q->objectives[0]) == std::make_pair(1, 1));
        p.counters["q:slimes"] = 40; // past the target still counts as done, shown as the target
        CHECK(sideHasAll(p, *q) && sideProgress(p, q->objectives[1]) == std::make_pair(3, 3));
        const auto name = [](const Id &) { return std::string("Gem"); };
        CHECK(sideLine(p, q->objectives[2], name, name) == "1/1  Gem" && sideLine(p, q->objectives[1], name, name) == "3/3  Gem");
        sideFinish(p, *q);
        CHECK(sideState(p, *q) == SideState::Done && p.itemCount("base:item/173") == 0);
        // abandon
        sideStart(p, *z);
        CHECK(sideState(p, *z) == SideState::Active && sideHasAll(p, *z)); // nothing asked: only a script ends it
        sideAbandon(p, *z);
        CHECK(sideState(p, *z) == SideState::Idle && sideOfferable(p, *z));
        // counters are saved with the pet
        SaveData sd;
        p.counters["q:slimes"] = 7;
        savePlayer(p, sd);
        Player r;
        loadPlayer(sd, r);
        CHECK(counterOf(r, "q:slimes") == 7);
    }
    { // time limit
        ContentDB db = baseDb();
        const auto errs = load("timed", R"({"type":"side_quest","mode":"add","id":"q:sidequest/t","giver":"base:npc/12","turn_in":"script","time_limit":60,"fail_text":"q:str/ok","offer_text":"q:str/offer"})", db);
        CHECK(errs.empty());
        const SideQuestDef *q = db.sideQuest("q:sidequest/t");
        CHECK(q && q->timeLimit == 60 && q->failText == "q:str/ok");
        Player p;
        CHECK(sideTimeLeft(p, *q, 30.0) == 60.0);
        sideStart(p, *q);
        p.counters[sideTimeKey(q->id)] = 30 * 45; // 45 s of steps at 30 Hz
        CHECK(sideTimeLeft(p, *q, 30.0) == 15.0);
        p.counters[sideTimeKey(q->id)] = 30 * 70;
        CHECK(sideTimeLeft(p, *q, 30.0) == 0.0);
        sideAbandon(p, *q);
        CHECK(counterOf(p, sideTimeKey(q->id)) == 0 && sideOfferable(p, *q)); // offered again, a fresh clock
        sideStart(p, *q);
        CHECK(sideTimeLeft(p, *q, 30.0) == 60.0);
        ContentDB db2 = baseDb();
        CHECK(sideTimeLeft(p, SideQuestDef(), 30.0) == -1); // no limit
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
    bad("badturn", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"turn_in\":\"magic\"," + ok + "}", "'turn_in' must be");
    bad("kinds2", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"item\":\"base:item/173\",\"flag\":\"a\"}]," + ok + "}", "exactly one of");
    bad("flagnotext", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"flag\":\"q:a\"}]," + ok + "}", "needs a 'text'");
    bad("flagspace", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"flag\":\"q a\",\"text\":\"q:str/ok\"}]," + ok + "}", "without spaces");
    bad("ctneed", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"counter\":\"q:a\",\"need\":0,\"text\":\"q:str/ok\"}]," + ok + "}", "'need' must be 1 to 9999");
    bad("ctcount", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"objectives\":[{\"counter\":\"q:a\",\"count\":2,\"text\":\"q:str/ok\"}]," + ok + "}", "use 'need'");
    bad("autoneedscomplete", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"giver\":\"base:npc/12\",\"offer_text\":\"q:str/offer\"}", "'complete_text' is required");
    bad("timelow", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"time_limit\":1," + ok + "}", "'time_limit' must be 5 to 86400");
    bad("failtext", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"fail_text\":\"q:str/none\"," + ok + "}", "'fail_text' must be the id of a text");
    bad("window", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"quest_min\":5,\"quest_max\":5," + ok + "}", "'quest_max' must be greater");
    bad("money", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"reward_money\":100000," + ok + "}", "'reward_money' must be 0 to 99999");
    bad("flag", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"requires\":\"a b\"," + ok + "}", "cannot contain spaces");
    bad("unknown", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"q:sidequest/x\",\"colour\":1," + ok + "}", "unknown field 'colour'");
    bad("prefix", "{\"type\":\"side_quest\",\"mode\":\"add\",\"id\":\"other:sidequest/x\"," + ok + "}", "own prefix");
    std::cout << (g_fail ? "side_quest_test: FAILED\n" : "side_quest_test: all passed\n");
    return g_fail ? 1 : 0;
}
