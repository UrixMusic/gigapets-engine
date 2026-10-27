// script_check_test.cpp - hook validation (bad targets, bad hook names) and runtime error reporting.
// Needs only script_check.hpp, script_host.hpp and Lua.
#include "../script_check.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                             \
    do                                                       \
    {                                                        \
        if (!(c))                                            \
        {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while (0)

static bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }
// Bounds-safe element access, so a failed check reports a FAIL line instead of crashing the test.
static std::string at(const std::vector<std::string> &v, size_t i) { return i < v.size() ? v[i] : std::string(); }

static gp::HookTargets targets()
{
    gp::HookTargets t;
    t.npcExists = [](const std::string &id) { return id == "base:npc/66" || id == "m:npc/ok"; };
    t.areaExists = [](const std::string &id) { return id == "base:area/0" || id == "m:area/ok"; };
    return t;
}

int main()
{
    // --- everything fine: no problems, and a table with no functions yet is not an error ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "local N = Npc.hooks('m:npc/ok'); function N:onTalk(ctx) end; function N:onTalkStart(ctx) end\n"
                          "local V = Npc.hooks('base:npc/66'); function V:onTalk(ctx) end\n"
                          "local A = Area.hooks('base:area/0'); function A:onInteract(ctx) end\n"
                          "Npc.hooks('m:npc/ok')\n"));
        CHECK(gp::validateHooks(h, targets()).empty());
    }
    // --- an NPC that does not exist: mod and file:line are named ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("alpha", "-- line 1\nlocal N = Npc.hooks('alpha:npc/typo')\nfunction N:onTalk(ctx) end\n"));
        const auto p = gp::validateHooks(h, targets());
        CHECK(p.size() == 1);
        CHECK(has(at(p, 0), "[alpha]") && has(at(p, 0), "string:2") && has(at(p, 0), "alpha:npc/typo") && has(at(p, 0), "no such NPC"));
    }
    // --- NPC and area ids are checked against their own sets ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m", "Npc.hooks('base:area/0'); Area.hooks('m:npc/ok')"));
        const auto p = gp::validateHooks(h, targets());
        CHECK(p.size() == 2);
        CHECK((has(at(p, 0), "no such NPC") && has(at(p, 1), "no such area")) || (has(at(p, 0), "no such area") && has(at(p, 1), "no such NPC")));
    }
    // --- a wrong hook name: the usual typo gets a hint, and the line is where the function is defined ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "local N = Npc.hooks('m:npc/ok')\n"
                          "N.note = 'not a function, ignored'\n"
                          "function N:onTalkstart(ctx) end\n"
                          "function N:onGreet(ctx) end\n"));
        const auto p = gp::validateHooks(h, targets());
        CHECK(p.size() == 2);
        CHECK(has(at(p, 0), "string:4") && has(at(p, 0), "onGreet") && !has(at(p, 0), "did you mean")); // sorted: onGreet, onTalkstart
        CHECK(has(at(p, 1), "string:3") && has(at(p, 1), "onTalkstart") && has(at(p, 1), "did you mean 'onTalkStart'?"));
        CHECK(has(at(p, 1), "valid: onTalkStart, onTalk"));
    }
    // --- each kind has its own valid names: onTalk is not an area hook, onInteract is not an NPC hook ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "local A = Area.hooks('m:area/ok'); function A:onTalk(ctx) end\n"
                          "local N = Npc.hooks('m:npc/ok'); function N:onInteract(ctx) end\n"));
        const auto p = gp::validateHooks(h, targets());
        CHECK(p.size() == 2);
        CHECK(has(at(p, 0) + at(p, 1), "valid: onInteract") && has(at(p, 0) + at(p, 1), "valid: onTalkStart, onTalk"));
    }
    // --- an unset predicate skips that check; two mods are attributed separately ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("one", "Npc.hooks('x:npc/1')"));
        CHECK(h.runString("two", "Npc.hooks('x:npc/2')"));
        gp::HookTargets none;
        CHECK(gp::validateHooks(h, none).empty());
        const auto p = gp::validateHooks(h, targets());
        CHECK(p.size() == 2 && has(at(p, 0), "[one]") && has(at(p, 1), "[two]"));
    }
    // --- runtime script errors are reported as they happen, not only collected ---
    {
        gp::ScriptHost h;
        std::vector<std::string> seen;
        h.api().onError = [&](const std::string &m) { seen.push_back(m); };
        CHECK(h.runString("m", "local N = Npc.hooks('m:npc/ok'); function N:onTalk(ctx) error('boom') end"));
        CHECK(seen.empty());
        CHECK(!h.fireNpcTalk("m:npc/ok", 0, 0));
        CHECK(seen.size() == 1 && has(at(seen, 0), "[m]") && has(at(seen, 0), "boom") && at(seen, 0) == h.errors().back());
        CHECK(!h.fireNpcTalk("m:npc/ok", 0, 0)); // hook is off now: no second report
        CHECK(seen.size() == 1);
        CHECK(!h.runString("m", "this is not lua")); // load errors are reported too
        CHECK(seen.size() == 2);
    }
    std::printf(fails ? "FAILED: %d check(s)\n" : "script_check_test: all passed\n", fails);
    return fails ? 1 : 0;
}