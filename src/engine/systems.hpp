// systems.hpp - the engine's named systems: parts of the game a mod can wrap or replace (`System.hooks("care.decay")` in Lua).
//
// A system is one thing the engine does (a stat wearing down, a pet stepping, an NPC picking what to do). Where the engine does it, it calls
// ScriptHost::system(name, ctx, defaultFn): the mods' `before(ctx)` hooks run (they may change ctx's values, or `return false` to cancel it all), then the first mod's
// `replace(ctx)` if there is one (it does the job itself: the C++ default is skipped), else the default runs with the values in ctx, then the mods' `after(ctx)` hooks.
// With no mod hooking a system there is no cost and no change: the default runs.
//
// Adding a system: one row in systemDefs() (the name, a line for modders, the ctx fields), and the call site wrapped with ScriptHost::system.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace gp
{
    // What a system's ctx holds: named numbers and strings. A hook reads and changes them as fields of `ctx`; the engine reads them back afterwards.
    struct SystemCtx
    {
        struct Value
        {
            std::string name;
            bool isString = false;
            double num = 0;
            std::string str;
        };
        std::vector<Value> values;

        SystemCtx &set(const std::string &name, double v)
        {
            for (Value &e : values)
                if (e.name == name)
                {
                    e.isString = false;
                    e.num = v;
                    return *this;
                }
            values.push_back({name, false, v, {}});
            return *this;
        }
        SystemCtx &set(const std::string &name, const std::string &v)
        {
            for (Value &e : values)
                if (e.name == name)
                {
                    e.isString = true;
                    e.str = v;
                    return *this;
                }
            values.push_back({name, true, 0, v});
            return *this;
        }
        double num(const std::string &name, double def = 0) const
        {
            for (const Value &e : values)
                if (e.name == name && !e.isString)
                    return e.num;
            return def;
        }
        std::string str(const std::string &name) const
        {
            for (const Value &e : values)
                if (e.name == name && e.isString)
                    return e.str;
            return {};
        }
    };

    struct SystemDef
    {
        const char *name;
        const char *what;   // for modders: what it is and when it runs
        const char *fields; // the ctx fields, for modders
    };

    // Every system a mod can hook. Names are public API (renaming one is a game_api major bump).
    inline const std::vector<SystemDef> &systemDefs()
    {
        static const std::vector<SystemDef> defs = {
            {"care.decay", "a care stat wears down by itself (hunger and tiredness, on a timer)",
             "stat (\"hunger\" or \"tiredness\"), amount (how much it falls by, a positive number: change it, or set 0)"},
            {"pet.move", "the pet takes this tick's steps while a direction is held (not while it is locked, in a dialogue or in a house room)",
             "x, y (where it is: set them in replace to move it yourself), dx, dy (the way it goes: -1, 0 or 1; change them to reverse the controls, or set both 0 to hold it), steps (how many pixel steps this tick)"},
            {"npc.tick", "an NPC's own brain takes one tick: it walks its path, or picks and runs an idle activity (the sequences of tricks and calls have their own code and still run)",
             "id (the npc's id), x, y (set them in replace to move it yourself), activity (what it is doing: 0x17 = wandering), stationary, scripted (1 while a script drives it: npc.walkTo...)"},
            {"encounter.start", "the pet meets an NPC it has not met (before the greeting sequence); cancel or replace and nothing starts, the NPC stays unmet",
             "npc (id), pet (the pet's personality delta), target (the NPC's delta): the two numbers the rules compare; change them to change how well they get on"},
            {"encounter.classify", "the sequence decides how the pet and the NPC react to each other, once the first calls are over; the codes also decide the outcome",
             "npc, pet, target (the deltas), player and animal (the reaction codes the rules gave: 1, 2 friendly, 3 / 7 startled or sad, 4, 5 hostile, 6 bored): change them. A friendly animal code with a player code that is not 4 or 5 is a friendly outcome; 4 or 5 on the animal side is a hostile one; anything else neutral"},
            {"encounter.finish", "the sequence has ended; after a friendly outcome the game rolls for friendship",
             "npc, outcome (\"friend\" or \"other\"), chance (the roll's number, 0..100+), friend (1 = they become friends: set it to decide; the default rolls only after a friendly outcome)"},
            {"friend.visit", "the pet visits a friend it has not seen this session, and may be given a gift",
             "npc, chance (the roll's number), hit (1 = a gift: set it to decide)"},
        };
        return defs;
    }
    inline bool systemKnown(const std::string &name)
    {
        for (const SystemDef &d : systemDefs())
            if (name == d.name)
                return true;
        return false;
    }
} // namespace gp
