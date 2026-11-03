// script_check.hpp - checks the hooks mod scripts registered, once ALL mods' data and scripts have loaded.
//
// A hook that can never fire is the easiest mistake to make in a script and the hardest to notice:
//   Npc.hooks("mymod:npc/typo")      -- no such NPC: registers fine, never runs
//   function T:onTalkstart(ctx) ...  -- not a hook name (it is onTalkStart): registers fine, never runs
// This reports both, with the mod and the file:line, so the game can refuse to start like it does for a script error.
#pragma once
#include "script_host.hpp"
#include <cctype>

namespace gp
{
    // Whether an id exists. An unset predicate skips that check.
    struct HookTargets
    {
        std::function<bool(const std::string &)> npcExists;
        std::function<bool(const std::string &)> areaExists;
        std::function<bool(const std::string &)> itemExists;
    };

    // Returns one message per problem, empty if every hook is fine. Message format: "[mod] file:line: text".
    inline std::vector<std::string> validateHooks(ScriptHost &host, const HookTargets &targets)
    {
        const auto lower = [](std::string s)
        {
            for (char &c : s)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        std::vector<std::string> problems;
        for (const HookInfo &h : host.hooks())
        {
            static const std::function<bool(const std::string &)> noTarget; // Game.hooks() has no id to check
            const auto &exists = h.kind == 'N'   ? targets.npcExists
                                 : h.kind == 'I' ? targets.itemExists
                                 : h.kind == 'G' || h.kind == 'S' ? noTarget // a scene is defined by its hook table: any id
                                                 : targets.areaExists;
            const std::string api = h.kind == 'N' ? "Npc.hooks" : h.kind == 'I' ? "Item.hooks" : h.kind == 'G' ? "Game.hooks" : h.kind == 'S' ? "Scene.hooks" : "Area.hooks";
            const char *noun = h.kind == 'N' ? "NPC" : h.kind == 'I' ? "item" : "area";
            const std::string call = h.kind == 'G' ? api + "()" : api + "(\"" + h.id + "\")";
            if (exists && !exists(h.id))
                problems.push_back("[" + h.mod + "] " + h.where + ": " + call + ": no such " + noun +
                                   ", so this hook can never fire");
            for (const HookInfo::Fn &fn : h.fns)
            {
                if (fn.known)
                    continue;
                std::string msg = "[" + h.mod + "] " + fn.where + ": " + call + " defines '" + fn.name +
                                  "', which is not a hook the game fires (valid: " + h.valid + ")";
                for (const std::string &valid : ScriptHost::hookNames(h.kind)) // a wrong-case name is the usual typo
                    if (lower(valid) == lower(fn.name))
                        msg += "; did you mean '" + valid + "'?";
                problems.push_back(std::move(msg));
            }
        }
        return problems;
    }
} // namespace gp