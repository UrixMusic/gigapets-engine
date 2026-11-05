// activity_system.hpp - the engine system "pet.activity": one step of a running activity of the pet, with the mods' say.
#pragma once
#include <functional>
#include "../mods/script_host.hpp"

namespace gp
{
    // `step` is the activity's step counter (0 = not running: then it is just `tick`, no system is called). A mod's before hook may change ctx.step (the game's own tick then starts from
    // that step); a replace or after hook may change it too (taken after). Values outside 0..999 are ignored. Cancel or replace skips `tick`.
    inline void runActivity(ScriptHost &scripts, const char *kind, int &step, const std::function<void()> &tick)
    {
        if (step == 0 || !scripts.hasSystem("pet.activity"))
        {
            tick();
            return;
        }
        SystemCtx ac;
        ac.set("kind", kind).set("step", step);
        int seen = step; // the step value the game has been told about
        const auto take = [&]
        {
            const int v = int(ac.num("step"));
            if (v != seen && v >= 0 && v < 1000)
                step = seen = v;
        };
        scripts.system("pet.activity", ac, [&]
                       {
                           take();
                           tick(); });
        take();
    }
} // namespace gp
