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
            {"friend.gift", "a new friend is made: the gift that drops where the pet stands, and on the 10th, 20th, 30th, 35th and 40th friend an extra one",
             "npc, friends (how many, counting this one), drop and milestone (item ids, \"\" = nothing): change them. An unknown id gives nothing"},
            {"quest.reward", "a story or generated quest runs one of its reward activities (when it is accepted or handed in); cancel or replace it and the dialogue just carries on",
             "code (the activity: -2 scatters the bonus items and may add a random find, -7 drops the quest's spawn table, -8 / -10 are the story rocks; change it to run another), quest (id, \"\" for a generated one), number, generic, type, giver, receiver"},
            {"item.use", "the player chose an item on the item screen to use (after the onBeforeItemUse / onItemUse hooks did not take it over); not for selling or giving",
             "item (id), type (the item's type number), room (1 in a home room); cancel it for nothing to happen, or replace it and do the job with the Lua API (player.removeItem, adjustCare...)"},
            {"trick.perform", "the player picks a trick on the tricks panel; the default is the pet doing it (level 1..3), or the question bubble if it has not learned it",
             "slot (0..2), level (the trick that will be done, 1..3: change it), known (1 = the pet knows this slot's trick, 0 = the question bubble: change it to let it do the trick anyway); cancel for nothing, replace to do it yourself"},
            {"trick.teach", "the player talks to a friend who is a trick teacher; the default offers the lesson when the rules allow",
             "npc, level (the teacher's trick, 1..3), known (how many tricks the pet knows), mask (which: bit 0 = trick 1 .. bit 2 = trick 3), cooldown (seconds left of the shared cooldown), can (1 = a lesson now: set it to decide; 0 falls through to a normal friend visit); replace or cancel = you did the talk"},
            {"trick.learn", "a lesson ends (the minigame was won or lost), before the message",
             "npc, level, won (1 / 0: change it), mask (which tricks the pet will know after: bit 0 = trick 1 .. bit 2 = trick 3; the level's bit is added on a win: change it, the pet may learn them in any order)"},
            {"quest.load", "the next quest is set up (a scripted story quest, or a generated one that the game rolls); replace it and do the choosing yourself, or change what was rolled in after",
             "number (the quest's number), scripted (1 = a story quest), giver, receiver (npc ids; set them), type (a quest type id, e.g. \"base:questtype/3\"), tier (the area tier 0..6), time (a time limit in seconds, 0 = none), objectives, bonus, spawn (lists of \"item_id*count\" joined by commas, e.g. \"base:item/205*2,m:item/gem*1\"; unknown items are dropped). A replace that leaves giver empty gets the game's roll"},
            {"quest.text", "a line of a generated quest is about to be shown (also the reminder of a story quest: kind \"reminder\"); change `text` to reword it",
             "kind (\"request\", \"accept\", \"decline\", \"reminder\", \"complete\", \"hint\", \"fail\", or \"story\" for a line of a story quest dialogue), text (the line, the {names} already filled in), number (the quest's number), type (a quest type id), giver, receiver (npc ids)"},
            {"room.enter", "the pet goes through the door of a home or hotel (before the screen fades to the room); cancel or replace and it stays outside",
             "location (0..2 the player homes, 4..7 the hotels), kind (\"home\" = its own, \"visit\" = another save slot's, \"hotel\")"},
            {"room.leave", "the pet has walked out of the room and is put back in the area; it cannot be cancelled",
             "location, x, y (where it comes out in the area: change them)"},
            {"room.interact", "Z next to something in a room: the door (leave), the food bowl (eat), the bed (sleep), or the visiting pet (give items); cancel or replace and nothing of that happens",
             "location, target (\"door\", \"bowl\", \"bed\" or \"visitor\"), x (the pet's place along the room)"},
            {"room.activity", "a button of the house bar starts a sequence in a room: sleeping or washing (the bed and the food bowl in the room itself are room.interact); cancel or replace and nothing starts",
             "location, activity (\"sleep\" or \"wash\")"},
            {"camera.view", "every frame in the walking world: where the camera is (the view's top left corner in the area), after the game worked it out; the belly-flop shake is already in it",
             "x, y (the pet's drawn place), camX, camY (the corner: change them to look elsewhere; not clamped to the area), zoom (1 = as is; 0.25 to 4) and angle (degrees) turn and scale the world around the middle of the view, the screens and HUD stay as they are, w, h (the area's size), viewW, viewH (the view's size in pixels)"},
            {"minigame.start", "a minigame is about to open (a well, a game tent, a friend's game, a trick lesson); cancel or replace and it does not open",
             "game (\"swift_pick\", \"three_in_a_row\", \"memory_match\" or \"trick_guess\")"},
            {"minigame.prize", "a minigame ended and is about to hand out its prize (the pet drops it); cancel or replace and nothing drops",
             "game (\"swift_pick\", \"three_in_a_row\" or \"memory_match\"), items (item ids joined by commas, one per prize: change the list; an unknown id is dropped)"},
            {"panel.open", "the player opens a panel (from the bar: the stats screen, the item screen, the tricks panel; or the island map from a map item); cancel or replace and it does not open: a mod may open its own screen instead (scene.open)",
             "panel (\"stats\", \"items\", \"tricks\" or \"map\")"},
            {"thought.show", "a thought bubble is about to go up over the pet (the game's hungry / sick / tired / low mood ones, or a mod's player.think); cancel or replace and none goes up",
             "kind (\"hungry\", \"sick\", \"tired\" or \"mood\": change it to show another icon)"},
            {"npc.sequence", "an idle NPC starts one of its show-off sequences: a trick, a call, a happy hop or a yawn; cancel or replace and it does not (it goes back to idling)",
             "npc (id), kind (\"trick\", \"call\", \"happy\" or \"yawn\"), level (trick only: 1..3, change it), repeats (trick only: how many times), count (call / happy / yawn: how many times)"},
            {"frontend.menu", "the player chose on the title menu (before the game's own action); cancel or replace and nothing happens. These front end systems need the mod's scripts, which the title screen loads too",
             "choice (\"new\", \"load\" or \"options\")"},
            {"frontend.start", "a save slot is about to be played (a new pet's, or a loaded one); cancel or replace and the front end stays",
             "slot (0..2: change it to play another)"},
            {"pet.wash", "the player washes the pet (the bar's wash button); cancel or replace and it is not washed",
             "cleanliness (the pet's cleanliness now)"},
            {"pet.sit", "the idle pet is about to sit down after a few seconds of standing; cancel or replace and it keeps standing",
             "idle (seconds it has stood)"},
            {"pet.activity", "one of the pet's running activities takes its step this tick (only while it runs); change `step` to jump (0 ends it), or cancel / replace and drive the pet yourself while the game's step is skipped",
             "kind (\"feed\", \"dig\", \"wash\", \"drop\" = a minigame prize being dropped, \"potion\", \"bark\", \"trick\", \"question\", \"colour\"), step (where it is: 1, 2, 3 ...; 0 = not running)"},
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
