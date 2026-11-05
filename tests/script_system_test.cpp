// script_system_test.cpp - engine systems a mod can wrap or replace (System.hooks): before changes the values or cancels, the first replace takes the job over,
// after sees the result; with no hooks the default runs untouched; unknown systems are an error; mods do not overwrite each other.
#include "../src/mods/script_host.hpp"
#include "../src/mods/script_check.hpp"
#include "../src/engine/activity_system.hpp"
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
    { // the room systems exist; room.leave's x, y come back changed
        gp::ScriptHost h;
        CHECK(h.runString("a", "local L = System.hooks('room.leave'); function L:before(ctx) ctx.x = ctx.x + 5 end; System.hooks('room.enter'); System.hooks('room.interact')"));
        gp::SystemCtx c;
        c.set("location", 0).set("x", 100).set("y", 50);
        h.system("room.leave", c, [] {});
        CHECK(c.num("x") == 105 && c.num("y") == 50);
        CHECK(h.errors().empty());
    }
    { // camera and minigame systems exist; a camera hook's numbers (fractions too) and a prize list come back changed
        gp::ScriptHost h;
        CHECK(h.runString("a", "local C = System.hooks('camera.view'); function C:after(ctx) ctx.camX = ctx.camX + 0.5; ctx.camY = ctx.y end;"
                               "local P = System.hooks('minigame.prize'); function P:before(ctx) ctx.items = ctx.items .. ',base:item/5' end; System.hooks('minigame.start')"));
        gp::SystemCtx c;
        c.set("x", 100).set("y", 60).set("camX", 10).set("camY", 20);
        h.system("camera.view", c, [] {});
        CHECK(c.num("camX") == 10.5 && c.num("camY") == 60);
        gp::SystemCtx p;
        p.set("game", "swift_pick").set("items", "base:item/1");
        h.system("minigame.prize", p, [] {});
        CHECK(p.str("items") == "base:item/1,base:item/5");
        CHECK(h.errors().empty());
    }
    { // player.think, and the panel / thought systems
        gp::ScriptHost h;
        std::string got;
        h.api().think = [&](const std::string &k) { got = k; return k != "nope"; };
        CHECK(h.runString("a", "assert(player.think('hungry') == true); assert(player.think('nope') == false)"));
        CHECK(got == "nope");
        CHECK(h.runString("b", "System.hooks('panel.open'); local T = System.hooks('thought.show'); function T:before(ctx) if ctx.kind == 'mood' then ctx.kind = 'tired' end end"));
        gp::SystemCtx c;
        c.set("kind", "mood");
        h.system("thought.show", c, [] {});
        CHECK(c.str("kind") == "tired" && h.errors().empty());
    }
    { // tutorial.once: shown the first time only, remembered under the mod's prefix, and only while tutorials are on
        gp::ScriptHost h;
        std::set<std::string> flags;
        int said = 0;
        bool on = true;
        h.api().hasFlag = [&](const std::string &f) { return flags.count(f) > 0; };
        h.api().setFlag = [&](const std::string &f) { flags.insert(f); };
        h.api().say = [&](const std::string &, const gp::ScriptApi::SayVars &) { ++said; return true; };
        h.api().tutorialsOn = [&] { return on; };
        CHECK(h.runString("m", "assert(tutorial.once('first', 'm:str/x') == true); assert(tutorial.once('first', 'm:str/x') == false)"));
        CHECK(said == 1 && flags.count("m:tutorial_first") == 1);
        on = false;
        CHECK(h.runString("m", "assert(tutorial.once('second', 'm:str/x') == false)")); // tutorials off: nothing, and not used up
        CHECK(said == 1 && flags.count("m:tutorial_second") == 0);
        on = true;
        CHECK(h.runString("m", "assert(tutorial.once('second', 'm:str/x') == true)"));
        CHECK(!h.runString("m", "tutorial.once('bad name', 'm:str/x')")); // a name is letters, digits, _ and -
    }
    { // npc.sequence: a mod may change how many times, or stop it
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('npc.sequence'); function S:before(ctx) if ctx.kind == 'yawn' then return false end; if ctx.kind == 'trick' then ctx.repeats = ctx.repeats + 2 end end"));
        gp::SystemCtx t;
        t.set("npc", "x").set("kind", "trick").set("level", 2).set("repeats", 1);
        int ran = 0;
        h.system("npc.sequence", t, [&] { ran = int(t.num("repeats")); });
        CHECK(ran == 3);
        gp::SystemCtx y;
        y.set("npc", "x").set("kind", "yawn").set("count", 1);
        CHECK(!h.system("npc.sequence", y, [] {}));
    }
    { // say with a portrait, and text.get
        gp::ScriptHost h;
        std::vector<std::string> portraits;
        h.api().say = [&](const std::string &, const gp::ScriptApi::SayVars &) { portraits.push_back(h.takeSayPortrait()); return true; };
        h.api().textGet = [&](const std::string &id, const gp::ScriptApi::SayVars &v, std::string &out) {
            if (id == "m:str/none") return false;
            out = id + ":" + (v.empty() ? "" : v[0].second);
            return true;
        };
        CHECK(h.runString("m", "say('m:str/a'); say('m:str/b', {}, { portrait = 'm:npc/baker' }); say('m:str/c', nil, {})"));
        CHECK(portraits.size() == 3 && portraits[0].empty() && portraits[1] == "m:npc/baker" && portraits[2].empty());
        CHECK(h.takeSayPortrait().empty()); // never left behind for the next say
        CHECK(!h.runString("m", "say('m:str/a', {}, { portrait = 5 })")); // a bad portrait is a script error
        CHECK(!h.runString("m", "say('m:str/a', {}, 'baker')"));
        CHECK(h.runString("m", "assert(text.get('m:str/x', { name = 'Pip' }) == 'm:str/x:Pip'); assert(text.get('m:str/none') == nil)"));
    }
    { // the front end systems exist; frontend.start can change the slot
        gp::ScriptHost h;
        CHECK(h.runString("a", "System.hooks('frontend.menu'); local S = System.hooks('frontend.start'); function S:before(ctx) ctx.slot = 2 end"));
        gp::SystemCtx c;
        c.set("slot", 0);
        int got = -1;
        h.system("frontend.start", c, [&] { got = int(c.num("slot")); });
        CHECK(got == 2 && h.errors().empty());
    }
    { // a trusted mod's scripts run without the sandbox (io, os, require ...); a sandboxed one's do not, and the two do not see each other's globals
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "gp_trusted_test";
        std::error_code rmEc;
        fs::remove_all(root, rmEc);
        for (const char *id : {"safe", "wild"})
        {
            fs::create_directories(root / id / "scripts");
            std::ofstream(root / id / "scripts" / "main.lua") << R"(
                have_os = os ~= nil; have_io = io ~= nil; have_load = load ~= nil; have_require = require ~= nil
                mine = "mine"
                if require then
                    lib = require("util.helper")
                    again = require("util.helper") == lib
                    local f = io.open(")" << (root / id / "out.txt").generic_string() << R"(", "w"); f:write("hi"); f:close()
                end
            )";
        }
        fs::create_directories(root / "wild" / "scripts" / "util");
        std::ofstream(root / "wild" / "scripts" / "util" / "helper.lua") << "return { answer = 42 }";
        gp::ScriptHost h;
        h.loadMod("safe", root / "safe", false);
        h.loadMod("wild", root / "wild", true);
        CHECK(h.errors().empty());
        CHECK(h.runString("probe", "assert(os == nil and io == nil and load == nil and require == nil and debug == nil)")); // the sandbox is unchanged for everyone else
        CHECK(!fs::exists(root / "safe" / "out.txt") && fs::exists(root / "wild" / "out.txt")); // only the trusted one wrote its file
        std::string got;
        {
            std::ifstream f(root / "wild" / "out.txt");
            f >> got;
        }
        CHECK(got == "hi");
        // what the trusted one defined stays in its own environment (the sandboxed one wrote to the shared globals, as before)
        CHECK(h.runString("probe", "assert(lib == nil and again == nil and have_os == false)"));
        fs::remove_all(root, rmEc);
    }
    { // pet.activity: a running activity's step goes through the system; step 0 never does
        gp::ScriptHost h;
        int step = 0, ran = 0;
        gp::runActivity(h, "feed", step, [&] { ++ran; });
        CHECK(ran == 1); // idle: just the tick
        step = 2;
        CHECK(h.runString("a", "log_ = ''; local A = System.hooks('pet.activity'); function A:before(ctx) log_ = log_ .. ctx.kind .. ctx.step; if ctx.kind == 'wash' then ctx.step = 5 end end; function A:after(ctx) if ctx.kind == 'dig' then ctx.step = 9 end end"));
        gp::runActivity(h, "feed", step, [&] { ++ran; step = 3; });
        CHECK(ran == 2 && step == 3); // untouched by the mod: the game's own move stands
        step = 1;
        int seenInTick = -1;
        gp::runActivity(h, "wash", step, [&] { seenInTick = step; step = 6; });
        CHECK(seenInTick == 5 && step == 6); // before changed it: the tick starts from there, and moves on
        step = 1;
        gp::runActivity(h, "dig", step, [&] { step = 2; });
        CHECK(step == 9); // after set it
        CHECK(h.runString("b", "local A = System.hooks('pet.activity'); function A:replace(ctx) if ctx.kind == 'bark' then ctx.step = 0 end end"));
        step = 3;
        int ticks = 0;
        gp::runActivity(h, "bark", step, [&] { ++ticks; });
        CHECK(ticks == 0 && step == 0); // replaced: the game's step is skipped, and the mod ended it
        CHECK(h.errors().empty());
    }
    { // player.tricks / setTricks: the mask of known tricks, in any order
        gp::ScriptHost h;
        int mask = 1;
        h.api().getTricks = [&] { return mask; };
        h.api().setTricks = [&](int m) { mask = m; };
        CHECK(h.runString("a", "assert(player.tricks() == 1); player.setTricks(5); assert(player.tricks() == 5)"));
        CHECK(mask == 5);
        CHECK(!h.runString("b", "player.setTricks(9)")); // out of range is a script error
    }
    { // the trick systems exist, and a mod can change what the engine reads back
        gp::ScriptHost h;
        CHECK(h.runString("a", "local P = System.hooks('trick.perform'); function P:before(ctx) if ctx.known == 0 then ctx.known = 1 end end; System.hooks('trick.teach'); System.hooks('trick.learn')"));
        gp::SystemCtx c;
        c.set("slot", 2).set("level", 3).set("known", 0);
        int level = 0;
        h.system("trick.perform", c, [&] { level = int(c.num("known")) * int(c.num("level")); });
        CHECK(level == 3);
        CHECK(h.errors().empty());
    }
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
    { // quest.reward: a mod can switch the reward activity
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('quest.reward'); function S:before(ctx) if ctx.code == -2 then ctx.code = -7 end; seen_ = ctx.quest .. ':' .. ctx.generic end"));
        gp::SystemCtx c;
        c.set("code", -2).set("quest", "base:quest/4").set("number", 4).set("generic", 0).set("type", "").set("giver", "").set("receiver", "");
        const bool run = h.system("quest.reward", c, [] {});
        CHECK(run && c.num("code") == -7);
        CHECK(h.runString("m", "assert(seen_ == 'base:quest/4:0', seen_)"));
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
