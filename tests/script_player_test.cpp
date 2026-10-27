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
    std::printf(fails ? "FAILED: %d check(s)\n" : "script_player_test: all passed\n", fails);
    return fails ? 1 : 0;
}
