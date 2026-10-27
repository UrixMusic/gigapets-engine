// script_player_test.cpp - the player.* script API (give, take, count, hasFlag, setFlag) and the "gift once" pattern.
// Needs only script_host.hpp and Lua. The game side (item caps, the real Player) is covered by the walk bindings;
// here the callbacks are a small in-memory fake, so every rule the HOST enforces is tested on its own.
//
// Build (adjust -I to wherever "lua/lua.h" lives):  g++ -std=c++17 -Iyour/include tests/script_player_test.cpp -llua -o script_player_test
#include "../src/mods/script_host.hpp"
#include <cstdio>
#include <map>
#include <set>

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

struct Fake
{
    std::map<std::string, int> inv;
    std::set<std::string> flags;
    int maxStack = 3;
    void wire(gp::ScriptApi &a)
    {
        a.give = [this](const std::string &id, int n)
        {
            const int add = std::min(n, std::max(0, maxStack - inv[id]));
            inv[id] += add;
            return add;
        };
        a.take = [this](const std::string &id, int n)
        {
            const int t = std::min(n, inv[id]);
            inv[id] -= t;
            return t;
        };
        a.count = [this](const std::string &id)
        { return inv[id]; };
        a.hasFlag = [this](const std::string &f)
        { return flags.count(f) > 0; };
        a.setFlag = [this](const std::string &f)
        { flags.insert(f); };
        a.rnd = [](int lo, int) { return lo; };
    }
};

