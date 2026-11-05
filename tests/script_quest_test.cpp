// script_quest_test.cpp - the Lua quest.* and counter.* functions (side quests a script starts, finishes, gives up; counters scripts keep).
#include "../src/mods/script_host.hpp"
#include <cstdio>
#include <map>

static int fails = 0;
#define CHECK(c)                                                       \
    do                                                                 \
    {                                                                  \
        if (!(c))                                                      \
        {                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);           \
            ++fails;                                                   \
        }                                                              \
    } while (0)

int main()
{
    gp::ScriptHost h;
    std::map<std::string, std::string> st{{"m:sidequest/a", "idle"}, {"m:sidequest/b", "active"}};
    std::map<std::string, int> counters;
    std::vector<std::string> asked;
    gp::ScriptApi &a = h.api();
    a.sideState = [&](const std::string &id) { return st.count(id) ? st[id] : std::string(); };
    a.sideStart = [&](const std::string &id) { if (!st.count(id) || st[id] == "active") return false; st[id] = "active"; return true; };
    a.sideComplete = [&](const std::string &id) { if (!st.count(id) || st[id] != "active") return false; asked.push_back(id); return true; };
    a.sideAbandon = [&](const std::string &id) { if (!st.count(id) || st[id] != "active") return false; st[id] = "idle"; return true; };
    a.sideActive = [&]
    {
        std::vector<std::string> v;
        for (auto &[k, s] : st)
            if (s == "active")
                v.push_back(k);
        return v;
    };
    a.sideInfo = [&](const std::string &id)
    {
        gp::SideQuestInfo r;
        if (id != "m:sidequest/b")
            return r;
        r.found = true;
        r.id = id;
        r.state = "active";
        r.giver = "base:npc/12";
        r.turnIn = "auto";
        r.rewardMoney = 40;
        r.timeLimit = 60;
        r.timeLeft = 12.5;
        gp::SideQuestInfo::Objective o;
        o.kind = "counter";
        o.counter = "m:slimes";
        o.text = "m:str/slimes";
        o.have = 2;
        o.need = 5;
        r.objectives.push_back(o);
        gp::SideQuestInfo::Objective f;
        f.kind = "flag";
        f.flag = "m:boss";
        f.done = true;
        f.have = 1;
        r.objectives.push_back(f);
        return r;
    };
    a.counterGet = [&](const std::string &n) { return counters.count(n) ? counters[n] : 0; };
    a.counterSet = [&](const std::string &n, int v) { counters[n] = v; };
    CHECK(h.runString("m", R"(
        assert(quest.state('m:sidequest/a') == 'idle' and quest.state('m:sidequest/b') == 'active')
        assert(quest.state('m:sidequest/none') == nil and quest.exists('m:sidequest/a') and not quest.exists('m:sidequest/none'))
        assert(quest.start('m:sidequest/a') == true and quest.start('m:sidequest/a') == false)  -- already running
        assert(quest.start('m:sidequest/none') == false)
        local act = quest.active()
        assert(#act == 2 and act[1] == 'm:sidequest/a' and act[2] == 'm:sidequest/b')
        assert(quest.complete('m:sidequest/a') == true)
        assert(quest.abandon('m:sidequest/b') == true and quest.abandon('m:sidequest/b') == false)
        assert(quest.complete('m:sidequest/b') == false)  -- not running any more
        assert(counter.get('m:slimes') == 0)
        assert(counter.add('m:slimes') == 1 and counter.add('m:slimes', 4) == 5 and counter.get('m:slimes') == 5)
        assert(counter.set('m:slimes', 2) == 2 and counter.add('m:slimes', -9) == 0)  -- never below 0
        assert(counter.get('other:x') == 0)  -- anyone's counter can be read
    )"));
    CHECK(h.runString("m", R"(
        assert(quest.info('m:sidequest/none') == nil)
        local q = quest.info('m:sidequest/b')
        assert(q.id == 'm:sidequest/b' and q.state == 'active' and q.giver == 'base:npc/12' and q.receiver == nil and q.turn_in == 'auto' and q.reward_money == 40)
        assert(q.time_limit == 60 and q.time_left == 12.5 and #q.objectives == 2)
        local o = q.objectives[1]
        assert(o.kind == 'counter' and o.counter == 'm:slimes' and o.text == 'm:str/slimes' and o.have == 2 and o.need == 5 and o.done == false and o.item == nil)
        assert(q.objectives[2].kind == 'flag' and q.objectives[2].flag == 'm:boss' and q.objectives[2].done == true and q.objectives[2].text == nil)
    )"));
    CHECK(asked.size() == 1 && asked[0] == "m:sidequest/a");
    CHECK(counters["m:slimes"] == 0);
    CHECK(!h.runString("m", "counter.set('other:x', 1)")); // only the mod's own prefix
    CHECK(!h.runString("m", "counter.add('m:a b')"));
    CHECK(h.errors().size() == 2);
    std::printf(fails ? "script_quest_test: %d failure(s)\n" : "script_quest_test: all passed\n", fails);
    return fails ? 1 : 0;
}
