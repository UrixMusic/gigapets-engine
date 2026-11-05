// script_mods_test.cpp - two mods hooking the same id: each has a table of its own, both run in the order the mods loaded, a hook that returns false takes over
// (the mods after it are not asked), one mod's error switches off only its own hook, and a waiting hook of one mod does not hold another back.
#include "../src/mods/script_host.hpp"
#include <cstdio>

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
    { // both run, in load order; neither overwrites the other
        gp::ScriptHost h;
        CHECK(h.runString("alpha", "log_ = ''; local T = Npc.hooks('x:npc/a'); function T:onTalk(ctx) log_ = log_ .. 'A' end"));
        CHECK(h.runString("beta", "local T = Npc.hooks('x:npc/a'); function T:onTalk(ctx) log_ = log_ .. 'B' end"));
        CHECK(h.runString("gamma", "local T = Npc.hooks('x:npc/a'); function T:onTalk(ctx) log_ = log_ .. 'C' end"));
        CHECK(h.fireNpcTalk("x:npc/a", 1, 2));
        CHECK(h.runString("m", "assert(log_ == 'ABC', log_)"));
        CHECK(h.runString("beta", "local T = Npc.hooks('x:npc/a'); function T:onTalkStart(ctx) log_ = log_ .. 'b' end")); // the same mod again: its own table
        CHECK(h.fireNpcTalkStart("x:npc/a", 1, 2).ran);
        CHECK(h.runString("m", "assert(log_ == 'ABCb')"));
        CHECK(h.errors().empty());
        // the validation list has one entry for each mod's table
        int n = 0;
        for (const auto &i : h.hooks())
            if (i.id == "x:npc/a")
                ++n;
        CHECK(n == 3);
    }
    { // a hook that returns false takes over: the mods after it are not asked; any other result lets the next one run
        gp::ScriptHost h;
        CHECK(h.runString("alpha", "ran = ''; local T = Npc.hooks('x:npc/a'); function T:onTalkStart(ctx) ran = ran .. 'A'; return nil end"));
        CHECK(h.runString("beta", "local T = Npc.hooks('x:npc/a'); function T:onTalkStart(ctx) ran = ran .. 'B'; return false end"));
        CHECK(h.runString("gamma", "local T = Npc.hooks('x:npc/a'); function T:onTalkStart(ctx) ran = ran .. 'C' end"));
        const auto r = h.fireNpcTalkStart("x:npc/a", 0, 0);
        CHECK(r.ran && r.suppress);
        CHECK(h.runString("m", "assert(ran == 'AB', ran)")); // gamma was not asked
    }
    { // one mod's error switches off only its own hook
        gp::ScriptHost h;
        CHECK(h.runString("alpha", "n = 0; local T = Npc.hooks('x:npc/a'); function T:onTalk(ctx) error('boom') end"));
        CHECK(h.runString("beta", "local T = Npc.hooks('x:npc/a'); function T:onTalk(ctx) n = n + 1 end"));
        CHECK(h.fireNpcTalk("x:npc/a", 0, 0));
        CHECK(h.fireNpcTalk("x:npc/a", 0, 0)); // alpha is off now, beta goes on
        CHECK(h.runString("m", "assert(n == 2, n)"));
        CHECK(h.errors().size() == 1 && h.errors()[0].find("[alpha]") == 0);
    }
    { // the global Game hooks and the area ones work the same way
        gp::ScriptHost h;
        CHECK(h.runString("alpha", "g = ''; local G = Game.hooks(); function G:onLoad(ctx) g = g .. 'a' end; local A = Area.hooks('x:area/1'); function A:onTick(ctx) g = g .. '1' end"));
        CHECK(h.runString("beta", "local G = Game.hooks(); function G:onLoad(ctx) g = g .. 'b' end; local A = Area.hooks('x:area/1'); function A:onTick(ctx) g = g .. '2' end"));
        CHECK(h.fireGameLoad());
        CHECK(h.fireAreaTick("x:area/1", 1));
        CHECK(h.runString("m", "assert(g == 'ab12', g)"));
    }
    { // a tick hook that waits (one mod) does not hold the other mod's tick back
        gp::ScriptHost h;
        CHECK(h.runString("alpha", "t = ''; local A = Area.hooks('x:area/1'); function A:onTick(ctx) t = t .. 'a'; wait(30) end"));
        CHECK(h.runString("beta", "local A = Area.hooks('x:area/1'); function A:onTick(ctx) t = t .. 'b' end"));
        h.fireAreaTick("x:area/1", 1);
        h.fireAreaTick("x:area/1", 2); // alpha is still waiting: not started again; beta runs
        h.fireAreaTick("x:area/1", 3);
        CHECK(h.runString("m", "assert(t == 'abbb', t)"));
    }
    { // a scene's id belongs to one mod: they share its table, as before
        gp::ScriptHost h;
        CHECK(h.runString("alpha", "s = ''; local S = Scene.hooks('x:scene/s'); function S:onOpen(ctx) s = s .. 'o' end"));
        CHECK(h.runString("beta", "local S = Scene.hooks('x:scene/s'); function S:onTick(ctx) s = s .. 't' end"));
        CHECK(h.sceneKnown("x:scene/s"));
        CHECK(h.runString("m", "local S = Scene.hooks('x:scene/s'); assert(type(S.onOpen) == 'function' and type(S.onTick) == 'function')")); // one shared table
        int n = 0;
        for (const auto &i : h.hooks())
            if (i.id == "x:scene/s")
                ++n;
        CHECK(n == 1);
    }
    std::printf(fails ? "script_mods_test: %d failure(s)\n" : "script_mods_test: all passed\n", fails);
    return fails ? 1 : 0;
}