int main()
{
    // --- basics ---
    {
        gp::ScriptHost h;
        Fake f;
        f.wire(h.api());
        CHECK(h.runString("m", "assert(player.give('m:item/a', 2) == 2)"));
        CHECK(h.runString("m", "assert(player.give('m:item/a', 5) == 1)")); // the fake caps the stack at 3
        CHECK(h.runString("m", "assert(player.count('m:item/a') == 3)"));
        CHECK(h.runString("m", "assert(player.take('m:item/a', 10) == 3)")); // never more than held
        CHECK(h.runString("m", "assert(player.count('m:item/a') == 0)"));
        CHECK(h.runString("m", "assert(player.give('m:item/a') == 1)")); // n defaults to 1
        CHECK(h.errors().empty());
    }
    // --- flags: own prefix to set, anything to read ---
    {
        gp::ScriptHost h;
        Fake f;
        f.wire(h.api());
        f.flags.insert("base:offered/9");
        CHECK(h.runString("m", "assert(not player.hasFlag('m:x')); player.setFlag('m:x'); assert(player.hasFlag('m:x'))"));
        CHECK(h.runString("m", "assert(player.hasFlag('base:offered/9'))")); // reading is not restricted
        CHECK(h.errors().empty());
        CHECK(!h.runString("m", "player.setFlag('other:x')")); // someone else's prefix
        CHECK(!h.runString("m", "player.setFlag('base:offered/3')"));
        CHECK(!h.runString("m", "player.setFlag('x')")); // no prefix
        CHECK(!h.runString("m", "player.setFlag('m:')")); // prefix only
        CHECK(!h.runString("m", "player.setFlag('m:a,b')")); // save-list separators and whitespace
        CHECK(!h.runString("m", "player.setFlag('m:a=b')"));
        CHECK(!h.runString("m", "player.setFlag('m:a|b')"));
        CHECK(!h.runString("m", "player.setFlag('m:a b')"));
        CHECK(f.flags.size() == 2); // only the two good ones are there
        CHECK(h.errors().size() == 8);
        CHECK(h.errors().back().find("[m]") == 0); // errors name the mod
        // a lookalike prefix must not pass: mod "mod" may not set "modx:..."
        CHECK(!h.runString("mod", "player.setFlag('modx:a')"));
    }
    // --- argument checks ---
    {
        gp::ScriptHost h;
        Fake f;
        f.wire(h.api());
        CHECK(!h.runString("m", "player.give('m:item/a', 0)"));
        CHECK(!h.runString("m", "player.give('m:item/a', -1)"));
        CHECK(!h.runString("m", "player.take('m:item/a', 10000)"));
        CHECK(!h.runString("m", "player.give('m:item/a', 1.5)"));
        CHECK(!h.runString("m", "player.give()"));
        CHECK(h.runString("m", "assert(player.give('a,b', 1) == 0)")); // bad characters never reach the game
        CHECK(h.runString("m", "assert(not player.hasFlag('a b'))"));
        CHECK(f.inv.empty());
    }
    // --- an unwired API does nothing and does not crash ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m", "assert(player.give('x', 1) == 0 and player.take('x') == 0 and player.count('x') == 0)"));
        CHECK(h.runString("m", "assert(player.hasFlag('m:x') == false); player.setFlag('m:x')"));
    }
    // --- the gift-once pattern, end to end through a hook ---
    {
        gp::ScriptHost h;
        Fake f;
        f.maxStack = 99;
        f.wire(h.api());
        CHECK(h.runString("testmod",
                          "local gifts = { 'base:item/5', 'base:item/13' }\n"
                          "local Tester = Npc.hooks('testmod:npc/tester')\n"
                          "function Tester:onTalk(ctx)\n"
                          "  if player.hasFlag('testmod:gift/tester') then return end\n"
                          "  player.setFlag('testmod:gift/tester')\n"
                          "  player.give(rand.pick(gifts), 1)\n"
                          "end\n"));
        CHECK(h.fireNpcTalk("testmod:npc/tester", 10, 20));
        CHECK(h.fireNpcTalk("testmod:npc/tester", 10, 20));
        CHECK(h.fireNpcTalk("testmod:npc/tester", 10, 20));
        CHECK(f.inv["base:item/5"] == 1); // the fake rnd always picks the first one
        CHECK(f.flags.count("testmod:gift/tester") == 1);
        CHECK(h.errors().empty());
    }
    // --- inside a hook the prefix is the mod that REGISTERED the hook; a bad flag disables the hook ---
    {
        gp::ScriptHost h;
        Fake f;
        f.wire(h.api());
        CHECK(h.runString("alpha",
                          "local T = Npc.hooks('alpha:npc/a')\n"
                          "function T:onTalk(ctx) player.setFlag('alpha:seen') end\n"));
        CHECK(h.runString("beta",
                          "local T = Npc.hooks('beta:npc/b')\n"
                          "function T:onTalk(ctx) player.setFlag('alpha:stolen') end\n")); // beta using alpha's prefix
        CHECK(h.fireNpcTalk("alpha:npc/a", 0, 0));
        CHECK(f.flags.count("alpha:seen") == 1);
        CHECK(!h.fireNpcTalk("beta:npc/b", 0, 0)); // errors: not beta's prefix
        CHECK(f.flags.count("alpha:stolen") == 0);
        CHECK(!h.fireNpcTalk("beta:npc/b", 0, 0)); // and the hook is now off for the session
        CHECK(h.errors().size() == 1 && h.errors()[0].find("[beta]") == 0);
    }
    // --- pet stats: care / personality read and adjust, by name, reporting the change actually made ---
    {
        gp::ScriptHost h;
        std::map<std::string, int> care{{"hunger", 40}, {"health", 100}, {"happiness", 0}, {"tiredness", 70}, {"cleanliness", 10}};
        std::map<std::string, int> pers{{"halos", 9990}, {"pitchforks", 5}, {"stars", 0}, {"lightbulbs", 0}};
        h.api().getCare = [&](const std::string &s) { return care.at(s); };
        h.api().adjustCare = [&](const std::string &s, int d) // clamped to 0..100 like Player::adjustCare
        {
            const int before = care.at(s);
            care[s] = std::max(0, std::min(100, before + d));
            return care[s] - before;
        };
        h.api().getPersonality = [&](const std::string &s) { return pers.at(s); };
        h.api().adjustPersonality = [&](const std::string &s, int d)
        {
            const int before = pers.at(s);
            pers[s] = std::max(0, std::min(9999, before + d));
            return pers[s] - before;
        };
        CHECK(h.runString("m",
                          "assert(player.care('hunger') == 40 and player.personality('halos') == 9990)\n"
                          "assert(player.adjustCare('hunger', 25) == 25 and player.care('hunger') == 65)\n"
                          // health is already full (no change); cleanliness clamps at 0; halos clamps at 9999
                          "assert(player.adjustCare('health', 10) == 0)\n"
                          "assert(player.adjustCare('tiredness', 100 - player.care('tiredness')) == 30)\n"
                          "assert(player.adjustCare('cleanliness', -50) == -10)\n"
                          "assert(player.adjustPersonality('halos', 50) == 9)\n"
                          "assert(player.adjustPersonality('stars', 100) == 100 and player.personality('stars') == 100)\n"));
        CHECK(care["hunger"] == 65 && care["tiredness"] == 100 && care["cleanliness"] == 0);
        CHECK(pers["halos"] == 9999 && pers["stars"] == 100);
        CHECK(h.errors().empty());
    }
    // --- unknown stat names, bad amounts and missing arguments are loud script errors ---
    {
        const char *bad[] = {"player.care('hungr')",              "player.adjustCare('halos', 1)", // a personality stat is not a care stat
                             "player.personality('hunger')",     "player.adjustPersonality('stars')",
                             "player.adjustCare('hunger', 1.5)", "player.adjustCare('hunger', 10000)",
                             "player.adjustPersonality('stars', -10000)", "player.care()"};
        for (const char *code : bad)
        {
            gp::ScriptHost h;
            CHECK(!h.runString("m", code));
            CHECK(h.errors().size() == 1);
        }
        gp::ScriptHost h;
        CHECK(!h.runString("m", "player.care('hungr')"));
        CHECK(h.errors()[0].find("unknown stat \"hungr\"") != std::string::npos &&
              h.errors()[0].find("valid: hunger, health, happiness, tiredness, cleanliness") != std::string::npos);
        CHECK(!h.runString("m", "player.adjustPersonality('luck', 1)"));
        CHECK(h.errors()[1].find("valid: halos, pitchforks, stars, lightbulbs") != std::string::npos);
    }
    // --- money: clamped like Player::adjustMoney, reports the change made ---
    {
        gp::ScriptHost h;
        int money = 99990;
        h.api().getMoney = [&] { return money; };
        h.api().adjustMoney = [&](int d)
        {
            const int before = money;
            money = std::max(0, std::min(99999, money + d));
            return money - before;
        };
        CHECK(h.runString("m",
                          "assert(player.money() == 99990)\n"
                          "assert(player.adjustMoney(25) == 9 and player.money() == 99999)\n" // capped
                          "assert(player.adjustMoney(-99999) == -99999 and player.money() == 0)\n"));
        CHECK(!h.runString("m", "player.adjustMoney(100000)")); // out of range is a script error
        CHECK(!h.runString("m", "player.adjustMoney(2.5)"));
        CHECK(!h.runString("m", "player.adjustMoney()"));
        CHECK(h.errors().size() == 3);
    }
    // --- illness: named ids, nil = healthy, setIllness / cure ---
    {
        gp::ScriptHost h;
        int ill = -1; // the ROM number; -1 = healthy
        h.api().getIllness = [&] { return ill; };
        h.api().setIllness = [&](int i) { ill = i; };
        CHECK(h.runString("m", "assert(player.illness() == nil and player.cure() == false)")); // healthy: nothing to cure
        CHECK(h.runString("m", "player.setIllness('panda_pox')"));
        CHECK(ill == 2); // the position in illnessNames() is the ROM number
        CHECK(h.runString("m", "assert(player.illness() == 'panda_pox')"));
        CHECK(h.runString("m", "assert(player.cure() == true and player.illness() == nil)"));
        CHECK(ill == -1);
        for (size_t i = 0; i < gp::ScriptHost::illnessNames().size(); ++i) // every name round-trips
        {
            CHECK(h.runString("m", "player.setIllness('" + gp::ScriptHost::illnessNames()[i] + "')"));
            CHECK(ill == int(i));
        }
        ill = 10; // an out-of-range number from the game reads as healthy
        CHECK(h.runString("m", "assert(player.illness() == nil)"));
        CHECK(h.errors().empty());
        CHECK(!h.runString("m", "player.setIllness('measles')"));
        CHECK(h.errors().size() == 1 && h.errors()[0].find("unknown illness \"measles\"") != std::string::npos &&
              h.errors()[0].find("valid: dog_breath, gone_bananas, panda_pox, cold") != std::string::npos);
        CHECK(!h.runString("m", "player.setIllness(nil)")); // nil is not a name: use player.cure()
        CHECK(!h.runString("m", "player.setIllness()"));
        CHECK(ill == 10);
    }
    // --- a game that has not wired the stats: reads and changes are 0, nothing breaks ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "assert(player.care('hunger') == 0 and player.personality('stars') == 0)\n"
                          "assert(player.adjustCare('hunger', 5) == 0 and player.adjustPersonality('stars', 5) == 0)\n"
                          "assert(player.money() == 0 and player.adjustMoney(5) == 0)\n"
                          "assert(player.illness() == nil and player.cure() == false)\n"
                          "player.setIllness('cold')\n"));
        CHECK(h.errors().empty());
    }
    std::printf(fails ? "FAILED: %d check(s)\n" : "script_player_test: all passed\n", fails);
    return fails ? 1 : 0;
}
