// script_system_test.cpp - engine systems a mod can wrap or replace (System.hooks): before changes the values or cancels, the first replace takes the job over,
// after sees the result; with no hooks the default runs untouched; unknown systems are an error; mods do not overwrite each other.
#include "../src/mods/script_host.hpp"
#include "../src/mods/script_check.hpp"
#include <cstdio>
#include <tuple>

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

struct Run
{
    int applied = 0;     // what the "engine" did
    bool defaulted = false;
};
static Run decay(gp::ScriptHost &h, double amount, std::string stat = "hunger")
{
    Run r;
    gp::SystemCtx c;
    c.set("stat", stat).set("amount", amount);
    r.defaulted = h.system("care.decay", c, [&] { r.applied = int(c.num("amount")); });
    return r;
}

int main()
{
    { // no hooks: the default, untouched
        gp::ScriptHost h;
        const Run r = decay(h, 3);
        CHECK(r.defaulted && r.applied == 3);
    }
    { // before changes the amount; after sees it; the order across mods is the load order
        gp::ScriptHost h;
        CHECK(h.runString("a", "log_ = ''; local S = System.hooks('care.decay'); function S:before(ctx) ctx.amount = ctx.amount * 2; log_ = log_ .. 'a' end; function S:after(ctx) log_ = log_ .. 'A' .. ctx.amount end"));
        CHECK(h.runString("b", "local S = System.hooks('care.decay'); function S:before(ctx) ctx.amount = ctx.amount + 1; log_ = log_ .. 'b' end"));
        const Run r = decay(h, 3);
        CHECK(r.defaulted && r.applied == 7); // (3 * 2) + 1
        CHECK(h.runString("m", "assert(log_ == 'abA7', log_)"));
        CHECK(h.errors().empty());
    }
    { // return false from before cancels everything: no default, no after
        gp::ScriptHost h;
        CHECK(h.runString("a", "ran_ = 0; local S = System.hooks('care.decay'); function S:before(ctx) if ctx.stat == 'hunger' then return false end end; function S:after(ctx) ran_ = ran_ + 1 end"));
        Run r = decay(h, 3, "hunger");
        CHECK(!r.defaulted && r.applied == 0);
        r = decay(h, 3, "tiredness");
        CHECK(r.defaulted && r.applied == 3);
        CHECK(h.runString("m", "assert(ran_ == 1, ran_)")); // after ran once, for the tiredness one
    }
    { // replace takes over: the default is skipped; only the first mod with a replace runs
        gp::ScriptHost h;
        CHECK(h.runString("a", "n_ = 0; local S = System.hooks('care.decay'); function S:replace(ctx) n_ = n_ + 1 end"));
        CHECK(h.runString("b", "local S = System.hooks('care.decay'); function S:replace(ctx) n_ = n_ + 10 end"));
        const Run r = decay(h, 3);
        CHECK(!r.defaulted && r.applied == 0);
        CHECK(h.runString("m", "assert(n_ == 1, n_)"));
    }
    { // a replace that errors falls back to the default, and is switched off
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:replace(ctx) error('boom') end"));
        Run r = decay(h, 4);
        CHECK(r.defaulted && r.applied == 4);
        CHECK(h.errors().size() == 1);
        r = decay(h, 4);
        CHECK(r.defaulted && r.applied == 4 && h.errors().size() == 1);
    }
    { // ctx values: strings too, and a script cannot turn a number into something else
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:before(ctx) ctx.amount = 'lots'; ctx.stat = 5 end"));
        const Run r = decay(h, 3);
        CHECK(r.defaulted && r.applied == 3); // the wrong types are ignored
    }
    { // a replace can do the work with the normal API
        gp::ScriptHost h;
        int care = 50;
        h.api().adjustCare = [&](const std::string &, int d) { care += d; return d; };
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:replace(ctx) player.adjustCare(ctx.stat, -1) end"));
        const Run r = decay(h, 9);
        CHECK(!r.defaulted && care == 49);
    }
    { // pet.move: reverse the controls, hold the pet, or move it your own way
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('pet.move'); function S:before(ctx) ctx.dx = -ctx.dx; if ctx.dy ~= 0 then return false end end"));
        auto move = [&](int dx, int dy)
        {
            gp::SystemCtx c;
            c.set("x", 100).set("y", 50).set("dx", dx).set("dy", dy).set("steps", 2);
            int nx = -1, ny = -1;
            const bool def = h.system("pet.move", c, [&] { nx = int(c.num("x") + c.num("dx") * c.num("steps")); ny = int(c.num("y")); });
            return std::make_tuple(def, nx, int(c.num("dx")));
        };
        auto [d1, x1, dx1] = move(1, 0);
        CHECK(d1 && x1 == 98 && dx1 == -1); // right became left
        auto [d2, x2, dx2] = move(0, 1);
        CHECK(!d2 && x2 == -1); // cancelled by a vertical move
        CHECK(h.runString("b", "local S = System.hooks('pet.move'); function S:replace(ctx) ctx.x = 7; ctx.y = 9 end"));
        gp::SystemCtx c;
        c.set("x", 100).set("y", 50).set("dx", 1).set("dy", 0).set("steps", 2);
        h.system("pet.move", c, [&] { c.set("x", 0).set("y", 0); });
        CHECK(c.num("x") == 7 && c.num("y") == 9);
    }
    { // hasSystem, and an npc.tick that does not take the NPC over
        gp::ScriptHost h;
        CHECK(!h.hasSystem("npc.tick"));
        CHECK(h.runString("a", "seen_ = {}; local S = System.hooks('npc.tick'); function S:before(ctx) seen_[#seen_ + 1] = ctx.id .. ':' .. ctx.x .. ',' .. ctx.y .. ':' .. ctx.stationary end"));
        CHECK(h.hasSystem("npc.tick"));
        gp::SystemCtx c;
        c.set("id", "base:npc/3").set("x", 40).set("y", 60).set("activity", 23).set("stationary", 0).set("scripted", 0);
        bool ran = false;
        h.system("npc.tick", c, [&] { ran = true; });
        CHECK(ran && c.str("id") == "base:npc/3" && c.num("x") == 40);
        CHECK(h.runString("m", "assert(seen_[1] == 'base:npc/3:40,60:0', seen_[1])"));
    }
    { // the game-wide tick: onTickStart / onTickEnd, with the state and the keys; a mod's two tables both run
        gp::ScriptHost h;
        CHECK(h.runString("a", "log_ = ''; local G = Game.hooks(); function G:onTickStart(ctx) log_ = log_ .. 'S' .. ctx.tick .. (ctx.state.dialogue and 'd' or '-') .. ctx.area.id .. ctx.pet.x end; function G:onTickEnd(ctx) log_ = log_ .. 'E' .. (ctx.held.z and 'z' or '-') .. (ctx.input.left and 'l' or '-') end"));
        gp::ScriptHost::TickInfo t;
        t.tick = 7;
        t.area = "base:area/0";
        t.petX = 12;
        t.dialogue = true;
        t.keys.heldZ = true;
        t.keys.left = true;
        h.fireGameTick(false, t);
        h.fireGameTick(true, t);
        CHECK(h.runString("m", "assert(log_ == 'S7dbase:area/012Ezl', log_)"));
        CHECK(h.errors().empty());
        // validation knows the two names
        gp::HookTargets targets;
        CHECK(gp::validateHooks(h, targets).empty());
    }
    { // the encounter systems exist and carry the npc id and codes
        gp::ScriptHost h;
        CHECK(h.runString("a", "local C = System.hooks('encounter.classify'); function C:before(ctx) ctx.player = ctx.animal + 1; ctx.animal = 5; seen_ = ctx.npc .. ctx.pet .. ctx.target end"));
        CHECK(h.runString("b", "System.hooks('encounter.start'); System.hooks('encounter.finish'); System.hooks('friend.visit')"));
        gp::SystemCtx c;
        c.set("npc", "base:npc/9").set("pet", 3).set("target", 4).set("player", 1).set("animal", 2);
        h.system("encounter.classify", c, [] {});
        CHECK(c.num("player") == 3 && c.num("animal") == 5);
        CHECK(h.runString("m", "assert(seen_ == 'base:npc/934', seen_)"));
        gp::SystemCtx f;
        f.set("npc", "x").set("outcome", "friend").set("chance", 40).set("friend", 0);
        bool rolled = false;
        h.system("encounter.finish", f, [&] { rolled = true; f.set("friend", 1); });
        CHECK(rolled && f.num("friend") == 1);
    }
    { // what the default leaves in ctx is what after() sees, and what the engine keeps
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('encounter.finish'); function S:after(ctx) seen_ = ctx.friend end"));
        gp::SystemCtx f;
        f.set("npc", "x").set("outcome", "friend").set("chance", 40).set("friend", 0);
        h.system("encounter.finish", f, [&] { f.set("friend", 1); });
        CHECK(f.num("friend") == 1);
        CHECK(h.runString("m", "assert(seen_ == 1, tostring(seen_))"));
    }
    { // unknown system names are an error at registration; System.list shows the real ones
        gp::ScriptHost h;
        CHECK(!h.runString("a", "System.hooks('care.decya')"));
        CHECK(h.errors().size() == 1 && h.errors()[0].find("no such system") != std::string::npos);
        CHECK(h.runString("b", "local l = System.list(); assert(#l >= 1 and l[1].name == 'care.decay' and l[1].what and l[1].fields)"));
    }
    { // validation: a misspelt function is reported like any other hook
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:befor(ctx) end"));
        gp::HookTargets t;
        const auto problems = gp::validateHooks(h, t);
        CHECK(problems.size() == 1 && problems[0].find("befor") != std::string::npos && problems[0].find("before") != std::string::npos);
    }
    std::printf(fails ? "script_system_test: %d failure(s)\n" : "script_system_test: all passed\n", fails);
    return fails ? 1 : 0;
}
