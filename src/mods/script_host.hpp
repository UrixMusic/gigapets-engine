// script_host.hpp - runs mod Lua scripts. One state for the whole game, sandboxed:
// no io/os/package/debug, no load/dofile, no math.random (use rand.*), a memory cap, and an
// instruction limit on every call into a script.
// API: Npc.hooks (onTalkStart, onTalk), Area.hooks (onInteract, onEnter, onLeave, onTick), Item.hooks (onItemUse), Game.hooks (onQuestComplete, onEnterArea), world.drop, rand.int/pick, player.give/take/count/hasFlag/setFlag, log/print, say, ask, wait.
// Every hook runs as a coroutine, so a hook can wait: ask(textId) asks a yes/no question and gives the answer, wait(ticks) pauses
// for some game ticks (30 per second). A hook that waits has taken over (as if it had returned false) and carries on later.
#pragma once
extern "C"
{
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../engine/registry.hpp"

namespace gp
{
    // What scripts can do to the game. walk.cpp fills these in; an unset one does nothing.
    struct ScriptApi
    {
        std::function<bool(const std::string &item, int x, int y)> drop; // false = unknown item
        std::function<int(int lo, int hi)> rnd;                          // lo inclusive, hi EXCLUSIVE (RandFn)
        // The player. An unset callback does nothing (give/take/count return 0, hasFlag false).
        std::function<int(const std::string &item, int n)> give; // returns how many were added (0 = unknown item or full)
        std::function<int(const std::string &item, int n)> take; // returns how many were removed (at most what is held)
        std::function<int(const std::string &item)> count;
        std::function<bool(const std::string &flag)> hasFlag;
        std::function<void(const std::string &flag)> setFlag; // the host has already checked the mod prefix and characters
        // The pet's stats, by name (the host has already checked the name against careStatNames() /
        // personalityStatNames()). adjust* return the change that was actually made: stats are clamped, and the game
        // ignores a negative care change until 4 quests are done, so it can differ from `delta`.
        std::function<int(const std::string &stat)> getCare;
        std::function<int(const std::string &stat, int delta)> adjustCare;
        std::function<int(const std::string &stat)> getPersonality;
        std::function<int(const std::string &stat, int delta)> adjustPersonality;
        // Money (0..99999). adjustMoney returns the change actually made, like the stats.
        std::function<int()> getMoney;
        std::function<int(int delta)> adjustMoney;
        // The pet's illness as the ROM's illness number (the index into illnessNames()), -1 = healthy.
        std::function<int()> getIllness;
        std::function<void(int illness)> setIllness; // -1 = healthy
        // say(textId, vars): queue a message box. Returns false for a text id the db doesn't know (nothing is shown).
        // `vars` are the {name} placeholders for fillTemplate: names are [A-Za-z0-9_], values are strings or numbers.
        using SayVars = std::vector<std::pair<std::string, std::string>>;
        std::function<bool(const std::string &textId, const SayVars &vars)> say;
        // ask(textId, vars): queue a yes/no question after any queued say() boxes. Returns false for a text id the db doesn't know
        // (nothing is shown). `answer(true)` for Yes, `answer(false)` for No is called when the player chooses; the host then
        // carries the waiting script on.
        std::function<bool(const std::string &textId, const SayVars &vars, std::function<void(bool)> answer)> ask;
        // The HUD objectives (objective.set / clear / has). The host has already checked the id (the mod's prefix, allowed
        // characters) and the numbers. setObjective returns false for a text or item id the db doesn't know, or when too many
        // are up (nothing changes); `item` and `text` are empty when not given, `need` is 0 when there is no counter.
        std::function<bool(const std::string &id, const std::string &text, const std::string &item, int need)> setObjective;
        std::function<bool(const std::string &id)> clearObjective; // true if there was one
        std::function<bool(const std::string &id)> hasObjective;
        // Objects and NPCs in the area the pet is in (object.* / npc.* in Lua). They last until the pet leaves the area.
        // spawnObject: `id` is made by the host and is new; false = unknown object type. reachX/reachY are 0 for a plain object, or the
        // reach box that makes it usable (Z near it, and Area.hooks onInteract fires). move/remove work on any object of the area, the
        // area's own too; false = no such object. The NPC calls take an NPC record id of the db: spawnNpc puts that NPC in this area at
        // x, y (false = unknown, already here, or too many NPCs); moveNpc / removeNpc work on any NPC that is here (false = not here).
        std::function<bool(const std::string &id, const std::string &objtype, int x, int y, int reachX, int reachY)> spawnObject;
        std::function<bool(const std::string &id, int x, int y)> moveObject;
        std::function<bool(const std::string &id)> removeObject;
        std::function<bool(const std::string &npc, int x, int y)> spawnNpc;
        std::function<bool(const std::string &npc, int x, int y)> moveNpc;
        std::function<bool(const std::string &npc)> removeNpc;
        // A read-only snapshot of the player for ctx.player. Unset = ctx.player is an empty table.
        struct PlayerInfo
        {
            std::string name;
            int money = 0;
            int questsCompleted = 0;
        };
        std::function<PlayerInfo()> playerInfo;
        // Called for every script error as it happens ("[mod] what: message"), so runtime errors in hooks are not
        // silent. Errors are also always collected in ScriptHost::errors().
        std::function<void(int id, int volume)> playSound; // sound.play: a sound effect of the game (the ids are the ROM's)
        std::function<void(const std::string &message)> onError;
    };

    // One registered hook table, for validation: who registered it, where, and what functions it defines.
    struct HookInfo
    {
        struct Fn
        {
            std::string name;
            std::string where; // "file:line" where the function is defined
            bool known = false; // is `name` one of the hooks the game actually fires for this kind?
        };
        char kind = 'N';  // 'N' = Npc.hooks, 'A' = Area.hooks, 'I' = Item.hooks, 'G' = Game.hooks (id is empty)
        std::string id;   // the id passed to Npc.hooks / Area.hooks / Item.hooks
        std::string mod;  // the mod whose script registered it
        std::string where; // "file:line" of the Npc.hooks / Area.hooks call
        std::string valid; // the hook names valid for this kind, for messages ("onTalkStart, onTalk")
        std::vector<Fn> fns;
    };

    // What the game tells onQuestComplete. Ids are strings; an empty one becomes nil in the script.
    struct QuestCompleteInfo
    {
        std::string questId; // "base:quest/12" for a table quest; empty for a generated one
        int number = 0;      // its place in the chain (the completed-quests counter, starting at 1)
        bool generic = false;
        std::string type;    // quest type id ("base:questtype/1"); empty if unknown
        std::string giverId, receiverId; // NPC ids; empty if that NPC isn't in the db
    };

    // What a hook that can steer the game returned.
    struct HookResult
    {
        bool ran = false;      // a script function was found and finished without an error
        bool suppress = false; // it returned exactly `false`: skip the default behaviour
    };


    // What a scene's onTick is told about the keys this tick (pressed this tick, not held).
    struct SceneInput
    {
        bool z = false, x = false, left = false, right = false, up = false, down = false;
        bool heldZ = false, heldX = false, heldLeft = false, heldRight = false, heldUp = false, heldDown = false; // held down right now
    };

    // One drawing call a scene's onDraw made. The script only fills a list of these; the game draws them (a script never touches the renderer).
    struct DrawCmd
    {
        enum class Kind
        {
            Clear,
            Rect,
            Sprite,
            Text,
            Num
        };
        Kind kind = Kind::Clear;
        int x = 0, y = 0, w = 0, h = 0;
        int r = 0, g = 0, b = 0, a = 255; // Clear, Rect
        bool fill = true;                 // Rect: false = an outline
        std::string str;                  // Sprite: the picture's id; Text: the text id
        double num = 0;                   // Num: the number
        int frame = 0;                    // Sprite: frame number from 0
        bool flip = false;                // Sprite: mirrored left to right
        int align = 0;                    // Text, Num: 0 left, 1 centre, 2 right (inside `width` pixels when width > 0, else around x)
        int width = 0;
        int font = 0;                     // 0 = the dialogue font, 3 = the title font
        std::vector<std::pair<std::string, std::string>> vars; // Text: {name} fills
    };

    class ScriptHost
    {
    public:
        ScriptHost()
        {
            L_ = lua_newstate(alloc, &mem_);
            if (!L_)
                return;
            open("_G", luaopen_base);
            open(LUA_TABLIBNAME, luaopen_table);
            open(LUA_STRLIBNAME, luaopen_string);
            open(LUA_MATHLIBNAME, luaopen_math);
            open(LUA_UTF8LIBNAME, luaopen_utf8);
            for (const char *g : {"dofile", "loadfile", "load", "collectgarbage"})
            {
                lua_pushnil(L_);
                lua_setglobal(L_, g);
            }
            dropField("string", "dump");
            dropField("math", "random");
            dropField("math", "randomseed");

            bindGlobal("print", l_log);
            bindGlobal("log", l_log);
            bindGlobal("say", l_say);
            bindGlobal("ask", l_ask);
            bindGlobal("wait", l_wait);
            bind("Npc", "hooks", l_npcHooks);
            bind("Area", "hooks", l_areaHooks);
            bind("Item", "hooks", l_itemHooks);
            bind("Game", "hooks", l_gameHooks);
            bind("world", "drop", l_drop);
            bind("rand", "int", l_randInt);
            bind("rand", "pick", l_randPick);
            bind("player", "give", l_pGive);
            bind("player", "take", l_pTake);
            bind("player", "count", l_pCount);
            bind("player", "hasFlag", l_pHasFlag);
            bind("player", "setFlag", l_pSetFlag);
            bind("player", "care", l_pCare);
            bind("player", "adjustCare", l_pAdjustCare);
            bind("player", "personality", l_pPersonality);
            bind("player", "adjustPersonality", l_pAdjustPersonality);
            bind("player", "money", l_pMoney);
            bind("player", "adjustMoney", l_pAdjustMoney);
            bind("player", "illness", l_pIllness);
            bind("player", "setIllness", l_pSetIllness);
            bind("player", "cure", l_pCure);
            bind("object", "spawn", l_oSpawn);
            bind("object", "move", l_oMove);
            bind("object", "remove", l_oRemove);
            bind("npc", "spawn", l_nSpawn);
            bind("npc", "move", l_nMove);
            bind("npc", "remove", l_nRemove);
            bind("engine", "get", l_eGet);
            bind("engine", "set", l_eSet);
            bind("engine", "has", l_eHas);
            bind("engine", "list", l_eList);
            bind("engine", "describe", l_eDescribe);
            bind("save", "get", l_sGet);
            bind("save", "set", l_sSet);
            bind("save", "keys", l_sKeys);
            bind("Scene", "hooks", l_sceneHooks);
            bind("sound", "play", l_soundPlay);
            bind("scene", "open", l_scOpen);
            bind("scene", "close", l_scClose);
            bind("scene", "isOpen", l_scIsOpen);
            bind("objective", "set", l_objSet);
            bind("objective", "clear", l_objClear);
            bind("objective", "has", l_objHas);
        }
        ~ScriptHost()
        {
            if (L_)
                lua_close(L_);
        }
        ScriptHost(const ScriptHost &) = delete;
        ScriptHost &operator=(const ScriptHost &) = delete;

        ScriptApi &api() { return api_; }

        // The engine values scripts can reach through engine.get / engine.set / engine.list. The registry must outlive the host's use.
        void setRegistry(EngineRegistry *r) { registry_ = r; }

        // What a mod keeps in the save (save.get / save.set). The game hands over the loaded slot's key/values (keys starting with
        // "mod.") when it starts, fires fireGameSave, then writes modSaveKv() into the slot.
        void loadModSave(const std::map<std::string, std::string> &kv)
        {
            modSave_.clear();
            for (const auto &[k, v] : kv)
                if (k.rfind("mod.", 0) == 0 && k.size() > 4)
                    modSave_[k.substr(4)] = v;
        }
        std::map<std::string, std::string> modSaveKv() const
        {
            std::map<std::string, std::string> out;
            for (const auto &[k, v] : modSave_)
                out["mod." + k] = v;
            return out;
        }
        const std::vector<std::string> &errors() const { return errors_; }

        // Once per game tick (30 Hz): scripts that called wait(n) count down and carry on.
        void tick()
        {
            std::vector<int> due;
            for (Routine &r : routines_)
                if (r.wait == Wait::Ticks && --r.ticks <= 0)
                    due.push_back(r.id);
            for (int id : due)
                if (Routine *r = routineById(id))
                {
                    r->wait = Wait::None;
                    resume(id, 0, nullptr);
                }
        }
        // The player answered a question a script asked (see ScriptApi::ask). Unknown or stale ids do nothing.
        void answerRoutine(int id, bool yes)
        {
            Routine *r = routineById(id);
            if (!r || r->wait != Wait::Asking)
                return;
            r->wait = Wait::None;
            lua_pushboolean(r->co, yes ? 1 : 0);
            resume(id, 1, nullptr);
        }
        // How many scripts are waiting for an answer or a timer (for tests and for the game to know it is not idle).
        size_t waiting() const { return routines_.size(); }

        // Runs mods/<id>/scripts/*.lua in name order. Top-level code only registers hooks.
        void loadMod(const std::string &modId, const std::filesystem::path &dir)
        {
            namespace fs = std::filesystem;
            const fs::path sd = dir / "scripts";
            if (!L_ || !fs::exists(sd))
                return;
            std::vector<fs::path> files;
            for (const auto &f : fs::directory_iterator(sd))
                if (f.is_regular_file() && f.path().extension() == ".lua")
                    files.push_back(f.path());
            std::sort(files.begin(), files.end());
            curMod_ = modId;
            for (const fs::path &f : files)
            {
                if (luaL_loadfilex(L_, f.string().c_str(), "t") != LUA_OK) // "t" = text only, no bytecode
                {
                    fail(f.string());
                    continue;
                }
                call(0, f.string());
            }
            curMod_.clear();
        }

        // Every registered hook table and the functions it defines, for validating after all scripts have loaded.
        // (Not const: it walks the Lua tables.)
        std::vector<HookInfo> hooks()
        {
            std::vector<HookInfo> out;
            if (!L_)
                return out;
            const std::pair<char, std::map<std::string, Hook> *> tables[] = {{'N', &npcHooks_}, {'A', &areaHooks_}, {'I', &itemHooks_}, {'G', &gameHooks_}, {'S', &sceneHooks_}};
            for (const auto &[kind, table] : tables)
                for (const auto &[id, hook] : *table)
                {
                    HookInfo info;
                    info.kind = kind;
                    info.id = id;
                    info.mod = hook.mod;
                    info.where = hook.where;
                    const std::vector<std::string> names = hookNames(kind);
                    for (size_t i = 0; i < names.size(); ++i)
                        info.valid += (i ? ", " : "") + names[i];
                    lua_rawgeti(L_, LUA_REGISTRYINDEX, hook.ref);
                    lua_pushnil(L_);
                    while (lua_next(L_, -2)) // stack: table, key, value
                    {
                        if (lua_type(L_, -2) == LUA_TSTRING && lua_isfunction(L_, -1))
                        {
                            HookInfo::Fn fn;
                            fn.name = lua_tostring(L_, -2);
                            fn.known = std::find(names.begin(), names.end(), fn.name) != names.end();
                            lua_Debug ar;
                            lua_pushvalue(L_, -1);
                            lua_getinfo(L_, ">S", &ar); // works without the debug library; pops the copy
                            fn.where = std::string(ar.short_src) + ":" + std::to_string(ar.linedefined);
                            info.fns.push_back(std::move(fn));
                        }
                        lua_pop(L_, 1); // the value; the key stays for lua_next
                    }
                    lua_pop(L_, 1); // the hook table
                    std::sort(info.fns.begin(), info.fns.end(), [](const HookInfo::Fn &a, const HookInfo::Fn &b)
                              { return a.name < b.name; });
                    out.push_back(std::move(info));
                }
            return out;
        }

        // The stat names scripts may use (public API: renaming one is a game_api major bump).
        static const std::vector<std::string> &careStatNames()
        {
            static const std::vector<std::string> n{"hunger", "health", "happiness", "tiredness", "cleanliness"};
            return n;
        }
        static const std::vector<std::string> &personalityStatNames()
        {
            static const std::vector<std::string> n{"halos", "pitchforks", "stars", "lightbulbs"};
            return n;
        }

        // The illness ids scripts use. The position in the list IS the ROM's illness number (0..9), which is what the
        // ScriptApi callbacks carry; the names are public API (appending is a minor bump, renaming a major one).
        static const std::vector<std::string> &illnessNames()
        {
            static const std::vector<std::string> n{"dog_breath", "gone_bananas", "panda_pox", "cold",    "molting",
                                                    "brain_freeze", "cold_feet",  "brain_ache", "oogies", "cooties"};
            return n;
        }

        // The hook function names the game fires, per kind ('N' = Npc.hooks, 'A' = Area.hooks, 'I' = Item.hooks,
        // 'G' = Game.hooks).
        static std::vector<std::string> hookNames(char kind)
        {
            switch (kind)
            {
            case 'N':
                return {"onTalkStart", "onTalk"};
            case 'I':
                return {"onItemUse"};
            case 'S':
                return {"onOpen", "onTick", "onDraw"};
            case 'G':
                return {"onQuestComplete", "onEnterArea", "onLoad", "onSave", "onBeforeDig", "onAfterDig", "onBeforePickup", "onAfterPickup",
                        "onBeforeBuy", "onAfterBuy", "onBeforeExit", "onStatChange", "onBefriend", "onQuestAccept", "onQuestDecline", "onBeforeItemUse", "onSceneClose"};
            default:
                return {"onInteract", "onEnter", "onLeave", "onTick"};
            }
        }

        // For tests: run a snippet as if it were a mod script.
        bool runString(const std::string &modId, const std::string &code)
        {
            curMod_ = modId;
            bool ok = luaL_loadbufferx(L_, code.data(), code.size(), "=string", "t") == LUA_OK;
            if (!ok)
                fail("string");
            else
                ok = call(0, "string");
            curMod_.clear();
            return ok;
        }

        // Fires Npc.hooks(id).onTalk (the player pressed OK on the NPC's line). Returns true if a script ran.
        // The return value of the script function is ignored. A script that errors is switched off for the rest of
        // the session and the error is logged.
        bool fireNpcTalk(const std::string &npcId, int x, int y)
        {
            return runNpcHook(npcId, "onTalk", x, y, nullptr);
        }

        // Fires Npc.hooks(id).onTalkStart: the player pressed Z on the NPC, before anything else happens.
        // `return false` from the script sets suppress: the game then skips its default handling of this talk
        // (quest, hand-in, dialogue, befriending). Any other result, no hook, or a script error: the game carries on.
        HookResult fireNpcTalkStart(const std::string &npcId, int x, int y)
        {
            HookResult r;
            r.ran = runNpcHook(npcId, "onTalkStart", x, y, &r.suppress);
            return r;
        }

        // Fires Area.hooks(areaId).onInteract: the player pressed Z near an object in that area, before the game's
        // own handling (the object's interaction, its sign text). `return false` takes over: the default is skipped.
        // ctx.area = { id }, ctx.object = { id }, ctx.player. Any other result, no hook or an error: the game carries on.
        HookResult fireAreaInteract(const std::string &areaId, const std::string &objectId)
        {
            HookResult r;
            r.ran = runHook(areaHooks_, 'A', areaId, "onInteract", [&]
                            {
                                lua_newtable(L_); // ctx.area
                                lua_pushstring(L_, areaId.c_str());
                                lua_setfield(L_, -2, "id");
                                lua_setfield(L_, -2, "area");
                                lua_newtable(L_); // ctx.object
                                lua_pushstring(L_, objectId.c_str());
                                lua_setfield(L_, -2, "id");
                                lua_setfield(L_, -2, "object"); },
                            &r.suppress);
            return r;
        }

        // Fires Area.hooks(areaId).onEnter: the pet has arrived in the area (also when the game starts there). ctx.area = { id },
        // ctx.from = { id } (the area it came from; ctx.from.id is nil when the game just started). The result is ignored.
        // Also fires Game.hooks().onEnterArea for every area, with the same ctx. Returns true if any script ran.
        bool fireAreaEnter(const std::string &areaId, const std::string &fromId)
        {
            auto fill = [&]
            {
                pushIdRef("area", areaId);
                pushIdRef("from", fromId);
            };
            const bool a = runHook(areaHooks_, 'A', areaId, "onEnter", fill, nullptr);
            const bool g = runHook(gameHooks_, 'G', "", "onEnterArea", fill, nullptr);
            return a || g;
        }

        // ---- seams: Game.hooks() functions around things the engine does. "Before" ones run first and `return false` (or waiting) cancels the
        // action; "after" ones run once it has happened and tell the script how it went. Both get ctx.player and ctx.area = { id }. ----
        // onBeforeDig(ctx): the pet is about to dig (Z on bare ground). ctx.x, ctx.y = where. Cancelled: nothing is dug.
        HookResult fireBeforeDig(const std::string &areaId, int x, int y)
        {
            HookResult r;
            r.ran = runHook(gameHooks_, 'G', "", "onBeforeDig", [&] { seamArea(areaId); seamPos(x, y); }, &r.suppress);
            return r;
        }
        // onAfterDig(ctx): the dig is over. ctx.x, ctx.y; ctx.result = "item" (something turned up), "dud" (a spot with nothing) or "miss";
        // ctx.item = { id } for "item" (the item that is now lying there to pick up).
        bool fireAfterDig(const std::string &areaId, int x, int y, const char *result, const std::string &itemId)
        {
            return runHook(gameHooks_, 'G', "", "onAfterDig", [&]
                           {
                               seamArea(areaId);
                               seamPos(x, y);
                               lua_pushstring(L_, result);
                               lua_setfield(L_, -2, "result");
                               if (!itemId.empty())
                                   pushIdRef("item", itemId); },
                           nullptr);
        }
        // onBeforePickup(ctx): the pet pressed Z on something lying on the ground. ctx.item = { id }, ctx.x, ctx.y. Cancelled: it stays.
        HookResult fireBeforePickup(const std::string &areaId, const std::string &itemId, int x, int y)
        {
            HookResult r;
            r.ran = runHook(gameHooks_, 'G', "", "onBeforePickup", [&] { seamArea(areaId); seamPos(x, y); pushIdRef("item", itemId); }, &r.suppress);
            return r;
        }
        // onAfterPickup(ctx): the item went into the bag. ctx.item = { id }, ctx.x, ctx.y.
        bool fireAfterPickup(const std::string &areaId, const std::string &itemId, int x, int y)
        {
            return runHook(gameHooks_, 'G', "", "onAfterPickup", [&] { seamArea(areaId); seamPos(x, y); pushIdRef("item", itemId); }, nullptr);
        }
        // onBeforeBuy(ctx): the pet pressed Z on a shop item it could buy. ctx.item = { id }, ctx.price (each). Cancelled: nothing happens.
        HookResult fireBeforeBuy(const std::string &areaId, const std::string &itemId, int price)
        {
            HookResult r;
            r.ran = runHook(gameHooks_, 'G', "", "onBeforeBuy", [&] { seamArea(areaId); pushIdRef("item", itemId); lua_pushinteger(L_, price); lua_setfield(L_, -2, "price"); }, &r.suppress);
            return r;
        }
        // onAfterBuy(ctx): the purchase went through. ctx.item = { id }, ctx.price (each), ctx.quantity.
        bool fireAfterBuy(const std::string &areaId, const std::string &itemId, int price, int quantity)
        {
            return runHook(gameHooks_, 'G', "", "onAfterBuy", [&]
                           {
                               seamArea(areaId);
                               pushIdRef("item", itemId);
                               lua_pushinteger(L_, price);
                               lua_setfield(L_, -2, "price");
                               lua_pushinteger(L_, quantity);
                               lua_setfield(L_, -2, "quantity"); },
                           nullptr);
        }
        // onStatChange(ctx): money, a personality stat or a care stat is about to change. ctx.kind = "money" | "personality" | "care", ctx.stat =
        // the stat's name as in player.care / player.personality (nil for money and for a stat that has no name), ctx.delta = the change, ctx.value = what it is now. Return a number to change by that instead,
        // `false` to cancel the change; anything else leaves it. It cannot wait. Changes the hook makes itself (player.adjust...) are
        // not passed through it again. Returns the delta to apply.
        int fireStatChange(int kind, const std::string &name, int delta, int value)
        {
            if (inStat_ || !L_)
                return delta;
            haveNumber_ = false;
            bool cancel = false;
            inStat_ = true;
            const bool ran = runHook(gameHooks_, 'G', "", "onStatChange", [&]
                                     {
                                         lua_pushstring(L_, kind == 0 ? "care" : kind == 1 ? "personality" : "money");
                                         lua_setfield(L_, -2, "kind");
                                         if (!name.empty())
                                         {
                                             lua_pushstring(L_, name.c_str());
                                             lua_setfield(L_, -2, "stat");
                                         }
                                         lua_pushinteger(L_, delta);
                                         lua_setfield(L_, -2, "delta");
                                         lua_pushinteger(L_, value);
                                         lua_setfield(L_, -2, "value"); },
                                     &cancel);
            inStat_ = false;
            if (!ran)
                return delta;
            if (cancel)
                return 0;
            if (haveNumber_ && number_ == number_)
                return int(std::max(-1000000.0, std::min(1000000.0, std::floor(number_ + 0.5))));
            return delta;
        }
        // onBefriend(ctx): an NPC has just become the pet's friend (or greeted it again: ctx.again). ctx.npc = { id }. Result ignored.
        bool fireBefriend(const std::string &npcId, bool again)
        {
            return runHook(gameHooks_, 'G', "", "onBefriend", [&]
                           {
                               pushNpcRef("npc", npcId);
                               lua_pushboolean(L_, again);
                               lua_setfield(L_, -2, "again"); },
                           nullptr);
        }
        // onQuestAccept(ctx) / onQuestDecline(ctx): the player said yes / no to a quest. ctx.quest / giver / receiver as in onQuestComplete.
        bool fireQuestAnswer(bool accepted, const QuestCompleteInfo &q)
        {
            return runHook(gameHooks_, 'G', "", accepted ? "onQuestAccept" : "onQuestDecline", [&] { fillQuestCtx(q); }, nullptr);
        }

        // onBeforeItemUse(ctx): the player chose "use" on ANY item, before the item's own Item.hooks(id).onItemUse and the game's handling.
        // ctx.item = { id }. `return false` takes the use over (nothing is eaten or consumed unless the script does it); the item's own hook then
        // does not run.
        HookResult fireBeforeItemUse(const std::string &itemId)
        {
            HookResult r;
            r.ran = runHook(gameHooks_, 'G', "", "onBeforeItemUse", [&] { pushIdRef("item", itemId); }, &r.suppress);
            return r;
        }
        // onBeforeExit(ctx): the pet is about to go through a door. ctx.area = { id } (where it is), ctx.to = { id }, ctx.exit = { id }.
        // Cancelled: the door stays shut (a door the pet walks into asks again only after it has stepped away). Say something with say().
        HookResult fireBeforeExit(const std::string &fromId, const std::string &toId, const std::string &exitId)
        {
            HookResult r;
            r.ran = runHook(gameHooks_, 'G', "", "onBeforeExit", [&] { seamArea(fromId); pushIdRef("to", toId); pushIdRef("exit", exitId); }, &r.suppress);
            return r;
        }

        // ---- scenes: screens a mod draws itself (Scene.hooks(id): onOpen, onTick, onDraw; scene.open / close from any script) ----
        // The game asks for a request every tick the world runs: takeSceneOpen hands over the id of a scene a script asked to open (once),
        // beginScene opens it (while the screen is black), tickScene / drawScene run it, sceneCloseRequested says a script closed it (or it
        // failed) and endScene closes it. All of it is a no-op while no scene is open.
        bool takeSceneOpen(std::string &id)
        {
            if (sceneOpen_ || pendingOpen_.empty())
                return false;
            id = pendingOpen_;
            pendingOpen_.clear();
            return true;
        }
        // The game's own way to ask for a scene (an object whose action is "scene"): false if no script defined it, or one is open / requested.
        bool openScene(const std::string &id)
        {
            if (!sceneHooks_.count(id) || sceneOpen_ || !pendingOpen_.empty())
                return false;
            pendingOpen_ = id;
            argKind_ = 0;
            return true;
        }
        bool sceneKnown(const std::string &id) const { return sceneHooks_.count(id) != 0; }
        bool sceneOpen() const { return sceneOpen_; }
        bool sceneCloseRequested() const { return sceneOpen_ && closeRequested_; }
        void beginScene(const std::string &id)
        {
            sceneOpen_ = true;
            sceneId_ = id;
            sceneTicks_ = 0;
            closeRequested_ = false;
            sceneCmds_.clear();
            runHook(sceneHooks_, 'S', id, "onOpen", [&] { pushSceneArg(); }, nullptr);
            checkSceneFailed("onOpen");
        }
        bool tickScene(const SceneInput &in)
        {
            if (!sceneOpen_ || closeRequested_)
                return false;
            ++sceneTicks_;
            if (routineActive(hookKey('S', sceneId_, "onTick"))) // the last onTick is still waiting: no pile-up
                return false;
            const bool ran = runHook(sceneHooks_, 'S', sceneId_, "onTick", [&]
                                     {
                                         lua_newtable(L_); // ctx.input
                                         const std::pair<const char *, bool> keys[] = {{"z", in.z}, {"x", in.x}, {"left", in.left}, {"right", in.right}, {"up", in.up}, {"down", in.down}};
                                         for (const auto &[k, v] : keys)
                                         {
                                             lua_pushboolean(L_, v);
                                             lua_setfield(L_, -2, k);
                                         }
                                         lua_setfield(L_, -2, "input");
                                         lua_newtable(L_); // ctx.held: the same keys, true while they are held down (for steering)
                                         const std::pair<const char *, bool> held[] = {{"z", in.heldZ}, {"x", in.heldX}, {"left", in.heldLeft}, {"right", in.heldRight}, {"up", in.heldUp}, {"down", in.heldDown}};
                                         for (const auto &[k, v] : held)
                                         {
                                             lua_pushboolean(L_, v);
                                             lua_setfield(L_, -2, k);
                                         }
                                         lua_setfield(L_, -2, "held");
                                         lua_pushinteger(L_, sceneTicks_);
                                         lua_setfield(L_, -2, "tick");
                                         lua_pushnumber(L_, 1.0 / 30.0);
                                         lua_setfield(L_, -2, "dt"); },
                                     nullptr);
            checkSceneFailed("onTick");
            return ran;
        }
        // Runs onDraw and returns what it drew (empty if there is no onDraw, or it failed: a failed scene closes itself).
        const std::vector<DrawCmd> &drawScene()
        {
            sceneCmds_.clear();
            if (!sceneOpen_ || closeRequested_)
                return sceneCmds_;
            drawing_ = true;
            runHook(sceneHooks_, 'S', sceneId_, "onDraw", [&]
                    {
                        const std::pair<const char *, lua_CFunction> fns[] = {{"clear", l_gClear}, {"rect", l_gRect}, {"sprite", l_gSprite}, {"text", l_gText}, {"num", l_gNum}};
                        for (const auto &[name, f] : fns)
                        {
                            lua_pushlightuserdata(L_, this);
                            lua_pushcclosure(L_, f, 1);
                            lua_setfield(L_, -2, name);
                        } },
                    nullptr);
            drawing_ = false;
            checkSceneFailed("onDraw");
            return sceneCmds_;
        }
        // Closes the scene and fires Game.hooks().onSceneClose (ctx.scene = { id }, ctx.result = what scene.close was given).
        void endScene()
        {
            if (!sceneOpen_)
                return;
            const std::string id = sceneId_;
            runHook(gameHooks_, 'G', "", "onSceneClose", [&]
                    {
                        pushIdRef("scene", id);
                        switch (resultKind_)
                        {
                        case 1:
                            pushNumber(resultNum_);
                            lua_setfield(L_, -2, "result");
                            break;
                        case 2:
                            lua_pushlstring(L_, resultStr_.data(), resultStr_.size());
                            lua_setfield(L_, -2, "result");
                            break;
                        case 3:
                            lua_pushboolean(L_, resultNum_ != 0);
                            lua_setfield(L_, -2, "result");
                            break;
                        default:
                            break;
                        } },
                    nullptr);
            sceneOpen_ = false;
            sceneId_.clear();
            closeRequested_ = false;
            sceneCmds_.clear();
            resultKind_ = 0;
        }

        // Fires Game.hooks().onLoad: a game has just started from a slot (a new one or a loaded one) and the mod's save values are
        // back (save.get works). This is where a mod puts its engine.set calls back. The result is ignored.
        bool fireGameLoad()
        {
            return runHook(gameHooks_, 'G', "", "onLoad", [] {}, nullptr);
        }

        // Fires Game.hooks().onSave: the slot is about to be written. save.set calls made here are written with it. Result ignored.
        bool fireGameSave()
        {
            return runHook(gameHooks_, 'G', "", "onSave", [] {}, nullptr);
        }

        // Fires Area.hooks(areaId).onLeave: the pet is going to another area. ctx.area = { id } (the one it leaves), ctx.to = { id }.
        // The result is ignored; the area has not changed yet when this runs.
        bool fireAreaLeave(const std::string &areaId, const std::string &toId)
        {
            return runHook(areaHooks_, 'A', areaId, "onLeave", [&]
                           {
                               pushIdRef("area", areaId);
                               pushIdRef("to", toId); },
                           nullptr);
        }

        // Fires Area.hooks(areaId).onTick once per game tick (30 a second) while the pet is in that area and the world is running.
        // ctx.area = { id }, ctx.dt = seconds per tick (1/30), ctx.tick = ticks since the game started (30 a second, counting
        // the ones skipped while a menu or dialogue was open). If the previous onTick is still
        // waiting (it called wait or ask) no new one starts, so a slow script cannot pile up. Returns true if a script ran.
        bool fireAreaTick(const std::string &areaId, long tick)
        {
            if (areaHooks_.find(areaId) == areaHooks_.end() || routineActive(hookKey('A', areaId, "onTick")))
                return false;
            return runHook(areaHooks_, 'A', areaId, "onTick", [&]
                           {
                               pushIdRef("area", areaId);
                               lua_pushnumber(L_, 1.0 / 30.0);
                               lua_setfield(L_, -2, "dt");
                               lua_pushinteger(L_, tick);
                               lua_setfield(L_, -2, "tick"); },
                           nullptr);
        }

        // Fires Item.hooks(itemId).onItemUse: the player chose "use" on the item in the menu, before the game's own
        // handling (eating, medicine, consuming it). `return false` takes over: the default is skipped entirely, so the
        // script decides whether the item is used up (player.take). ctx.item = { id }, ctx.player. Any other result, no
        // hook or an error: the game carries on.
        HookResult fireItemUse(const std::string &itemId)
        {
            HookResult r;
            r.ran = runHook(itemHooks_, 'I', itemId, "onItemUse", [&]
                            {
                                lua_newtable(L_); // ctx.item
                                lua_pushstring(L_, itemId.c_str());
                                lua_setfield(L_, -2, "id");
                                lua_setfield(L_, -2, "item"); },
                            &r.suppress);
            return r;
        }

        // Fires Game.hooks().onQuestComplete: the receiver took a quest's hand-in (the objectives are already removed),
        // before the completion dialogue. Global: it fires for table quests and generated ones alike (ctx.quest.id is
        // nil for a generated one). `return false` skips the quest's built-in bonus rewards (the thank-you text and the
        // next quest still follow). ctx.quest = { id, number, generic, type }, ctx.giver = { id }, ctx.receiver = { id },
        // ctx.player.
        void fillQuestCtx(const QuestCompleteInfo &q)
        {
            lua_newtable(L_); // ctx.quest
            if (!q.questId.empty())
            {
                lua_pushstring(L_, q.questId.c_str());
                lua_setfield(L_, -2, "id");
            }
            lua_pushinteger(L_, q.number);
            lua_setfield(L_, -2, "number");
            lua_pushboolean(L_, q.generic);
            lua_setfield(L_, -2, "generic");
            if (!q.type.empty())
            {
                lua_pushstring(L_, q.type.c_str());
                lua_setfield(L_, -2, "type");
            }
            lua_setfield(L_, -2, "quest");
            pushNpcRef("giver", q.giverId);
            pushNpcRef("receiver", q.receiverId);
        }
        HookResult fireQuestComplete(const QuestCompleteInfo &q)
        {
            HookResult r;
            r.ran = runHook(gameHooks_, 'G', "", "onQuestComplete", [&] { fillQuestCtx(q); }, &r.suppress);
            return r;
        }

    private:
        struct Mem
        {
            size_t used = 0, limit = 64u << 20;
        };
        struct Hook
        {
            int ref;
            std::string mod;
            std::string where; // "file:line" of the registering call, for validation messages
        };

        // Runs one named hook function from a hook table (npcHooks_ / areaHooks_ / itemHooks_). Hooks are switched off one by one:
        // an error in onTalk does not stop onTalkStart. `fillCtx` adds the hook's own fields to the ctx table on top of
        // the stack. If suppress is given, the function's result is read into it (true = it returned exactly `false`).
        template <class FillCtx>
        bool runHook(std::map<std::string, Hook> &hooks, char kind, const std::string &id, const char *hook,
                     FillCtx fillCtx, bool *suppress)
        {
            auto it = hooks.find(id);
            // \x1f is forbidden in ids, so keys cannot collide; `kind` keeps NPC, area and item ids apart
            const std::string key = hookKey(kind, id, hook);
            if (!L_ || it == hooks.end() || disabled_.count(key))
                return false;
            lua_rawgeti(L_, LUA_REGISTRYINDEX, it->second.ref);
            lua_getfield(L_, -1, hook);
            if (!lua_isfunction(L_, -1))
            {
                lua_pop(L_, 2);
                return false;
            }
            lua_pushvalue(L_, -2); // self = the hook table, so `function T:onTalk(ctx)` works
            lua_newtable(L_);      // ctx
            fillCtx();
            pushPlayerCtx(); // ctx.player
            lua_setfield(L_, -2, "player");
            // The hook runs as a coroutine (stack here: hook table, function, self, ctx), so it can wait for an answer or a timer.
            if (routines_.size() >= kMaxRoutines)
            {
                lua_pop(L_, 4);
                errors_.push_back("[" + it->second.mod + "] " + id + " " + hook + ": too many scripts are waiting");
                if (api_.onError)
                    api_.onError(errors_.back());
                return false;
            }
            lua_State *co = lua_newthread(L_);
            const int tref = luaL_ref(L_, LUA_REGISTRYINDEX); // keeps the thread alive
            lua_xmove(L_, co, 3);                             // the function, self and ctx
            lua_pop(L_, 1);                                   // the hook table
            Routine r;
            r.id = nextRoutine_++;
            r.co = co;
            r.ref = tref;
            r.mod = it->second.mod;
            r.label = id + " " + hook;
            r.key = key;
            routines_.push_back(std::move(r));
            return resume(routines_.back().id, 2, suppress) != Step::Failed;
        }

        enum class Wait
        {
            None,
            Asking,
            Ticks
        };
        enum class Step
        {
            Yielded,
            Finished,
            Failed
        };
        // A hook that is running or waiting. The thread is kept alive by a registry reference until it ends.
        struct Routine
        {
            int id = 0;
            lua_State *co = nullptr;
            int ref = LUA_NOREF;
            std::string mod, label, key;
            Wait wait = Wait::None;
            int ticks = 0;
        };
        static constexpr size_t kMaxRoutines = 8;
        std::vector<Routine> routines_;
        int nextRoutine_ = 1;
        int nextSpawn_ = 1; // for the ids of objects scripts spawn
        bool haveNumber_ = false; // the last hook that was asked for its result returned a number
        double number_ = 0;
        // scene state
        bool sceneOpen_ = false, closeRequested_ = false, drawing_ = false;
        std::string sceneId_, pendingOpen_;
        long sceneTicks_ = 0;
        std::vector<DrawCmd> sceneCmds_;
        int argKind_ = 0, resultKind_ = 0; // 0 none, 1 number, 2 string, 3 boolean
        double argNum_ = 0, resultNum_ = 0;
        std::string argStr_, resultStr_;
        bool inStat_ = false;     // an onStatChange is running: changes its own script makes are not filtered again

        Routine *routineById(int id)
        {
            for (Routine &r : routines_)
                if (r.id == id)
                    return &r;
            return nullptr;
        }
        Routine *routineFor(lua_State *co)
        {
            for (Routine &r : routines_)
                if (r.co == co)
                    return &r;
            return nullptr;
        }

        // Runs a routine until it yields, ends or fails. `suppress` (first run of a hook only) is set when the script
        // returned exactly `false`, or when it started waiting (a script that waits has taken over). An error switches
        // that hook off for the session and is logged; a routine that has ended is released.
        Step resume(int rid, int nargs, bool *suppress)
        {
            Routine *r = routineById(rid);
            if (!r)
                return Step::Failed;
            lua_State *co = r->co;
            const std::string saveMod = curMod_;
            curMod_ = r->mod;
            lua_sethook(co, budgetHook, LUA_MASKCOUNT, 1000000);
            int nres = 0;
            const int st = lua_resume(co, L_, nargs, &nres);
            lua_sethook(co, nullptr, 0, 0);
            curMod_ = saveMod;
            r = routineById(rid);
            if (st == LUA_YIELD)
            {
                if (suppress)
                    *suppress = true;
                return Step::Yielded;
            }
            const bool ok = st == LUA_OK;
            std::string key, label, mod;
            if (r)
            {
                key = r->key;
                label = r->label;
                mod = r->mod;
            }
            if (ok)
            {
                if (suppress)
                {
                    const int first = lua_gettop(co) - nres + 1;
                    *suppress = nres > 0 && lua_isboolean(co, first) && !lua_toboolean(co, first);
                    if (nres > 0 && lua_type(co, first) == LUA_TNUMBER) // a hook that returns a number (onStatChange)
                    {
                        haveNumber_ = true;
                        number_ = lua_tonumber(co, first);
                    }
                }
            }
            else
            {
                const char *msg = lua_tostring(co, -1);
                errors_.push_back("[" + mod + "] " + label + ": " + (msg ? msg : "error"));
                if (api_.onError)
                    api_.onError(errors_.back());
                disabled_.insert(key);
            }
            if (r)
            {
                luaL_unref(L_, LUA_REGISTRYINDEX, r->ref);
                routines_.erase(routines_.begin() + (r - routines_.data()));
            }
            return ok ? Step::Finished : Step::Failed;
        }

        // ctx.npc = { id, x, y }
        bool runNpcHook(const std::string &npcId, const char *hook, int x, int y, bool *suppress)
        {
            return runHook(npcHooks_, 'N', npcId, hook, [&]
                           {
                               lua_newtable(L_); // ctx.npc
                               lua_pushstring(L_, npcId.c_str());
                               lua_setfield(L_, -2, "id");
                               lua_pushinteger(L_, x);
                               lua_setfield(L_, -2, "x");
                               lua_pushinteger(L_, y);
                               lua_setfield(L_, -2, "y");
                               lua_setfield(L_, -2, "npc"); },
                           suppress);
        }

        // The key a hook is switched off by, and by which a running copy of it is recognised.
        static std::string hookKey(char kind, const std::string &id, const char *hook)
        {
            return std::string(1, kind) + '\x1f' + id + '\x1f' + hook;
        }
        bool routineActive(const std::string &key) const
        {
            for (const Routine &r : routines_)
                if (r.key == key)
                    return true;
            return false;
        }
        // ctx.<field> = { id = id }, with id left out (nil) when it is empty. Sets the field on the ctx table on top of the stack.
        void seamArea(const std::string &areaId) { pushIdRef("area", areaId); }
        void seamPos(int x, int y)
        {
            lua_pushinteger(L_, x);
            lua_setfield(L_, -2, "x");
            lua_pushinteger(L_, y);
            lua_setfield(L_, -2, "y");
        }
        void pushIdRef(const char *field, const std::string &id)
        {
            lua_newtable(L_);
            if (!id.empty())
            {
                lua_pushstring(L_, id.c_str());
                lua_setfield(L_, -2, "id");
            }
            lua_setfield(L_, -2, field);
        }

        // ctx.<field> = { id = npcId }, with id left out (nil) when the NPC isn't known. Sets the field on the ctx table
        // on top of the stack.
        void pushNpcRef(const char *field, const std::string &npcId)
        {
            lua_newtable(L_);
            if (!npcId.empty())
            {
                lua_pushstring(L_, npcId.c_str());
                lua_setfield(L_, -2, "id");
            }
            lua_setfield(L_, -2, field);
        }

        // Pushes the ctx.player table: a snapshot, so changing it in a script changes nothing in the game.
        void pushPlayerCtx()
        {
            lua_newtable(L_);
            if (!api_.playerInfo)
                return;
            const ScriptApi::PlayerInfo p = api_.playerInfo();
            lua_pushstring(L_, p.name.c_str());
            lua_setfield(L_, -2, "name");
            lua_pushinteger(L_, p.money);
            lua_setfield(L_, -2, "money");
            lua_pushinteger(L_, p.questsCompleted);
            lua_setfield(L_, -2, "questsCompleted");
        }

        static void *alloc(void *ud, void *ptr, size_t osize, size_t nsize)
        {
            Mem *m = static_cast<Mem *>(ud);
            const size_t old = ptr ? osize : 0; // osize is a type tag when ptr is null
            if (nsize == 0)
            {
                m->used -= old;
                std::free(ptr);
                return nullptr;
            }
            if (m->used - old + nsize > m->limit)
                return nullptr;
            void *p = std::realloc(ptr, nsize);
            if (p)
                m->used = m->used - old + nsize;
            return p;
        }

        static void budgetHook(lua_State *L, lua_Debug *) { luaL_error(L, "script ran too long (instruction limit)"); }

        // Calls the function on the stack with nargs arguments. Pops everything it used; on success the nresults
        // results (0 or 1 here) are left on the stack for the caller, on failure the error is popped and logged.
        bool call(int nargs, const std::string &what, int nresults = 0)
        {
            lua_sethook(L_, budgetHook, LUA_MASKCOUNT, 1000000);
            const int rc = lua_pcall(L_, nargs, nresults, 0);
            lua_sethook(L_, nullptr, 0, 0);
            if (rc != LUA_OK)
            {
                fail(what);
                return false;
            }
            return true;
        }
        void fail(const std::string &what)
        {
            const char *msg = lua_tostring(L_, -1);
            errors_.push_back("[" + curMod_ + "] " + what + ": " + (msg ? msg : "error"));
            lua_pop(L_, 1);
            if (api_.onError)
                api_.onError(errors_.back());
        }

        void open(const char *name, lua_CFunction f)
        {
            luaL_requiref(L_, name, f, 1);
            lua_pop(L_, 1);
        }
        void dropField(const char *table, const char *field)
        {
            lua_getglobal(L_, table);
            lua_pushnil(L_);
            lua_setfield(L_, -2, field);
            lua_pop(L_, 1);
        }
        void bindGlobal(const char *name, lua_CFunction f)
        {
            lua_pushlightuserdata(L_, this);
            lua_pushcclosure(L_, f, 1);
            lua_setglobal(L_, name);
        }
        void bind(const char *table, const char *name, lua_CFunction f)
        {
            lua_getglobal(L_, table);
            if (lua_isnil(L_, -1))
            {
                lua_pop(L_, 1);
                lua_newtable(L_);
                lua_pushvalue(L_, -1);
                lua_setglobal(L_, table);
            }
            lua_pushlightuserdata(L_, this);
            lua_pushcclosure(L_, f, 1);
            lua_setfield(L_, -2, name);
            lua_pop(L_, 1);
        }

        static ScriptHost &self(lua_State *L)
        {
            return *static_cast<ScriptHost *>(lua_touserdata(L, lua_upvalueindex(1)));
        }

        // ---- the script-facing API ----
        static int l_log(lua_State *L)
        {
            ScriptHost &h = self(L);
            std::string s;
            for (int i = 1, n = lua_gettop(L); i <= n; ++i)
            {
                size_t len;
                const char *p = luaL_tolstring(L, i, &len);
                if (i > 1)
                    s += '\t';
                s.append(p, len);
                lua_pop(L, 1);
            }
            std::fprintf(stderr, "[%s] %s\n", h.curMod_.c_str(), s.c_str());
            return 0;
        }
        // Returns (creating it on first use) the hook table for an id: Npc.hooks("testmod:npc/tester"),
        // Area.hooks("base:area/3"), Item.hooks("mymod:item/pie"). Scripts define functions on it: onTalkStart, onTalk,
        // onInteract, onItemUse.
        static int hooksFor(lua_State *L, std::map<std::string, Hook> &hooks)
        {
            return hooksTable(L, hooks, luaL_checkstring(L, 1)); // plain C value: Lua may unwind (longjmp) below
        }
        static int hooksTable(lua_State *L, std::map<std::string, Hook> &hooks, const char *id)
        {
            ScriptHost &h = self(L);
            auto it = hooks.find(id);
            if (it == hooks.end())
            {
                lua_newtable(L);
                lua_pushvalue(L, -1);
                const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
                luaL_where(L, 1); // "file:line: " of the calling script, or "" if unknown
                std::string where = lua_tostring(L, -1);
                lua_pop(L, 1);
                while (!where.empty() && (where.back() == ' ' || where.back() == ':'))
                    where.pop_back();
                hooks.emplace(id, Hook{ref, h.curMod_, std::move(where)});
            }
            else
                lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.ref);
            return 1;
        }
        static int l_npcHooks(lua_State *L) { return hooksFor(L, self(L).npcHooks_); }
        static int l_areaHooks(lua_State *L) { return hooksFor(L, self(L).areaHooks_); }
        static int l_sceneHooks(lua_State *L) { return hooksFor(L, self(L).sceneHooks_); }
        static int l_itemHooks(lua_State *L) { return hooksFor(L, self(L).itemHooks_); }
        // Game.hooks() -> the one global hook table (no id): function G:onQuestComplete(ctx)
        static int l_gameHooks(lua_State *L) { return hooksTable(L, self(L).gameHooks_, ""); }
        // world.drop(itemId, x, y) -> true if the item exists
        static int l_drop(lua_State *L)
        {
            ScriptHost &h = self(L);
            const std::string item = luaL_checkstring(L, 1);
            const int x = int(luaL_checkinteger(L, 2)), y = int(luaL_checkinteger(L, 3));
            lua_pushboolean(L, h.api_.drop && h.api_.drop(item, x, y));
            return 1;
        }
        // rand.int(lo, hi): both ends included
        static int l_randInt(lua_State *L)
        {
            ScriptHost &h = self(L);
            const int lo = int(luaL_checkinteger(L, 1)), hi = int(luaL_checkinteger(L, 2));
            luaL_argcheck(L, lo <= hi, 2, "empty range");
            lua_pushinteger(L, h.api_.rnd ? h.api_.rnd(lo, hi + 1) : lo);
            return 1;
        }
        // rand.pick(list) -> a random element, nil for an empty list
        static int l_randPick(lua_State *L)
        {
            ScriptHost &h = self(L);
            luaL_checktype(L, 1, LUA_TTABLE);
            const lua_Integer n = luaL_len(L, 1);
            if (n == 0 || !h.api_.rnd)
            {
                lua_pushnil(L);
                return 1;
            }
            lua_geti(L, 1, h.api_.rnd(1, int(n) + 1));
            return 1;
        }

        // ---- player.* ----
        // Lua errors unwind with longjmp, so these functions hold only plain C values (const char *, ints) at any
        // point where Lua can raise an error; std::strings are only built inside the callbacks.
        //
        // Flag and item ids are written into saves as comma-separated lists, so they must not contain , = | or
        // whitespace/control characters. (Same rule as the loader's validIdChars.)
        static bool idCharsOk(const char *s)
        {
            const size_t n = std::strlen(s);
            if (n == 0 || n > 200)
                return false;
            for (const char *p = s; *p; ++p)
            {
                const unsigned char c = static_cast<unsigned char>(*p);
                if (c <= ' ' || c == 127 || c == ',' || c == '=' || c == '|')
                    return false;
            }
            return true;
        }
        // optional count argument: an integer 1..9999 (default 1)
        static int countArg(lua_State *L, int idx)
        {
            const lua_Integer n = luaL_optinteger(L, idx, 1);
            luaL_argcheck(L, n >= 1 && n <= 9999, idx, "count must be between 1 and 9999");
            return int(n);
        }
        // player.give(itemId [, n]) -> how many were added (0 for an unknown item or a full stack)
        static int l_pGive(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *item = luaL_checkstring(L, 1);
            const int n = countArg(L, 2);
            lua_pushinteger(L, h.api_.give && idCharsOk(item) ? h.api_.give(item, n) : 0);
            return 1;
        }
        // player.take(itemId [, n]) -> how many were removed (never more than the player holds)
        static int l_pTake(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *item = luaL_checkstring(L, 1);
            const int n = countArg(L, 2);
            lua_pushinteger(L, h.api_.take && idCharsOk(item) ? h.api_.take(item, n) : 0);
            return 1;
        }
        // player.count(itemId) -> how many the player holds
        static int l_pCount(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *item = luaL_checkstring(L, 1);
            lua_pushinteger(L, h.api_.count && idCharsOk(item) ? h.api_.count(item) : 0);
            return 1;
        }
        // player.hasFlag(flagId) -> boolean. Any flag can be read (including another mod's, or a base flag).
        static int l_pHasFlag(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *flag = luaL_checkstring(L, 1);
            lua_pushboolean(L, h.api_.hasFlag && idCharsOk(flag) && h.api_.hasFlag(flag));
            return 1;
        }
        // player.setFlag(flagId). A script may only set flags under its own mod prefix ("mymod:...").
        // A wrong prefix or bad characters is a script error (loud on purpose: it is always a mistake).
        static int l_pSetFlag(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *flag = luaL_checkstring(L, 1);
            const std::string &mod = h.curMod_;
            const bool prefixOk = !mod.empty() && std::strlen(flag) > mod.size() + 1 &&
                                  std::strncmp(flag, mod.c_str(), mod.size()) == 0 && flag[mod.size()] == ':';
            if (!prefixOk)
                return luaL_argerror(L, 1, lua_pushfstring(L, "flag ids must start with \"%s:\" (the mod's own prefix)", mod.c_str()));
            if (!idCharsOk(flag))
                return luaL_argerror(L, 1, "flag id has a forbidden character (, = | or whitespace)");
            if (h.api_.setFlag)
                h.api_.setFlag(flag);
            return 0;
        }

        // ---- engine values (the registry) ----
        // Lua errors unwind with longjmp, so each of these keeps its std::string / optional objects in an inner scope and raises
        // the error only after it closed.
        static void pushEngineValue(lua_State *L, const EngineValue &v)
        {
            switch (v.kind)
            {
            case EngineValue::Kind::Int:
                lua_pushinteger(L, static_cast<lua_Integer>(v.i));
                break;
            case EngineValue::Kind::Real:
                lua_pushnumber(L, v.r);
                break;
            case EngineValue::Kind::Bool:
                lua_pushboolean(L, v.b);
                break;
            case EngineValue::Kind::Str:
                lua_pushlstring(L, v.s.data(), v.s.size());
                break;
            }
        }
        // engine.get(name) -> the value (number, boolean or string). A name the game does not have is a script error.
        static int l_eGet(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *name = luaL_checkstring(L, 1);
            char msg[160] = "";
            {
                if (!h.registry_)
                    std::snprintf(msg, sizeof msg, "engine values are not available");
                else if (const std::optional<EngineValue> v = h.registry_->get(name))
                    pushEngineValue(L, *v);
                else
                    std::snprintf(msg, sizeof msg, "no engine value '%.60s' (engine.list() shows the names)", name);
            }
            if (msg[0])
                return luaL_error(L, "engine.get: %s", msg);
            return 1;
        }
        // engine.set(name, value). Anything the game exposes can be set. A wrong kind of value, or a number the variable cannot hold,
        // is a script error (the value stays as it was). A mod that wants it kept puts it in the save itself (save.set, Game onLoad).
        static int l_eSet(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *name = luaL_checkstring(L, 1);
            const int t = lua_type(L, 2);
            bool isInt = false, bv = false;
            lua_Integer iv = 0;
            lua_Number rv = 0;
            const char *sv = nullptr;
            size_t slen = 0;
            if (t == LUA_TNUMBER)
            {
                isInt = lua_isinteger(L, 2) != 0;
                if (isInt)
                    iv = lua_tointeger(L, 2);
                else
                    rv = lua_tonumber(L, 2);
            }
            else if (t == LUA_TBOOLEAN)
                bv = lua_toboolean(L, 2) != 0;
            else if (t == LUA_TSTRING)
                sv = lua_tolstring(L, 2, &slen);
            else
                return luaL_argerror(L, 2, "expects a number, a boolean or a string");
            char msg[320] = "";
            {
                if (!h.registry_)
                    std::snprintf(msg, sizeof msg, "engine values are not available");
                else
                {
                    EngineValue v = t == LUA_TNUMBER ? (isInt ? EngineValue::ofInt(iv) : EngineValue::ofReal(rv))
                                    : t == LUA_TBOOLEAN ? EngineValue::ofBool(bv)
                                                        : EngineValue::ofStr(std::string(sv, slen));
                    const std::string why = h.registry_->set(name, v);
                    std::snprintf(msg, sizeof msg, "%.300s", why.c_str());
                }
            }
            if (msg[0])
                return luaL_error(L, "engine.set: %s", msg);
            return 0;
        }
        // engine.has(name) -> boolean
        static int l_eHas(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *name = luaL_checkstring(L, 1);
            bool has = false;
            {
                has = h.registry_ && h.registry_->has(name);
            }
            lua_pushboolean(L, has);
            return 1;
        }
        // engine.list([prefix]) -> sorted array of names, e.g. engine.list("pet.")
        static int l_eList(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *prefix = luaL_optstring(L, 1, "");
            lua_newtable(L);
            if (!h.registry_)
                return 1;
            const std::vector<std::string> names = h.registry_->names(prefix);
            for (size_t i = 0; i < names.size(); ++i)
            {
                lua_pushlstring(L, names[i].data(), names[i].size());
                lua_rawseti(L, -2, lua_Integer(i + 1));
            }
            return 1;
        }
        // engine.describe(name) -> kind ("int", "number", "bool" or "string"), note; nil for an unknown name.
        static int l_eDescribe(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *name = luaL_checkstring(L, 1);
            if (!h.registry_ || !h.registry_->has(name))
            {
                lua_pushnil(L);
                return 1;
            }
            const std::string kind = h.registry_->kindOf(name), note = h.registry_->noteOf(name);
            lua_pushlstring(L, kind.data(), kind.size());
            lua_pushlstring(L, note.data(), note.size());
            return 2;
        }

        // ---- what a mod keeps in the save ----
        // save.get(key [, default]) -> the stored number, boolean or string, or `default` (nil) if there is none.
        // save.set(key, value) stores a number, boolean or string (nil removes the key). The values live in the slot that is
        // played, are written whenever the game saves, and come back before Game onLoad. Each mod has its own keys.
        static bool saveKeyOk(const char *k)
        {
            const size_t n = std::strlen(k);
            return n >= 1 && n <= 64 && std::strspn(k, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") == n;
        }
        static int l_sGet(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *key = luaL_checkstring(L, 1);
            lua_settop(L, 2); // an absent default is nil
            if (h.curMod_.empty())
                return luaL_error(L, "save.get: only a mod's scripts can use the save");
            luaL_argcheck(L, saveKeyOk(key), 1, "a save key is 1 to 64 letters, digits, _ . or -");
            bool found = false;
            {
                const auto it = h.modSave_.find(h.curMod_ + "." + key);
                if (it != h.modSave_.end() && !it->second.empty())
                {
                    const std::string &v = it->second;
                    found = true;
                    switch (v[0])
                    {
                    case 'i':
                        lua_pushinteger(L, static_cast<lua_Integer>(std::strtoll(v.c_str() + 1, nullptr, 10)));
                        break;
                    case 'n':
                        lua_pushnumber(L, std::strtod(v.c_str() + 1, nullptr));
                        break;
                    case 'b':
                        lua_pushboolean(L, v.size() > 1 && v[1] == '1');
                        break;
                    case 's':
                        lua_pushlstring(L, v.data() + 1, v.size() - 1);
                        break;
                    default:
                        found = false;
                    }
                }
            }
            if (!found)
                lua_pushvalue(L, 2); // the default, or nil when there is none
            return 1;
        }
        static int l_sSet(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *key = luaL_checkstring(L, 1);
            if (h.curMod_.empty())
                return luaL_error(L, "save.set: only a mod's scripts can use the save");
            luaL_argcheck(L, saveKeyOk(key), 1, "a save key is 1 to 64 letters, digits, _ . or -");
            const int t = lua_type(L, 2);
            char text[1100] = "";
            if (t == LUA_TNIL || t == LUA_TNONE)
                text[0] = 0;
            else if (t == LUA_TBOOLEAN)
                std::snprintf(text, sizeof text, "b%d", lua_toboolean(L, 2) ? 1 : 0);
            else if (t == LUA_TNUMBER)
            {
                if (lua_isinteger(L, 2))
                    std::snprintf(text, sizeof text, "i%lld", static_cast<long long>(lua_tointeger(L, 2)));
                else
                {
                    const lua_Number d = lua_tonumber(L, 2);
                    luaL_argcheck(L, d == d && d - d == 0, 2, "a saved number must be finite");
                    std::snprintf(text, sizeof text, "n%.17g", d);
                }
            }
            else if (t == LUA_TSTRING)
            {
                size_t len = 0;
                const char *s = lua_tolstring(L, 2, &len);
                luaL_argcheck(L, len <= 1000, 2, "a saved string is at most 1000 characters");
                luaL_argcheck(L, std::strpbrk(s, "\r\n") == nullptr && std::strlen(s) == len, 2, "a saved string cannot hold a line break or a zero byte");
                std::snprintf(text, sizeof text, "s%s", s);
            }
            else
                return luaL_argerror(L, 2, "expects a number, a boolean or a string (or nil to remove the key)");
            bool tooMany = false;
            {
                const std::string full = h.curMod_ + "." + key;
                if (!text[0])
                    h.modSave_.erase(full);
                else
                {
                    if (!h.modSave_.count(full))
                    {
                        int mine = 0;
                        const std::string pre = h.curMod_ + ".";
                        for (const auto &kv : h.modSave_)
                            mine += kv.first.compare(0, pre.size(), pre) == 0;
                        tooMany = mine >= 500;
                    }
                    if (!tooMany)
                        h.modSave_[full] = text;
                }
            }
            if (tooMany)
                return luaL_error(L, "save.set: a mod can keep at most 500 keys");
            return 0;
        }
        // save.keys() -> sorted array of this mod's keys
        static int l_sKeys(lua_State *L)
        {
            ScriptHost &h = self(L);
            lua_newtable(L);
            const std::string pre = h.curMod_ + ".";
            lua_Integer n = 0;
            for (const auto &kv : h.modSave_)
                if (!h.curMod_.empty() && kv.first.compare(0, pre.size(), pre) == 0)
                {
                    lua_pushlstring(L, kv.first.data() + pre.size(), kv.first.size() - pre.size());
                    lua_rawseti(L, -2, ++n);
                }
            return 1;
        }

        // sound.play(id [, volume = 255]): a sound effect of the game by its number (0 to 511; 0x59 is the menu click, 0x15 a match chime, 0x53 a pickup).
        static int l_soundPlay(lua_State *L)
        {
            ScriptHost &h = self(L);
            const int id = int(luaL_checkinteger(L, 1)), vol = int(luaL_optinteger(L, 2, 255));
            luaL_argcheck(L, id >= 0 && id <= 511, 1, "a sound number from 0 to 511");
            luaL_argcheck(L, vol >= 0 && vol <= 255, 2, "a volume from 0 to 255");
            if (h.api_.playSound)
                h.api_.playSound(id, vol);
            return 0;
        }
        // ---- scenes: the Lua side ----
        // scene.open(id [, arg]) -> true if the scene will open (it does so when the world next lets it: not in a dialogue or a menu). `arg` (a
        // number, string or boolean) reaches onOpen as ctx.arg. An id nobody defined with Scene.hooks(id) is a script error; opening while
        // one is open or already requested is refused (false).
        static int l_scOpen(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            const int t = lua_type(L, 2);
            if (!(t == LUA_TNONE || t == LUA_TNIL || t == LUA_TNUMBER || t == LUA_TSTRING || t == LUA_TBOOLEAN))
                return luaL_argerror(L, 2, "the argument is a number, a string or a boolean");
            double num = 0;
            size_t slen = 0;
            const char *str = nullptr;
            if (t == LUA_TNUMBER)
                num = lua_tonumber(L, 2);
            else if (t == LUA_TBOOLEAN)
                num = lua_toboolean(L, 2);
            else if (t == LUA_TSTRING)
                str = lua_tolstring(L, 2, &slen);
            bool known = false, accepted = false;
            {
                known = h.sceneHooks_.count(id) != 0;
                if (known && !h.sceneOpen_ && h.pendingOpen_.empty())
                {
                    h.pendingOpen_ = id;
                    h.argKind_ = t == LUA_TNUMBER ? 1 : t == LUA_TSTRING ? 2 : t == LUA_TBOOLEAN ? 3 : 0;
                    h.argNum_ = num;
                    h.argStr_ = str ? std::string(str, slen) : std::string();
                    accepted = true;
                }
            }
            if (!known)
                return luaL_error(L, "scene.open: no scene '%s' (define it with Scene.hooks(\"%s\"))", id, id);
            lua_pushboolean(L, accepted);
            return 1;
        }
        // scene.close([result]) -> true if a scene was open. The scene closes at the end of this tick (the screen fades); `result` (a number,
        // string or boolean) reaches Game.hooks():onSceneClose as ctx.result.
        static int l_scClose(lua_State *L)
        {
            ScriptHost &h = self(L);
            const int t = lua_type(L, 1);
            if (!(t == LUA_TNONE || t == LUA_TNIL || t == LUA_TNUMBER || t == LUA_TSTRING || t == LUA_TBOOLEAN))
                return luaL_argerror(L, 1, "the result is a number, a string or a boolean");
            if (!h.sceneOpen_ || h.closeRequested_)
            {
                lua_pushboolean(L, 0);
                return 1;
            }
            h.resultKind_ = t == LUA_TNUMBER ? 1 : t == LUA_TSTRING ? 2 : t == LUA_TBOOLEAN ? 3 : 0;
            if (t == LUA_TNUMBER)
                h.resultNum_ = lua_tonumber(L, 1);
            else if (t == LUA_TBOOLEAN)
                h.resultNum_ = lua_toboolean(L, 1);
            else if (t == LUA_TSTRING)
            {
                size_t n = 0;
                const char *p = lua_tolstring(L, 1, &n);
                h.resultStr_.assign(p, n);
            }
            h.closeRequested_ = true;
            lua_pushboolean(L, 1);
            return 1;
        }
        static int l_scIsOpen(lua_State *L)
        {
            lua_pushboolean(L, self(L).sceneOpen_);
            return 1;
        }
        // a whole number reaches Lua as an integer (so tostring(2) is "2", not "2.0")
        void pushNumber(double v)
        {
            if (v == std::floor(v) && std::fabs(v) < 9e15)
                lua_pushinteger(L_, static_cast<lua_Integer>(v));
            else
                lua_pushnumber(L_, v);
        }
        void pushSceneArg()
        {
            switch (argKind_)
            {
            case 1:
                pushNumber(argNum_);
                lua_setfield(L_, -2, "arg");
                break;
            case 2:
                lua_pushlstring(L_, argStr_.data(), argStr_.size());
                lua_setfield(L_, -2, "arg");
                break;
            case 3:
                lua_pushboolean(L_, argNum_ != 0);
                lua_setfield(L_, -2, "arg");
                break;
            default:
                break;
            }
        }
        // A scene whose hook has failed (an error switches it off for the session) closes itself instead of showing a frozen screen.
        void checkSceneFailed(const char *hook)
        {
            if (sceneOpen_ && disabled_.count(hookKey('S', sceneId_, hook)))
                closeRequested_ = true;
        }

        // ---- the draw handle (the ctx table of onDraw) ----
        static constexpr size_t kMaxDrawCmds = 2000;
        // A colour is { r, g, b [, a] }, each 0 to 255.
        static bool colourArg(lua_State *L, int idx, int out[4])
        {
            luaL_checktype(L, idx, LUA_TTABLE);
            out[3] = 255;
            for (int i = 0; i < 4; ++i)
            {
                lua_rawgeti(L, idx, i + 1);
                if (lua_isnil(L, -1))
                {
                    lua_pop(L, 1);
                    if (i < 3)
                        luaL_argerror(L, idx, "a colour is { r, g, b } or { r, g, b, a }");
                    break;
                }
                if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 0 || lua_tointeger(L, -1) > 255)
                    luaL_argerror(L, idx, "colour values are whole numbers from 0 to 255");
                out[i] = int(lua_tointeger(L, -1));
                lua_pop(L, 1);
            }
            return true;
        }
        static void drawGuard(lua_State *L)
        {
            ScriptHost &h = self(L);
            if (!h.drawing_)
                luaL_error(L, "draw calls only work inside onDraw, while it runs");
            if (h.sceneCmds_.size() >= kMaxDrawCmds)
                luaL_error(L, "too many draw calls in one frame (at most %d)", int(kMaxDrawCmds));
        }
        // g.clear(color): fills the whole screen
        static int l_gClear(lua_State *L)
        {
            drawGuard(L);
            int c[4];
            colourArg(L, 1, c);
            DrawCmd d;
            d.kind = DrawCmd::Kind::Clear;
            d.r = c[0];
            d.g = c[1];
            d.b = c[2];
            d.a = c[3];
            self(L).sceneCmds_.push_back(std::move(d));
            return 0;
        }
        // g.rect(x, y, w, h, color [, fill = true])
        static int l_gRect(lua_State *L)
        {
            drawGuard(L);
            const int x = int(luaL_checkinteger(L, 1)), y = int(luaL_checkinteger(L, 2)), w = int(luaL_checkinteger(L, 3)), hh = int(luaL_checkinteger(L, 4));
            int c[4];
            colourArg(L, 5, c);
            const bool fill = lua_isnoneornil(L, 6) || lua_toboolean(L, 6);
            DrawCmd d;
            d.kind = DrawCmd::Kind::Rect;
            d.x = x;
            d.y = y;
            d.w = w;
            d.h = hh;
            d.r = c[0];
            d.g = c[1];
            d.b = c[2];
            d.a = c[3];
            d.fill = fill;
            self(L).sceneCmds_.push_back(std::move(d));
            return 0;
        }
        // g.sprite(id, x, y [, { frame = 0, flip = false }]): a picture of a mod (an anim record's id) or of the game, with its anchor at x, y
        static int l_gSprite(lua_State *L)
        {
            drawGuard(L);
            const char *id = luaL_checkstring(L, 1);
            const int x = int(luaL_checkinteger(L, 2)), y = int(luaL_checkinteger(L, 3));
            int frame = 0;
            bool flip = false;
            if (!lua_isnoneornil(L, 4))
            {
                luaL_checktype(L, 4, LUA_TTABLE);
                lua_getfield(L, 4, "frame");
                if (!lua_isnil(L, -1))
                    frame = int(luaL_checkinteger(L, -1));
                lua_pop(L, 1);
                lua_getfield(L, 4, "flip");
                flip = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
            }
            luaL_argcheck(L, idCharsOk(id) && frame >= 0 && frame < 10000, 1, "a picture id (and a frame from 0)");
            DrawCmd d;
            d.kind = DrawCmd::Kind::Sprite;
            d.str = id;
            d.x = x;
            d.y = y;
            d.frame = frame;
            d.flip = flip;
            self(L).sceneCmds_.push_back(std::move(d));
            return 0;
        }
        // The options of g.text / g.num: { align = "left" | "center" | "right", width = n, font = 0 | 3 } and, for g.text, vars = {...}.
        // Reads them into plain values; `varsIdx` is the stack index of the vars table (0 = none), left on the stack for readVars.
        static void textOpts(lua_State *L, int idx, bool withVars, int &align, int &width, int &font, int &varsIdx, int &nvars)
        {
            align = width = font = varsIdx = nvars = 0;
            if (lua_isnoneornil(L, idx))
                return;
            luaL_checktype(L, idx, LUA_TTABLE);
            lua_getfield(L, idx, "align");
            if (lua_isstring(L, -1))
            {
                const char *a = lua_tostring(L, -1);
                if (!std::strcmp(a, "center") || !std::strcmp(a, "centre"))
                    align = 1;
                else if (!std::strcmp(a, "right"))
                    align = 2;
                else if (std::strcmp(a, "left"))
                    luaL_argerror(L, idx, "align is \"left\", \"center\" or \"right\"");
            }
            lua_pop(L, 1);
            lua_getfield(L, idx, "width");
            if (!lua_isnil(L, -1))
                width = int(luaL_checkinteger(L, -1));
            lua_pop(L, 1);
            lua_getfield(L, idx, "font");
            if (!lua_isnil(L, -1))
                font = int(luaL_checkinteger(L, -1));
            lua_pop(L, 1);
            luaL_argcheck(L, (font == 0 || font == 3) && width >= 0 && width <= 1000, idx, "font is 0 (text) or 3 (title); width 0 to 1000");
            if (withVars)
            {
                lua_getfield(L, idx, "vars");
                if (lua_isnil(L, -1))
                    lua_pop(L, 1);
                else
                {
                    varsIdx = lua_gettop(L);
                    nvars = checkVars(L, varsIdx, "g.text");
                }
            }
        }
        // g.text(textId, x, y [, opts]): a text of the mod (or the game) by its id, with {name} filled from opts.vars and "%s" the pet's name
        static int l_gText(lua_State *L)
        {
            drawGuard(L);
            const char *id = luaL_checkstring(L, 1);
            const int x = int(luaL_checkinteger(L, 2)), y = int(luaL_checkinteger(L, 3));
            int align, width, font, varsIdx, nvars;
            textOpts(L, 4, true, align, width, font, varsIdx, nvars);
            luaL_argcheck(L, idCharsOk(id), 1, "a text id");
            {
                DrawCmd d;
                d.kind = DrawCmd::Kind::Text;
                d.str = id;
                d.x = x;
                d.y = y;
                d.align = align;
                d.width = width;
                d.font = font;
                if (nvars > 0)
                    d.vars = readVars(L, varsIdx, nvars);
                self(L).sceneCmds_.push_back(std::move(d));
            }
            return 0;
        }
        // g.num(n, x, y [, opts]): a number (a score, a timer) in the dialogue font
        static int l_gNum(lua_State *L)
        {
            drawGuard(L);
            const lua_Number n = luaL_checknumber(L, 1);
            const int x = int(luaL_checkinteger(L, 2)), y = int(luaL_checkinteger(L, 3));
            int align, width, font, varsIdx, nvars;
            textOpts(L, 4, false, align, width, font, varsIdx, nvars);
            DrawCmd d;
            d.kind = DrawCmd::Kind::Num;
            d.num = n;
            d.x = x;
            d.y = y;
            d.align = align;
            d.width = width;
            d.font = font;
            self(L).sceneCmds_.push_back(std::move(d));
            return 0;
        }

        // ---- objects and NPCs in the current area ----
        static int coordArg(lua_State *L, int idx)
        {
            const lua_Integer v = luaL_checkinteger(L, idx);
            luaL_argcheck(L, v >= -100000 && v <= 100000, idx, "a position must be between -100000 and 100000");
            return int(v);
        }
        // object.spawn(objtypeId, x, y [, { reach = {rx, ry} }]) -> the new object's id, or nil for an unknown object type.
        // x, y are the object's feet. With a reach the pet can use it (Z within the box); Area.hooks(...).onInteract then
        // fires for it. The id is "<mod>:obj/s<n>" and is only valid until the pet leaves the area.
        static int l_oSpawn(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *type = luaL_checkstring(L, 1);
            const int x = coordArg(L, 2), y = coordArg(L, 3);
            int rx = 0, ry = 0;
            if (!lua_isnoneornil(L, 4))
            {
                luaL_checktype(L, 4, LUA_TTABLE);
                lua_getfield(L, 4, "reach");
                if (!lua_isnil(L, -1))
                {
                    luaL_checktype(L, -1, LUA_TTABLE);
                    lua_geti(L, -1, 1);
                    lua_geti(L, -2, 2);
                    rx = int(luaL_checkinteger(L, -2));
                    ry = int(luaL_checkinteger(L, -1));
                    luaL_argcheck(L, rx >= 1 && rx <= 1000 && ry >= 1 && ry <= 1000, 4, "reach must be {rx, ry} with both 1 to 1000");
                }
            }
            if (!h.api_.spawnObject || !idCharsOk(type) || h.curMod_.empty())
            {
                lua_pushnil(L);
                return 1;
            }
            const std::string id = h.curMod_ + ":obj/s" + std::to_string(h.nextSpawn_++);
            if (!h.api_.spawnObject(id, type, x, y, rx, ry))
            {
                lua_pushnil(L);
                return 1;
            }
            lua_pushstring(L, id.c_str());
            return 1;
        }
        // object.move(id, x, y) -> true if the object is in this area. Works on the area's own objects too.
        static int l_oMove(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            const int x = coordArg(L, 2), y = coordArg(L, 3);
            lua_pushboolean(L, h.api_.moveObject && idCharsOk(id) && h.api_.moveObject(id, x, y));
            return 1;
        }
        // object.remove(id) -> true if it was there. Gone until the pet leaves the area (the area's own objects come back then).
        static int l_oRemove(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            lua_pushboolean(L, h.api_.removeObject && idCharsOk(id) && h.api_.removeObject(id));
            return 1;
        }
        // npc.spawn(npcId, x, y) -> true if that NPC (a record of the db) is now in this area at x, y. False for an unknown NPC, one that
        // is already here, or when the area is full. Its look and lines come from its record.
        static int l_nSpawn(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            const int x = coordArg(L, 2), y = coordArg(L, 3);
            lua_pushboolean(L, h.api_.spawnNpc && idCharsOk(id) && h.api_.spawnNpc(id, x, y));
            return 1;
        }
        // npc.move(npcId, x, y) -> true if the NPC is in this area. It starts over from the new spot (a wanderer's route follows it).
        static int l_nMove(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            const int x = coordArg(L, 2), y = coordArg(L, 3);
            lua_pushboolean(L, h.api_.moveNpc && idCharsOk(id) && h.api_.moveNpc(id, x, y));
            return 1;
        }
        // npc.remove(npcId) -> true if the NPC was in this area. Gone until the pet leaves the area.
        static int l_nRemove(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            lua_pushboolean(L, h.api_.removeNpc && idCharsOk(id) && h.api_.removeNpc(id));
            return 1;
        }

        // ---- HUD objectives ----
        // objective.set(id, { text = textId, item = itemId, need = n }): a line on the HUD, kept in the save until cleared. `text`
        // is a text id; `item` + `need` add a "have/need" counter of that item the player holds. At least one of text / item.
        // Setting an id again replaces it. The id must carry the mod's prefix, like flags. Returns false (and warns) for an
        // unknown text or item, or if too many objectives are up.
        static int l_objSet(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            const std::string &mod = h.curMod_;
            const bool prefixOk = !mod.empty() && std::strlen(id) > mod.size() + 1 && std::strncmp(id, mod.c_str(), mod.size()) == 0 && id[mod.size()] == ':';
            if (!prefixOk)
                return luaL_argerror(L, 1, lua_pushfstring(L, "objective ids must start with \"%s:\" (the mod's own prefix)", mod.c_str()));
            if (!idCharsOk(id))
                return luaL_argerror(L, 1, "objective id has a forbidden character (, = | or whitespace)");
            luaL_checktype(L, 2, LUA_TTABLE);
            lua_getfield(L, 2, "text");
            lua_getfield(L, 2, "item");
            lua_getfield(L, 2, "need");
            const char *text = lua_isnil(L, -3) ? "" : luaL_checkstring(L, -3);
            const char *item = lua_isnil(L, -2) ? "" : luaL_checkstring(L, -2);
            const lua_Integer need = lua_isnil(L, -1) ? (*item ? 1 : 0) : luaL_checkinteger(L, -1);
            if (!*text && !*item)
                return luaL_error(L, "objective.set: give a text, an item, or both");
            if ((*text && !idCharsOk(text)) || (*item && !idCharsOk(item)))
                return luaL_error(L, "objective.set: text and item ids cannot contain , = | or whitespace");
            if (*item && (need < 1 || need > 99))
                return luaL_error(L, "objective.set: need must be 1 to 99");
            if (!*item && need != 0)
                return luaL_error(L, "objective.set: need only goes with an item");
            lua_pushboolean(L, h.api_.setObjective && h.api_.setObjective(id, text, item, int(need)));
            return 1;
        }
        // objective.clear(id) -> true if it was up.
        static int l_objClear(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            lua_pushboolean(L, h.api_.clearObjective && idCharsOk(id) && h.api_.clearObjective(id));
            return 1;
        }
        // objective.has(id) -> boolean
        static int l_objHas(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            lua_pushboolean(L, h.api_.hasObjective && idCharsOk(id) && h.api_.hasObjective(id));
            return 1;
        }

        // ---- pet stats ----
        // The stat name argument is checked against `names` first; an unknown name is a script error that lists the
        // valid ones (a typo like "hungr" would otherwise silently do nothing). Plain C values only until the check.
        static const char *statArg(lua_State *L, int idx, const std::vector<std::string> &names)
        {
            const char *s = luaL_checkstring(L, idx);
            for (const std::string &n : names)
                if (n == s)
                    return s;
            luaL_Buffer b; // Lua-owned memory: no std::string may be alive when luaL_argerror unwinds
            luaL_buffinit(L, &b);
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (i)
                    luaL_addstring(&b, ", ");
                luaL_addstring(&b, names[i].c_str());
            }
            luaL_pushresult(&b);
            luaL_argerror(L, idx, lua_pushfstring(L, "unknown stat \"%s\" (valid: %s)", s, lua_tostring(L, -1)));
            return s; // not reached
        }
        // optional delta argument: an integer -9999..9999
        static int deltaArg(lua_State *L, int idx)
        {
            const lua_Integer n = luaL_checkinteger(L, idx);
            luaL_argcheck(L, n >= -9999 && n <= 9999, idx, "amount must be between -9999 and 9999");
            return int(n);
        }
        // player.care(stat) -> current value 0..100. Stats: hunger, health, happiness, tiredness, cleanliness
        // (all "higher is better").
        static int l_pCare(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *stat = statArg(L, 1, careStatNames());
            lua_pushinteger(L, h.api_.getCare ? h.api_.getCare(stat) : 0);
            return 1;
        }
        // player.adjustCare(stat, amount) -> the change actually made (clamped to 0..100; the game ignores a negative
        // change until 4 quests are done)
        static int l_pAdjustCare(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *stat = statArg(L, 1, careStatNames());
            const int delta = deltaArg(L, 2);
            lua_pushinteger(L, h.api_.adjustCare ? h.api_.adjustCare(stat, delta) : 0);
            return 1;
        }
        // player.personality(stat) -> current value 0..9999. Stats: halos, pitchforks, stars, lightbulbs
        static int l_pPersonality(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *stat = statArg(L, 1, personalityStatNames());
            lua_pushinteger(L, h.api_.getPersonality ? h.api_.getPersonality(stat) : 0);
            return 1;
        }
        // player.adjustPersonality(stat, amount) -> the change actually made (clamped to 0..9999)
        static int l_pAdjustPersonality(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *stat = statArg(L, 1, personalityStatNames());
            const int delta = deltaArg(L, 2);
            lua_pushinteger(L, h.api_.adjustPersonality ? h.api_.adjustPersonality(stat, delta) : 0);
            return 1;
        }

        // ---- say ----
        // say(textId [, vars]) -> true if the text exists and was queued, false if the id is unknown.
        // The text is a TextDef id (never a raw string), so all of a mod's text stays in its text records. In the text,
        // "%s" is the pet's name (as in NPC lines) and {name} is filled from `vars`, e.g. say("m:str/found", {item = "pie"}).
        // The box is shown after the hook returns, one box per call, in order. It is meant for hooks that take over
        // (return false); if the game's own dialogue opens right after the hook, the text is shown once that one closes.
        // Pass 1 of reading the `vars` table argument of say / ask: check it using plain C values only (a Lua error unwinds
        // with longjmp, so no std::string may exist yet). Returns the number of variables.
        static int checkVars(lua_State *L, int idx, const char *who)
        {
            int nvars = 0;
            if (!lua_isnoneornil(L, idx))
            {
                luaL_checktype(L, idx, LUA_TTABLE);
                lua_pushnil(L);
                while (lua_next(L, idx))
                {
                    if (lua_type(L, -2) != LUA_TSTRING)
                        luaL_error(L, "%s: variable names must be strings", who);
                    const char *key = lua_tostring(L, -2);
                    const size_t klen = std::strlen(key);
                    if (klen == 0 || klen > 32 || std::strspn(key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != klen)
                        luaL_error(L, "%s: variable name '%s' must be 1-32 letters, digits or _", who, key);
                    const int t = lua_type(L, -1);
                    if (t != LUA_TSTRING && t != LUA_TNUMBER)
                        luaL_error(L, "%s: the value of '%s' must be a string or a number", who, key);
                    if (t == LUA_TSTRING && lua_rawlen(L, -1) > 200)
                        luaL_error(L, "%s: the value of '%s' is longer than 200 characters", who, key);
                    if (++nvars > 16)
                        luaL_error(L, "%s: at most 16 variables", who);
                    lua_pop(L, 1);
                }
            }
            return nvars;
        }
        // Pass 2: nothing here can raise a Lua error.
        static ScriptApi::SayVars readVars(lua_State *L, int idx, int nvars)
        {
            ScriptApi::SayVars vars;
            if (nvars > 0)
            {
                lua_pushnil(L);
                while (lua_next(L, idx))
                {
                    lua_pushvalue(L, -1); // convert a copy: converting the table's own value would change it
                    size_t len = 0;
                    const char *v = lua_tolstring(L, -1, &len);
                    vars.emplace_back(lua_tostring(L, -3), std::string(v, len));
                    lua_pop(L, 2); // the copy and the value; the key stays for lua_next
                }
            }
            return vars;
        }

        static int l_say(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *id = luaL_checkstring(L, 1);
            const int nvars = checkVars(L, 2, "say");
            const ScriptApi::SayVars vars = readVars(L, 2, nvars);
            lua_pushboolean(L, h.api_.say && h.api_.say(id, vars));
            return 1;
        }

        // ask(textId [, vars]) -> true for Yes, false for No. Only inside a hook (it waits for the answer; the hook has taken
        // over). An unknown text id warns and returns false at once. The box comes after any say() boxes queued before it.
        // The work is in askQueue so that nothing with a destructor is alive when lua_yield unwinds the C stack.
        static bool askQueue(lua_State *L, ScriptHost &h)
        {
            const char *id = lua_tostring(L, 1);
            const int nvars = checkVars(L, 2, "ask");
            const ScriptApi::SayVars vars = readVars(L, 2, nvars);
            Routine *r = h.routineFor(L);
            if (!r || !h.api_.ask)
                return false;
            const int rid = r->id;
            ScriptHost *hp = &h;
            if (!h.api_.ask(id, vars, [hp, rid](bool yes)
                            { hp->answerRoutine(rid, yes); }))
                return false;
            r->wait = Wait::Asking; // (the answer cannot come before this function returns: the box is only queued)
            return true;
        }
        static int l_ask(lua_State *L)
        {
            if (!lua_isyieldable(L))
                return luaL_error(L, "ask can only be used inside a hook");
            luaL_checkstring(L, 1);
            if (!askQueue(L, self(L)))
            {
                lua_pushboolean(L, 0);
                return 1;
            }
            return lua_yield(L, 0); // resumed with the answer as ask's result
        }

        // wait(ticks): pause the hook for 1..1800 game ticks (30 per second), then carry on. The game goes on meanwhile.
        static int l_wait(lua_State *L)
        {
            if (!lua_isyieldable(L))
                return luaL_error(L, "wait can only be used inside a hook");
            const lua_Integer n = luaL_checkinteger(L, 1);
            if (n < 1 || n > 1800)
                return luaL_error(L, "wait: ticks must be 1 to 1800 (30 ticks are one second)");
            if (Routine *r = self(L).routineFor(L))
            {
                r->wait = Wait::Ticks;
                r->ticks = int(n);
            }
            return lua_yield(L, 0);
        }

        // ---- money and illness ----
        // player.money() -> the player's money (0..99999)
        static int l_pMoney(lua_State *L)
        {
            ScriptHost &h = self(L);
            lua_pushinteger(L, h.api_.getMoney ? h.api_.getMoney() : 0);
            return 1;
        }
        // player.adjustMoney(amount) -> the change actually made (money stays within 0..99999)
        static int l_pAdjustMoney(lua_State *L)
        {
            ScriptHost &h = self(L);
            const lua_Integer n = luaL_checkinteger(L, 1);
            luaL_argcheck(L, n >= -99999 && n <= 99999, 1, "amount must be between -99999 and 99999");
            lua_pushinteger(L, h.api_.adjustMoney ? h.api_.adjustMoney(int(n)) : 0);
            return 1;
        }
        // player.illness() -> the pet's illness id ("panda_pox", ...), or nil when healthy
        static int l_pIllness(lua_State *L)
        {
            ScriptHost &h = self(L);
            const int ill = h.api_.getIllness ? h.api_.getIllness() : -1;
            const std::vector<std::string> &names = illnessNames();
            if (ill < 0 || ill >= int(names.size()))
                lua_pushnil(L);
            else
                lua_pushstring(L, names[size_t(ill)].c_str());
            return 1;
        }
        // player.setIllness(id): make the pet ill with that illness. An unknown id is a script error that lists them.
        static int l_pSetIllness(lua_State *L)
        {
            ScriptHost &h = self(L);
            const char *name = luaL_checkstring(L, 1);
            const std::vector<std::string> &names = illnessNames();
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i] == name)
                {
                    if (h.api_.setIllness)
                        h.api_.setIllness(int(i));
                    return 0;
                }
            luaL_Buffer b; // Lua-owned memory: no std::string may be alive when luaL_argerror unwinds
            luaL_buffinit(L, &b);
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (i)
                    luaL_addstring(&b, ", ");
                luaL_addstring(&b, names[i].c_str());
            }
            luaL_pushresult(&b);
            return luaL_argerror(L, 1, lua_pushfstring(L, "unknown illness \"%s\" (valid: %s)", name, lua_tostring(L, -1)));
        }
        // player.cure() -> true if the pet was ill (and is healthy now), false if it was already healthy
        static int l_pCure(lua_State *L)
        {
            ScriptHost &h = self(L);
            const bool ill = h.api_.getIllness && h.api_.getIllness() >= 0;
            if (ill && h.api_.setIllness)
                h.api_.setIllness(-1);
            lua_pushboolean(L, ill);
            return 1;
        }

        Mem mem_;
        lua_State *L_ = nullptr;
        ScriptApi api_;
        EngineRegistry *registry_ = nullptr;
        std::map<std::string, std::string> modSave_; // "<mod>.<key>" -> typed text ("i12", "n1.5", "btrue", "sabc")
        std::map<std::string, Hook> npcHooks_;
        std::map<std::string, Hook> areaHooks_;
        std::map<std::string, Hook> sceneHooks_;
        std::map<std::string, Hook> itemHooks_;
        std::map<std::string, Hook> gameHooks_; // one table, under the id ""
        std::set<std::string> disabled_;
        std::string curMod_;
        std::vector<std::string> errors_;
    };
} // namespace gp