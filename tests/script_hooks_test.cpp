// script_hooks_test.cpp - NPC and area hooks: ctx.player, onTalkStart / onInteract (return false = take over),
// per-hook disabling.
// Needs only script_host.hpp and Lua.
#include "../mods/script_host.hpp"
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

static void wire(gp::ScriptHost &h, int quests = 4)
{
    h.api().playerInfo = [quests]
    {
        gp::ScriptApi::PlayerInfo p;
        p.name = "Rex";
        p.money = 250;
        p.questsCompleted = quests;
        return p;
    };
}

int main()
{
    // --- ctx.player and ctx.npc reach both hooks ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "seen = {}\n"
                          "local T = Npc.hooks('m:npc/a')\n"
                          "function T:onTalkStart(ctx)\n"
                          "  seen.start = ctx.player.name .. ':' .. ctx.player.money .. ':' .. ctx.player.questsCompleted\n"
                          "  seen.npc = ctx.npc.id .. '@' .. ctx.npc.x .. ',' .. ctx.npc.y\n"
                          "end\n"
                          "function T:onTalk(ctx) seen.talk = ctx.player.name end\n"));
        const auto r = h.fireNpcTalkStart("m:npc/a", 3, 4);
        CHECK(r.ran && !r.suppress); // no return value: the game carries on
        CHECK(h.fireNpcTalk("m:npc/a", 3, 4));
        CHECK(h.runString("m", "assert(seen.start == 'Rex:250:4', seen.start); assert(seen.npc == 'm:npc/a@3,4'); assert(seen.talk == 'Rex')"));
        CHECK(h.errors().empty());
    }
    // --- ctx.player is a snapshot: changing it changes nothing, and it is empty if the game gave no info ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/a'); function T:onTalk(ctx) ctx.player.money = 999 end"));
        CHECK(h.fireNpcTalk("m:npc/a", 0, 0));
        CHECK(h.api().playerInfo().money == 250);
        gp::ScriptHost bare; // playerInfo not set
        CHECK(bare.runString("m", "local T = Npc.hooks('m:npc/a'); function T:onTalk(ctx) assert(type(ctx.player) == 'table' and ctx.player.name == nil) end"));
        CHECK(bare.fireNpcTalk("m:npc/a", 0, 0) && bare.errors().empty());
    }
    // --- only an exact `false` suppresses ---
    {
        const char *bodies[] = {"return false", "return nil", "return true", "return 0", "return ''", "return"};
        const bool want[] = {true, false, false, false, false, false};
        for (int i = 0; i < 6; ++i)
        {
            gp::ScriptHost h;
            CHECK(h.runString("m", std::string("local T = Npc.hooks('m:npc/a'); function T:onTalkStart(ctx) ") + bodies[i] + " end"));
            const auto r = h.fireNpcTalkStart("m:npc/a", 0, 0);
            CHECK(r.ran);
            CHECK(r.suppress == want[i]);
        }
    }
    // --- no hook, no function, unknown NPC: nothing runs, nothing is suppressed ---
    {
        gp::ScriptHost h;
        auto r = h.fireNpcTalkStart("m:npc/none", 0, 0);
        CHECK(!r.ran && !r.suppress);
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/a'); function T:onTalk(ctx) end")); // only onTalk defined
        r = h.fireNpcTalkStart("m:npc/a", 0, 0);
        CHECK(!r.ran && !r.suppress);
    }
    // --- a script error never suppresses (the player still gets the normal dialogue), and disables that hook only ---
    {
        gp::ScriptHost h;
        int talks = 0;
        CHECK(h.runString("m",
                          "local T = Npc.hooks('m:npc/a')\n"
                          "function T:onTalkStart(ctx) error('boom') end\n"
                          "function T:onTalk(ctx) world.drop('x', 0, 0) end\n"));
        h.api().drop = [&talks](const std::string &, int, int) { ++talks; return true; };
        auto r = h.fireNpcTalkStart("m:npc/a", 0, 0);
        CHECK(!r.ran && !r.suppress);
        CHECK(h.errors().size() == 1 && h.errors()[0].find("[m]") == 0 && h.errors()[0].find("onTalkStart") != std::string::npos);
        r = h.fireNpcTalkStart("m:npc/a", 0, 0); // off for the session: no second error
        CHECK(!r.ran && h.errors().size() == 1);
        CHECK(h.fireNpcTalk("m:npc/a", 0, 0) && talks == 1); // onTalk is a separate hook and still works
    }
    // --- the other way round: onTalk failing leaves onTalkStart alone ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "local T = Npc.hooks('m:npc/a')\n"
                          "function T:onTalkStart(ctx) return false end\n"
                          "function T:onTalk(ctx) error('boom') end\n"));
        CHECK(!h.fireNpcTalk("m:npc/a", 0, 0));
        const auto r = h.fireNpcTalkStart("m:npc/a", 0, 0);
        CHECK(r.ran && r.suppress);
    }
    // --- a take-over script can do the whole talk itself: give once, then stay silent ---
    {
        gp::ScriptHost h;
        std::set<std::string> flags;
        int given = 0;
        h.api().hasFlag = [&](const std::string &f) { return flags.count(f) > 0; };
        h.api().setFlag = [&](const std::string &f) { flags.insert(f); };
        h.api().give = [&](const std::string &, int n) { given += n; return n; };
        CHECK(h.runString("m",
                          "local T = Npc.hooks('m:npc/a')\n"
                          "function T:onTalkStart(ctx)\n"
                          "  if player.hasFlag('m:done') then return end\n"
                          "  player.setFlag('m:done'); player.give('m:item/x', 1)\n"
                          "  return false\n"
                          "end\n"));
        CHECK(h.fireNpcTalkStart("m:npc/a", 0, 0).suppress);  // first talk: scripted, default skipped
        CHECK(!h.fireNpcTalkStart("m:npc/a", 0, 0).suppress); // afterwards: the normal dialogue
        CHECK(given == 1);
    }
    // --- an over-budget onTalkStart is stopped, does not suppress ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/a'); function T:onTalkStart(ctx) while true do end end"));
        const auto r = h.fireNpcTalkStart("m:npc/a", 0, 0);
        CHECK(!r.ran && !r.suppress && h.errors().size() == 1);
    }
    // --- Area.hooks(...).onInteract: ctx.area, ctx.object, ctx.player; return false takes over ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "seen = {}\n"
                          "local A = Area.hooks('base:area/3')\n"
                          "function A:onInteract(ctx)\n"
                          "  seen[#seen + 1] = ctx.area.id .. '/' .. ctx.object.id .. '/' .. ctx.player.name\n"
                          "  if ctx.object.id == 'chest' then return false end\n"
                          "end\n"));
        auto r = h.fireAreaInteract("base:area/3", "chest");
        CHECK(r.ran && r.suppress);
        r = h.fireAreaInteract("base:area/3", "sign");
        CHECK(r.ran && !r.suppress);
        CHECK(h.runString("m", "assert(#seen == 2 and seen[1] == 'base:area/3/chest/Rex' and seen[2] == 'base:area/3/sign/Rex')"));
        r = h.fireAreaInteract("base:area/4", "chest"); // another area: no hook
        CHECK(!r.ran && !r.suppress);
        CHECK(h.errors().empty());
    }
    // --- NPC and area hooks are separate namespaces, even for the same id string, and fail separately ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "local N = Npc.hooks('m:x'); function N:onTalkStart(ctx) return false end\n"
                          "local A = Area.hooks('m:x'); function A:onInteract(ctx) error('boom') end\n"));
        CHECK(h.fireNpcTalkStart("m:x", 0, 0).suppress);
        CHECK(!h.fireAreaInteract("m:x", "o").ran && h.errors().size() == 1);
        CHECK(h.fireNpcTalkStart("m:x", 0, 0).suppress); // the NPC hook is unaffected by the area hook's error
        CHECK(!h.fireAreaInteract("m:x", "o").ran && h.errors().size() == 1); // area hook now off
    }
    // --- an area hook that takes over can hand out an item itself ---
    {
        gp::ScriptHost h;
        int given = 0;
        h.api().give = [&](const std::string &, int n) { given += n; return n; };
        CHECK(h.runString("m",
                          "local A = Area.hooks('m:area/a')\n"
                          "function A:onInteract(ctx) if ctx.object.id == 'pile' then player.give('m:item/x', 2) return false end end\n"));
        CHECK(h.fireAreaInteract("m:area/a", "pile").suppress && given == 2);
        CHECK(!h.fireAreaInteract("m:area/a", "tree").suppress && given == 2);
    }
    std::printf(fails ? "FAILED: %d check(s)\n" : "script_hooks_test: all passed\n", fails);
    return fails ? 1 : 0;
}