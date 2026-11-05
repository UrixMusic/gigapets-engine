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
    // --- Item.hooks(...).onItemUse: ctx.item, ctx.player; return false takes the use over ---
    {
        gp::ScriptHost h;
        wire(h);
        int taken = 0, given = 0;
        h.api().take = [&](const std::string &, int n) { taken += n; return n; };
        h.api().give = [&](const std::string &, int n) { given += n; return n; };
        CHECK(h.runString("m",
                          "seen = {}\n"
                          "local I = Item.hooks('m:item/box')\n"
                          "function I:onItemUse(ctx)\n"
                          "  seen[#seen + 1] = ctx.item.id .. '/' .. ctx.player.name\n"
                          "  player.take(ctx.item.id, 1); player.give('base:item/5', 1)\n"
                          "  return false\n"
                          "end\n"
                          "local P = Item.hooks('m:item/passive')\n"
                          "function P:onItemUse(ctx) end\n"));
        auto r = h.fireItemUse("m:item/box");
        CHECK(r.ran && r.suppress && taken == 1 && given == 1);
        r = h.fireItemUse("m:item/passive"); // runs, returns nothing: the game's own use goes ahead
        CHECK(r.ran && !r.suppress);
        r = h.fireItemUse("m:item/other"); // no hook for this item
        CHECK(!r.ran && !r.suppress);
        CHECK(h.runString("m", "assert(#seen == 1 and seen[1] == 'm:item/box/Rex')"));
        CHECK(h.errors().empty());
    }
    // --- item, NPC and area hooks are separate namespaces; an item hook that errors is off for the session and never
    //     takes over ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "local N = Npc.hooks('m:x'); function N:onTalkStart(ctx) return false end\n"
                          "local I = Item.hooks('m:x'); function I:onItemUse(ctx) error('boom') end\n"));
        CHECK(!h.fireItemUse("m:x").ran && h.errors().size() == 1);
        CHECK(!h.fireItemUse("m:x").ran && h.errors().size() == 1); // off now: no second report
        CHECK(h.fireNpcTalkStart("m:x", 0, 0).suppress);            // the NPC hook is unaffected
    }
    // --- an item script that loops forever is stopped, and does not take over ---
    {
        gp::ScriptHost h;
        CHECK(h.runString("m", "local I = Item.hooks('m:x'); function I:onItemUse(ctx) while true do end end"));
        const auto r = h.fireItemUse("m:x");
        CHECK(!r.ran && !r.suppress && h.errors().size() == 1);
    }
    // --- Game.hooks().onQuestComplete: ctx.quest / giver / receiver / player; return false takes the rewards over ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "seen = {}\n"
                          "local G = Game.hooks()\n"
                          "function G:onQuestComplete(ctx)\n"
                          "  seen[#seen + 1] = tostring(ctx.quest.id) .. '|' .. ctx.quest.number .. '|' .. tostring(ctx.quest.generic) .. '|' ..\n"
                          "    tostring(ctx.quest.type) .. '|' .. tostring(ctx.giver.id) .. '|' .. tostring(ctx.receiver.id) .. '|' .. ctx.player.name\n"
                          "  if ctx.quest.number == 3 then return false end\n"
                          "end\n"
                          "assert(Game.hooks() == Game.hooks())\n")); // one shared table
        gp::QuestCompleteInfo a;
        a.questId = "base:quest/2";
        a.number = 2;
        a.type = "base:questtype/0";
        a.giverId = "base:npc/1";
        a.receiverId = "base:npc/2";
        auto r = h.fireQuestComplete(a);
        CHECK(r.ran && !r.suppress);
        gp::QuestCompleteInfo b; // a generated quest, NPCs the db doesn't know: the ids are nil, not empty strings
        b.number = 3;
        b.generic = true;
        r = h.fireQuestComplete(b);
        CHECK(r.ran && r.suppress);
        CHECK(h.runString("m",
                          "assert(seen[1] == 'base:quest/2|2|false|base:questtype/0|base:npc/1|base:npc/2|Rex', seen[1])\n"
                          "assert(seen[2] == 'nil|3|true|nil|nil|nil|Rex', seen[2])"));
        CHECK(h.errors().empty());
    }
    // --- no hook registered: nothing runs; an error disables the hook and never takes the rewards over ---
    {
        gp::ScriptHost h;
        auto r = h.fireQuestComplete({});
        CHECK(!r.ran && !r.suppress);
        CHECK(h.runString("m", "local G = Game.hooks(); function G:onQuestComplete(ctx) error('boom') end"));
        r = h.fireQuestComplete({});
        CHECK(!r.ran && !r.suppress && h.errors().size() == 1);
        r = h.fireQuestComplete({});
        CHECK(!r.ran && h.errors().size() == 1); // off for the session: no second error
    }
    // --- a quest hook can hand out its own reward and speak, like the other take-over hooks ---
    {
        gp::ScriptHost h;
        int given = 0;
        std::vector<std::string> said;
        h.api().give = [&](const std::string &, int n) { given += n; return n; };
        h.api().say = [&](const std::string &id, const gp::ScriptApi::SayVars &) { said.push_back(id); return true; };
        CHECK(h.runString("m",
                          "local G = Game.hooks()\n"
                          "function G:onQuestComplete(ctx)\n"
                          "  if ctx.quest.id ~= 'm:quest/pie' then return end\n"
                          "  player.give('m:item/pie', 3); say('m:str/thanks'); return false\n"
                          "end\n"));
        gp::QuestCompleteInfo other;
        other.questId = "base:quest/9";
        CHECK(!h.fireQuestComplete(other).suppress && given == 0 && said.empty());
        gp::QuestCompleteInfo mine;
        mine.questId = "m:quest/pie";
        CHECK(h.fireQuestComplete(mine).suppress && given == 3 && said.size() == 1);
    }
    // --- ask() and wait(): a hook runs as a coroutine ---
    {
        gp::ScriptHost h;
        wire(h);
        std::vector<std::pair<std::string, std::function<void(bool)>>> asked;
        std::vector<std::string> flags;
        h.api().ask = [&](const std::string &id, const gp::ScriptApi::SayVars &, std::function<void(bool)> a)
        {
            if (id == "m:str/none")
                return false;
            asked.emplace_back(id, std::move(a));
            return true;
        };
        h.api().setFlag = [&](const std::string &f) { flags.push_back(f); };
        CHECK(h.runString("m",
                          "local T = Npc.hooks('m:npc/a')\n"
                          "function T:onTalkStart(ctx)\n"
                          "  if ask('m:str/q') then player.setFlag('m:yes') else player.setFlag('m:no') end\n"
                          "  player.setFlag('m:after')\n"
                          "end\n"));
        const auto r = h.fireNpcTalkStart("m:npc/a", 0, 0);
        CHECK(r.ran && r.suppress); // it waits: it has taken over
        CHECK(asked.size() == 1 && asked[0].first == "m:str/q" && h.waiting() == 1 && flags.empty());
        asked[0].second(true);
        CHECK(flags.size() == 2 && flags[0] == "m:yes" && flags[1] == "m:after" && h.waiting() == 0);
        asked[0].second(false); // a stale answer does nothing
        CHECK(flags.size() == 2 && h.errors().empty());
        // No
        flags.clear();
        asked.clear();
        h.fireNpcTalkStart("m:npc/a", 0, 0);
        asked[0].second(false);
        CHECK(flags.size() == 2 && flags[0] == "m:no");
    }
    {
        gp::ScriptHost h;
        wire(h);
        std::vector<std::string> flags;
        h.api().setFlag = [&](const std::string &f) { flags.push_back(f); };
        CHECK(h.runString("m",
                          "local T = Npc.hooks('m:npc/a')\n"
                          "function T:onTalk(ctx)\n"
                          "  player.setFlag('m:one'); wait(3); player.setFlag('m:two'); wait(1); player.setFlag('m:three')\n"
                          "end\n"));
        CHECK(h.fireNpcTalk("m:npc/a", 0, 0));
        CHECK(flags.size() == 1 && h.waiting() == 1);
        h.tick();
        h.tick();
        CHECK(flags.size() == 1);
        h.tick();
        CHECK(flags.size() == 2 && h.waiting() == 1); // the second wait began
        h.tick();
        CHECK(flags.size() == 3 && h.waiting() == 0 && h.errors().empty());
    }
    {
        // an unknown text answers false at once and does not wait; wait / ask outside a hook are errors
        gp::ScriptHost h;
        wire(h);
        h.api().ask = [](const std::string &id, const gp::ScriptApi::SayVars &, std::function<void(bool)>) { return id != "m:str/none"; };
        std::vector<std::string> flags;
        h.api().setFlag = [&](const std::string &f) { flags.push_back(f); };
        CHECK(h.runString("m",
                          "local T = Npc.hooks('m:npc/a')\n"
                          "function T:onTalkStart(ctx)\n"
                          "  if ask('m:str/none') == false then player.setFlag('m:false') end\n"
                          "end\n"));
        const auto r = h.fireNpcTalkStart("m:npc/a", 0, 0);
        CHECK(r.ran && !r.suppress && flags.size() == 1 && h.waiting() == 0);
        CHECK(!h.runString("m", "wait(5)") && !h.errors().empty());
        CHECK(!h.runString("m", "ask('m:str/q')"));
    }
    {
        // wait(0) and wait(5000) are script errors; the hook is then off
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/a'); function T:onTalk(ctx) wait(0) end"));
        CHECK(h.fireNpcTalk("m:npc/a", 0, 0) == false && h.errors().size() == 1 && h.waiting() == 0);
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/b'); function T:onTalk(ctx) wait(5000) end"));
        CHECK(h.fireNpcTalk("m:npc/b", 0, 0) == false && h.errors().size() == 2);
    }
    {
        // an error after a wait switches the hook off and frees the slot; at most 8 scripts can be waiting at once
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/a'); function T:onTalk(ctx) wait(1); error('boom') end"));
        CHECK(h.fireNpcTalk("m:npc/a", 0, 0) && h.waiting() == 1);
        h.tick();
        CHECK(h.waiting() == 0 && h.errors().size() == 1 && !h.fireNpcTalk("m:npc/a", 0, 0));
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/w'); function T:onTalk(ctx) wait(100) end"));
        int started = 0;
        for (int i = 0; i < 10; ++i)
            started += h.fireNpcTalk("m:npc/w", 0, 0);
        CHECK(started == 8 && h.waiting() == 8);
    }
    {
        // an endless loop after a wait still hits the instruction limit
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m", "local T = Npc.hooks('m:npc/a'); function T:onTalk(ctx) wait(1); while true do end end"));
        CHECK(h.fireNpcTalk("m:npc/a", 0, 0));
        h.tick();
        CHECK(h.waiting() == 0 && h.errors().size() == 1);
    }
    // --- onEnter / onLeave / onTick / onEnterArea ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "log_ = {}\n"
                          "local A = Area.hooks('m:area/cave')\n"
                          "function A:onEnter(ctx) log_[#log_ + 1] = 'enter:' .. ctx.area.id .. ':' .. tostring(ctx.from.id) .. ':' .. ctx.player.name end\n"
                          "function A:onLeave(ctx) log_[#log_ + 1] = 'leave:' .. ctx.area.id .. ':' .. ctx.to.id end\n"
                          "local G = Game.hooks()\n"
                          "function G:onEnterArea(ctx) log_[#log_ + 1] = 'any:' .. ctx.area.id end\n"));
        CHECK(h.fireAreaEnter("m:area/cave", "base:area/0"));
        CHECK(h.fireAreaEnter("m:area/cave", "")); // the game just started here: from.id is nil
        CHECK(h.fireAreaLeave("m:area/cave", "base:area/0"));
        CHECK(!h.fireAreaLeave("base:area/0", "m:area/cave")); // no hook on that area
        CHECK(h.fireAreaEnter("base:area/0", "m:area/cave")); // ...but the game-wide onEnterArea still runs
        CHECK(h.runString("m",
                          "assert(log_[1] == 'enter:m:area/cave:base:area/0:Rex', log_[1])\n"
                          "assert(log_[2] == 'any:m:area/cave', log_[2])\n"
                          "assert(log_[3] == 'enter:m:area/cave:nil:Rex', log_[3])\n"
                          "assert(log_[5] == 'leave:m:area/cave:base:area/0', log_[5])\n"
                          "assert(log_[6] == 'any:base:area/0', log_[6])\n"
                          "assert(#log_ == 6, #log_)"));
        CHECK(h.errors().empty());
    }
    {
        // onTick: once per call with dt and tick; only for the area it was registered on
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "n, last, dt = 0, 0, 0\n"
                          "local A = Area.hooks('m:area/cave')\n"
                          "function A:onTick(ctx) n = n + 1; last = ctx.tick; dt = ctx.dt; assert(ctx.area.id == 'm:area/cave') end\n"));
        for (int i = 0; i < 5; ++i)
            CHECK(h.fireAreaTick("m:area/cave", 100 + i));
        CHECK(!h.fireAreaTick("base:area/0", 1)); // nothing on that area
        CHECK(h.runString("m", "assert(n == 5 and last == 104 and math.abs(dt - 1 / 30) < 1e-9)"));
        CHECK(h.errors().empty());
    }
    {
        // an onTick that waits is not started again until it has finished (no pile-up), and wait() works inside it
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "starts, ends = 0, 0\n"
                          "local A = Area.hooks('m:area/cave')\n"
                          "function A:onTick(ctx) starts = starts + 1; wait(3); ends = ends + 1 end\n"));
        CHECK(h.fireAreaTick("m:area/cave", 1));
        CHECK(!h.fireAreaTick("m:area/cave", 2) && !h.fireAreaTick("m:area/cave", 3)); // still waiting
        CHECK(h.waiting() == 1);
        h.tick();
        h.tick();
        h.tick(); // the wait is over: the script finishes
        CHECK(h.waiting() == 0);
        CHECK(h.fireAreaTick("m:area/cave", 4)); // and a new one may start
        CHECK(h.runString("m", "assert(starts == 2 and ends == 1, starts .. ',' .. ends)"));
    }
    {
        // a failing onTick is switched off for the session (one error, not one per tick); the area's other hooks stay on
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "local A = Area.hooks('m:area/cave')\n"
                          "function A:onTick(ctx) error('boom') end\n"
                          "function A:onEnter(ctx) ok = true end\n"));
        CHECK(!h.fireAreaTick("m:area/cave", 1));
        CHECK(!h.fireAreaTick("m:area/cave", 2) && !h.fireAreaTick("m:area/cave", 3));
        CHECK(h.errors().size() == 1);
        CHECK(h.fireAreaEnter("m:area/cave", "") && h.runString("m", "assert(ok == true)"));
    }
    {
        // the new names are known hooks (validation), a misspelling is not
        gp::ScriptHost h;
        CHECK(h.runString("m",
                          "local A = Area.hooks('m:area/cave')\n"
                          "function A:onEnter(ctx) end\nfunction A:onLeave(ctx) end\nfunction A:onTick(ctx) end\nfunction A:onTicks(ctx) end\n"
                          "local G = Game.hooks()\nfunction G:onEnterArea(ctx) end\n"));
        int known = 0, unknown = 0;
        for (const auto &hk : h.hooks())
            for (const auto &fn : hk.fns)
                (fn.known ? known : unknown)++;
        CHECK(known == 4 && unknown == 1);
    }
    // --- object.* and npc.* reach the game's callbacks, with checked arguments ---
    {
        gp::ScriptHost h;
        wire(h);
        std::vector<std::string> calls;
        h.api().spawnObject = [&](const std::string &id, const std::string &type, int x, int y, int rx, int ry)
        {
            calls.push_back("spawn " + id + " " + type + " " + std::to_string(x) + "," + std::to_string(y) + " r" + std::to_string(rx) + "x" + std::to_string(ry));
            return type != "m:objtype/none"; // an unknown type
        };
        h.api().moveObject = [&](const std::string &id, int x, int y)
        {
            calls.push_back("move " + id + " " + std::to_string(x) + "," + std::to_string(y));
            return id != "gone";
        };
        h.api().removeObject = [&](const std::string &id)
        {
            calls.push_back("remove " + id);
            return id != "gone";
        };
        h.api().spawnNpc = [&](const std::string &id, int x, int y)
        {
            calls.push_back("npc.spawn " + id + " " + std::to_string(x) + "," + std::to_string(y));
            return id == "m:npc/a";
        };
        h.api().moveNpc = [&](const std::string &id, int x, int y)
        {
            calls.push_back("npc.move " + id + " " + std::to_string(x) + "," + std::to_string(y));
            return true;
        };
        h.api().removeNpc = [&](const std::string &id)
        {
            calls.push_back("npc.remove " + id);
            return false;
        };
        CHECK(h.runString("mymod",
                          "a = object.spawn('m:objtype/rock', 10, 20)\n"
                          "b = object.spawn('m:objtype/door', 30, 40, { reach = { 25, 18 } })\n"
                          "c = object.spawn('m:objtype/none', 1, 2)\n"
                          "assert(a == 'mymod:obj/s1' and b == 'mymod:obj/s2' and c == nil, tostring(a) .. tostring(b) .. tostring(c))\n"
                          "assert(object.move(a, 5, 6) == true and object.move('gone', 1, 1) == false)\n"
                          "assert(object.remove(a) == true and object.remove('gone') == false)\n"
                          "assert(npc.spawn('m:npc/a', 7, 8) == true and npc.spawn('m:npc/x', 1, 1) == false)\n"
                          "assert(npc.move('m:npc/a', 9, 9) == true and npc.remove('m:npc/a') == false)\n"));
        CHECK(h.errors().empty());
        const std::vector<std::string> want = {"spawn mymod:obj/s1 m:objtype/rock 10,20 r0x0", "spawn mymod:obj/s2 m:objtype/door 30,40 r25x18", "spawn mymod:obj/s3 m:objtype/none 1,2 r0x0",
                                               "move mymod:obj/s1 5,6", "move gone 1,1", "remove mymod:obj/s1", "remove gone",
                                               "npc.spawn m:npc/a 7,8", "npc.spawn m:npc/x 1,1", "npc.move m:npc/a 9,9", "npc.remove m:npc/a"};
        CHECK(calls == want);
        // bad arguments are script errors, not silent: a position out of range, a reach that is not {rx, ry}, an id with a forbidden character
        CHECK(!h.runString("mymod", "object.spawn('m:objtype/rock', 999999, 0)"));
        CHECK(!h.runString("mymod", "object.spawn('m:objtype/rock', 1, 1, { reach = { 0, 5 } })"));
        CHECK(!h.runString("mymod", "object.spawn('m:objtype/rock', 1, 1, { reach = 5 })"));
        CHECK(!h.runString("mymod", "object.move(1)"));
        CHECK(h.runString("mymod", "assert(object.remove('has space') == false and npc.spawn('a,b', 1, 1) == false)")); // never reaches the game
        CHECK(calls == want);
        // an unwired host does nothing
        gp::ScriptHost bare;
        CHECK(bare.runString("m", "assert(object.spawn('x', 1, 1) == nil and object.move('x', 1, 1) == false and npc.spawn('x', 1, 1) == false and npc.remove('x') == false)"));
        CHECK(bare.errors().empty());
    }
    // --- seams: Game onBefore* (return false cancels) and onAfter* hooks ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "log_ = {}\n"
                          "local G = Game.hooks()\n"
                          "function G:onBeforeDig(ctx) log_[#log_ + 1] = 'bd ' .. ctx.area.id .. ' ' .. ctx.x .. ',' .. ctx.y .. ' ' .. ctx.player.name; if ctx.x < 0 then return false end end\n"
                          "function G:onAfterDig(ctx) log_[#log_ + 1] = 'ad ' .. ctx.result .. ' ' .. tostring(ctx.item and ctx.item.id) end\n"
                          "function G:onBeforePickup(ctx) log_[#log_ + 1] = 'bp ' .. ctx.item.id .. ' ' .. ctx.x .. ',' .. ctx.y; return ctx.item.id ~= 'base:item/9' end\n"
                          "function G:onAfterPickup(ctx) log_[#log_ + 1] = 'ap ' .. ctx.item.id end\n"
                          "function G:onBeforeBuy(ctx) log_[#log_ + 1] = 'bb ' .. ctx.item.id .. ' ' .. ctx.price; return ctx.price < 100 end\n"
                          "function G:onAfterBuy(ctx) log_[#log_ + 1] = 'ab ' .. ctx.item.id .. ' x' .. ctx.quantity .. ' @' .. ctx.price end\n"
                          "function G:onBeforeExit(ctx) log_[#log_ + 1] = 'be ' .. ctx.area.id .. '>' .. ctx.to.id .. ' via ' .. ctx.exit.id; if ctx.to.id == 'm:area/locked' then return false end end\n"));
        const auto d1 = h.fireBeforeDig("base:area/0", 10, 20);
        CHECK(d1.ran && !d1.suppress);                                   // no return value: the dig goes ahead
        CHECK(h.fireBeforeDig("base:area/0", -1, 5).suppress);           // return false: called off
        CHECK(h.fireAfterDig("base:area/0", 10, 20, "item", "base:item/173"));
        CHECK(h.fireAfterDig("base:area/0", 10, 20, "miss", ""));
        CHECK(!h.fireBeforePickup("base:area/0", "base:item/1", 3, 4).suppress);
        CHECK(h.fireBeforePickup("base:area/0", "base:item/9", 3, 4).suppress);
        CHECK(h.fireAfterPickup("base:area/0", "base:item/1", 3, 4));
        CHECK(!h.fireBeforeBuy("base:area/23", "base:item/5", 40).suppress && h.fireBeforeBuy("base:area/23", "base:item/5", 400).suppress);
        CHECK(h.fireAfterBuy("base:area/23", "base:item/5", 40, 3));
        CHECK(!h.fireBeforeExit("base:area/0", "base:area/1", "base:exit/a").suppress && h.fireBeforeExit("base:area/0", "m:area/locked", "m:exit/x").suppress);
        CHECK(h.runString("m",
                          "local want = { 'bd base:area/0 10,20 Rex', 'bd base:area/0 -1,5 Rex', 'ad item base:item/173', 'ad miss nil', 'bp base:item/1 3,4', 'bp base:item/9 3,4', 'ap base:item/1',\n"
                          "  'bb base:item/5 40', 'bb base:item/5 400', 'ab base:item/5 x3 @40', 'be base:area/0>base:area/1 via base:exit/a', 'be base:area/0>m:area/locked via m:exit/x' }\n"
                          "assert(#log_ == #want, #log_)\n"
                          "for i = 1, #want do assert(log_[i] == want[i], i .. ': ' .. log_[i]) end"));
        CHECK(h.errors().empty());
    }
    { // a seam with no script, an error (switches only that seam off), and a hook that waits (counts as taking over)
        gp::ScriptHost h;
        wire(h);
        CHECK(!h.fireBeforeDig("a", 1, 1).ran && !h.fireBeforeDig("a", 1, 1).suppress && !h.fireAfterDig("a", 1, 1, "miss", ""));
        CHECK(h.runString("m",
                          "local G = Game.hooks()\n"
                          "function G:onBeforeDig(ctx) error('boom') end\n"
                          "function G:onAfterDig(ctx) fired_ = (fired_ or 0) + 1 end\n"
                          "function G:onBeforeExit(ctx) wait(10) end\n"));
        CHECK(!h.fireBeforeDig("a", 1, 1).suppress);            // an error never cancels
        CHECK(!h.errors().empty());
        CHECK(h.fireAfterDig("a", 1, 1, "miss", ""));           // the other seam is unaffected
        CHECK(!h.fireBeforeDig("a", 1, 1).ran);                 // the one that failed is off for the session
        CHECK(h.fireBeforeExit("a", "b", "x").suppress);        // waiting = the script took over: the door stays shut meanwhile
        CHECK(h.runString("m", "assert(fired_ == 1)"));
        // the names are real hook names (a typo in a script would be caught at start-up)
        const auto names = gp::ScriptHost::hookNames('G');
        for (const char *n : {"onBeforeDig", "onAfterDig", "onBeforePickup", "onAfterPickup", "onBeforeBuy", "onAfterBuy", "onBeforeExit"})
            CHECK(std::find(names.begin(), names.end(), n) != names.end());
    }
    // --- onBeforeItemUse: the global one, ahead of the item's own hook ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "order_ = {}\n"
                          "local G = Game.hooks()\n"
                          "function G:onBeforeItemUse(ctx) order_[#order_ + 1] = 'g ' .. ctx.item.id .. ' ' .. ctx.player.name; if ctx.item.id == 'm:item/locked' then return false end end\n"
                          "local I = Item.hooks('m:item/pie')\n"
                          "function I:onItemUse(ctx) order_[#order_ + 1] = 'i ' .. ctx.item.id end\n"));
        CHECK(!h.fireBeforeItemUse("m:item/pie").suppress && !h.fireItemUse("m:item/pie").suppress);
        CHECK(h.fireBeforeItemUse("m:item/locked").suppress);
        CHECK(h.runString("m", "assert(#order_ == 3 and order_[1] == 'g m:item/pie Rex' and order_[2] == 'i m:item/pie' and order_[3] == 'g m:item/locked Rex', table.concat(order_, '|'))"));
        const auto names = gp::ScriptHost::hookNames('G');
        CHECK(std::find(names.begin(), names.end(), "onBeforeItemUse") != names.end());
        CHECK(h.errors().empty());
    }
    // --- onStatChange (a number changes the delta, false cancels, no waiting), onBefriend, onQuestAccept / onQuestDecline ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.fireStatChange(2, "", 50, 100) == 50); // no script: unchanged
        CHECK(h.runString("m",
                          "seen_ = {}\n"
                          "local G = Game.hooks()\n"
                          "function G:onStatChange(ctx)\n"
                          "  seen_[#seen_ + 1] = ctx.kind .. ':' .. tostring(ctx.stat) .. ':' .. ctx.delta .. ':' .. ctx.value\n"
                          "  if ctx.kind == 'money' and ctx.delta > 0 then return ctx.delta * 2 end\n"          // double what the pet earns
                          "  if ctx.kind == 'care' and ctx.stat == 'hunger' and ctx.delta < 0 then return false end\n" // hunger never drops
                          "  if ctx.kind == 'personality' then return 2.6 end\n"                                // rounds to 3
                          "end\n"));
        CHECK(h.fireStatChange(2, "", 50, 100) == 100);
        CHECK(h.fireStatChange(2, "", -20, 100) == -20);         // returned nothing: unchanged
        CHECK(h.fireStatChange(0, "hunger", -5, 80) == 0);             // false cancels
        CHECK(h.fireStatChange(0, "health", -5, 80) == -5);            // another care stat
        CHECK(h.fireStatChange(1, "stars", 10, 300) == 3);            // a number is rounded
        CHECK(h.runString("m",
                          "local want = { 'money:nil:50:100', 'money:nil:-20:100', 'care:hunger:-5:80', 'care:health:-5:80', 'personality:stars:10:300' }\n"
                          "assert(#seen_ == #want, #seen_)\n"
                          "for i = 1, #want do assert(seen_[i] == want[i], i .. ': ' .. seen_[i]) end"));
        CHECK(h.errors().empty());
    }
    { // a change the hook makes itself is not passed through the hook again; waiting cancels; an error leaves the change alone
        gp::ScriptHost h;
        wire(h);
        int adjusts = 0;
        h.api().adjustMoney = [&](int d)
        {
            ++adjusts;
            return h.fireStatChange(2, "", d, 0); // what Player::adjustMoney does through its filter
        };
        CHECK(h.runString("m", "local G = Game.hooks(); function G:onStatChange(ctx) hits_ = (hits_ or 0) + 1; player.adjustMoney(5); return ctx.delta end"));
        CHECK(h.fireStatChange(2, "", 10, 0) == 10);
        CHECK(adjusts == 1);
        CHECK(h.runString("m", "assert(hits_ == 1, hits_)"));
        gp::ScriptHost w;
        wire(w);
        CHECK(w.runString("m", "local G = Game.hooks(); function G:onStatChange(ctx) wait(5) end"));
        CHECK(w.fireStatChange(2, "", 10, 0) == 0);
        gp::ScriptHost e;
        wire(e);
        CHECK(e.runString("m", "local G = Game.hooks(); function G:onStatChange(ctx) error('boom') end"));
        CHECK(e.fireStatChange(2, "", 10, 0) == 10 && !e.errors().empty());
    }
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "log_ = {}\n"
                          "local G = Game.hooks()\n"
                          "function G:onBefriend(ctx) log_[#log_ + 1] = 'bf ' .. ctx.npc.id .. ' ' .. tostring(ctx.again) end\n"
                          "function G:onQuestAccept(ctx) log_[#log_ + 1] = 'qa ' .. tostring(ctx.quest.id) .. ' ' .. ctx.quest.number .. ' ' .. tostring(ctx.giver.id) end\n"
                          "function G:onQuestDecline(ctx) log_[#log_ + 1] = 'qd ' .. ctx.quest.number end\n"));
        gp::QuestCompleteInfo q;
        q.questId = "base:quest/3";
        q.number = 3;
        q.giverId = "base:npc/66";
        CHECK(h.fireBefriend("base:npc/12", false) && h.fireBefriend("base:npc/12", true));
        CHECK(h.fireQuestAnswer(true, q) && h.fireQuestAnswer(false, q));
        CHECK(h.runString("m",
                          "local want = { 'bf base:npc/12 false', 'bf base:npc/12 true', 'qa base:quest/3 3 base:npc/66', 'qd 3' }\n"
                          "assert(#log_ == #want, #log_)\n"
                          "for i = 1, #want do assert(log_[i] == want[i], i .. ': ' .. log_[i]) end"));
        const auto names = gp::ScriptHost::hookNames('G');
        for (const char *n : {"onStatChange", "onBefriend", "onQuestAccept", "onQuestDecline"})
            CHECK(std::find(names.begin(), names.end(), n) != names.end());
        CHECK(h.errors().empty());
    }
    // --- scenes: scene.open / close, onOpen / onTick / onDraw, the draw list, onSceneClose ---
    {
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "log_ = {}\n"
                          "local S = Scene.hooks('m:scene/catch')\n"
                          "function S:onOpen(ctx) score = 0; log_[#log_ + 1] = 'open ' .. tostring(ctx.arg) .. ' ' .. ctx.player.name end\n"
                          "function S:onTick(ctx)\n"
                          "  if ctx.input.z then score = score + 1 end\n"
                          "  if ctx.input.left then log_[#log_ + 1] = 'left t' .. ctx.tick end\n"
                          "  if ctx.input.x then scene.close(score) end\n"
                          "end\n"
                          "function S:onDraw(g)\n"
                          "  g.clear({ 10, 20, 30 })\n"
                          "  g.rect(5, 6, 70, 8, { 255, 0, 0, 128 })\n"
                          "  g.rect(1, 2, 3, 4, { 1, 2, 3 }, false)\n"
                          "  g.sprite('m:anim/star', 160, 120, { frame = 2, flip = true })\n"
                          "  g.text('m:str/score', 20, 30, { align = 'center', width = 100, font = 3, vars = { score = score } })\n"
                          "  g.num(score, 300, 10, { align = 'right' })\n"
                          "end\n"
                          "local G = Game.hooks()\n"
                          "function G:onSceneClose(ctx) log_[#log_ + 1] = 'closed ' .. ctx.scene.id .. ' ' .. tostring(ctx.result) end\n"));
        std::string id;
        CHECK(!h.takeSceneOpen(id) && !h.sceneOpen());                                                          // nothing requested
        CHECK(h.runString("m", "assert(scene.isOpen() == false and scene.close() == false)"));
        CHECK(!h.runString("m", "scene.open('m:scene/none')"));                                                 // an undefined scene is an error
        CHECK(h.runString("m", "assert(scene.open('m:scene/catch', 7) == true and scene.open('m:scene/catch') == false)")); // one request at a time
        CHECK(h.takeSceneOpen(id) && id == "m:scene/catch" && !h.takeSceneOpen(id));
        h.beginScene(id);
        CHECK(h.sceneOpen() && !h.sceneCloseRequested());
        gp::SceneInput in;
        in.z = true;
        CHECK(h.tickScene(in) && h.tickScene(in));
        in = {};
        in.left = true;
        CHECK(h.tickScene(in));
        const std::vector<gp::DrawCmd> cmds = h.drawScene();
        CHECK(cmds.size() == 6);
        if (cmds.size() == 6)
        {
            CHECK(cmds[0].kind == gp::DrawCmd::Kind::Clear && cmds[0].r == 10 && cmds[0].g == 20 && cmds[0].b == 30 && cmds[0].a == 255);
            CHECK(cmds[1].kind == gp::DrawCmd::Kind::Rect && cmds[1].x == 5 && cmds[1].y == 6 && cmds[1].w == 70 && cmds[1].h == 8 && cmds[1].r == 255 && cmds[1].a == 128 && cmds[1].fill);
            CHECK(cmds[2].kind == gp::DrawCmd::Kind::Rect && !cmds[2].fill);
            CHECK(cmds[3].kind == gp::DrawCmd::Kind::Sprite && cmds[3].str == "m:anim/star" && cmds[3].x == 160 && cmds[3].y == 120 && cmds[3].frame == 2 && cmds[3].flip);
            CHECK(cmds[4].kind == gp::DrawCmd::Kind::Text && cmds[4].str == "m:str/score" && cmds[4].align == 1 && cmds[4].width == 100 && cmds[4].font == 3 &&
                  cmds[4].vars.size() == 1 && cmds[4].vars[0].first == "score" && cmds[4].vars[0].second == "2");
            CHECK(cmds[5].kind == gp::DrawCmd::Kind::Num && cmds[5].num == 2 && cmds[5].align == 2 && cmds[5].x == 300);
        }
        CHECK(!h.sceneCloseRequested());
        in = {};
        in.x = true;
        h.tickScene(in); // scene.close(score)
        CHECK(h.sceneCloseRequested());
        CHECK(h.drawScene().empty()); // nothing is drawn once it is closing
        h.endScene();
        CHECK(!h.sceneOpen() && !h.sceneCloseRequested());
        CHECK(h.runString("m",
                          "local want = { 'open 7 Rex', 'left t3', 'closed m:scene/catch 2' }\n"
                          "assert(#log_ == #want, table.concat(log_, '|'))\n"
                          "for i = 1, #want do assert(log_[i] == want[i], i .. ': ' .. log_[i]) end"));
        CHECK(h.errors().size() == 1); // (only the undefined scene's error from above)
    }
    { // held keys (ctx.held) next to the pressed ones (ctx.input), and sound.play
        gp::ScriptHost h;
        wire(h);
        std::vector<std::pair<int, int>> sounds;
        h.api().playSound = [&](int id, int vol) { sounds.push_back({id, vol}); };
        CHECK(h.runString("m",
                          "log_ = {}\n"
                          "local S = Scene.hooks('m:scene/held')\n"
                          "function S:onTick(ctx)\n"
                          "  log_[#log_ + 1] = (ctx.held.left and 'L' or '-') .. (ctx.held.right and 'R' or '-') .. (ctx.held.z and 'Z' or '-') .. (ctx.input.left and 'l' or '-')\n"
                          "  if ctx.held.z then sound.play(0x59); sound.play(7, 100) end\n"
                          "end\n"));
        std::string id;
        CHECK(h.runString("m", "scene.open('m:scene/held')") && h.takeSceneOpen(id));
        h.beginScene(id);
        gp::SceneInput in;
        in.heldLeft = true;
        in.left = true; // pressed this tick
        h.tickScene(in);
        in = {};
        in.heldLeft = in.heldRight = in.heldZ = true; // still held, not pressed again
        h.tickScene(in);
        CHECK(h.runString("m", "assert(log_[1] == 'L--l' and log_[2] == 'LRZ-', table.concat(log_, '|'))"));
        CHECK((sounds == std::vector<std::pair<int, int>>({{0x59, 255}, {7, 100}})));
        CHECK(!h.runString("m", "sound.play(512)") && !h.runString("m", "sound.play(1, 300)"));
        gp::ScriptHost bare; // no sound device wired: nothing happens
        CHECK(bare.runString("m", "sound.play(1)"));
        h.endScene();
    }
    { // mistakes: draw calls outside onDraw, bad arguments, too many calls, a failing scene closes itself
        gp::ScriptHost h;
        wire(h);
        CHECK(h.runString("m",
                          "local S = Scene.hooks('m:scene/a')\n"
                          "function S:onOpen(ctx) kept_ = nil end\n"
                          "function S:onDraw(g) kept_ = g; g.clear({ 0, 0, 0 }) end\n"
                          "local B = Scene.hooks('m:scene/bad')\n"
                          "function B:onDraw(g) g.rect(1, 2, 3, 4, { 300, 0, 0 }) end\n"
                          "local C = Scene.hooks('m:scene/many')\n"
                          "function C:onDraw(g) for i = 1, 4001 do g.rect(0, 0, 1, 1, { 1, 1, 1 }) end end\n"
                          "local D = Scene.hooks('m:scene/tickerr')\n"
                          "function D:onTick(ctx) error('boom') end\n"));
        std::string id;
        CHECK(h.runString("m", "scene.open('m:scene/a')") && h.takeSceneOpen(id));
        h.beginScene(id);
        CHECK(h.drawScene().size() == 1);
        CHECK(h.runString("m", "local ok = pcall(kept_.clear, { 1, 1, 1 }); assert(not ok)")); // the draw handle is only good inside onDraw
        h.endScene();
        for (const char *scene : {"m:scene/bad", "m:scene/many", "m:scene/tickerr"})
        {
            CHECK(h.runString("m", std::string("scene.open('") + scene + "')") && h.takeSceneOpen(id));
            h.beginScene(id);
            CHECK(h.sceneOpen() && !h.sceneCloseRequested());
            h.tickScene({});
            h.drawScene();
            CHECK(h.sceneCloseRequested()); // the failed hook is off for the session and the scene closes
            h.endScene();
        }
        CHECK(h.errors().size() == 3);
        // an invalid argument and the hook names
        CHECK(!h.runString("m", "scene.open('m:scene/a', {})") && !h.runString("m", "scene.close({})"));
        const auto names = gp::ScriptHost::hookNames('S');
        CHECK(names == std::vector<std::string>({"onOpen", "onTick", "onDraw"}));
        const auto gnames = gp::ScriptHost::hookNames('G');
        CHECK(std::find(gnames.begin(), gnames.end(), "onSceneClose") != gnames.end());
    }
    { // wait() inside a scene's onTick keeps it from piling up; the host with no scene open does nothing
        gp::ScriptHost h;
        wire(h);
        CHECK(!h.tickScene({}) && h.drawScene().empty() && !h.sceneCloseRequested());
        CHECK(h.runString("m", "local S = Scene.hooks('m:scene/w'); function S:onTick(ctx) n_ = (n_ or 0) + 1; wait(3) end"));
        std::string id;
        CHECK(h.runString("m", "scene.open('m:scene/w')") && h.takeSceneOpen(id));
        h.beginScene(id);
        h.tickScene({});
        h.tickScene({});
        h.tickScene({});
        CHECK(h.runString("m", "assert(n_ == 1, n_)")); // still waiting: no second copy started
        h.tick();
        h.tick();
        h.tick();
        h.tickScene({});
        CHECK(h.runString("m", "assert(n_ == 2, n_)"));
        h.endScene();
    }
    std::printf(fails ? "FAILED: %d check(s)\n" : "script_hooks_test: all passed\n", fails);
    return fails ? 1 : 0;
}