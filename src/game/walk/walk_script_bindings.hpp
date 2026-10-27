// walk_script_bindings.hpp - connects the Lua host (ScriptHost) to the running game.
//
// This is the one place the script API meets the engine: every callback ScriptApi has is filled in here.
// New script API functions (and ctx.player) go here as more callbacks on ScriptApi.
// Include after walk_services.hpp.
#pragma once
#include "walk_services.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace walk
{

inline void bindScripts(WalkServices &svc)
{
    gp::ScriptApi &api = svc.scripts.api();

    // Errors inside a hook happen while the game runs, long after the startup checks: print them when they happen
    // (the host also keeps them in errors()). Set here, after loading, so startup errors aren't printed twice.
    api.onError = [](const std::string &message)
    { std::fprintf(stderr, "script error: %s\n", message.c_str()); };

    // ---- world ----
    // world.drop(itemId, x, y): false for an item the db doesn't know. Lands in the session-only ground items.
    api.drop = [&svc](const std::string &id, int x, int y)
    {
        const gp::ItemDef *it = svc.db.item(id);
        if (!it)
            return false;
        svc.events.placeAt(svc.currentArea(), it->index, x, y, false);
        return true;
    };

    // ---- rand.* ----
    // Scripts have their own stream, never the base game's. GP_SCRIPT_SEED=<number> makes it repeatable for tests;
    // otherwise it is seeded randomly each run.
    if (const char *seed = std::getenv("GP_SCRIPT_SEED"))
        svc.S.script.gen.seed(unsigned(std::strtoul(seed, nullptr, 10)));
    else
        svc.S.script.gen.seed(std::random_device{}());
    api.rnd = [&svc](int lo, int hi) // lo inclusive, hi EXCLUSIVE (same shape as RandFn)
    {
        if (hi <= lo)
            return lo;
        return std::uniform_int_distribution<int>(lo, hi - 1)(svc.S.script.gen);
    };

    // ---- ctx.player (a read-only snapshot handed to hooks) ----
    api.playerInfo = [&svc]
    {
        gp::ScriptApi::PlayerInfo p;
        p.name = svc.player.name;
        p.money = svc.player.money;
        p.questsCompleted = svc.quests.counter; // the live counter; player.questsCompleted is only copied at save time
        return p;
    };

    // ---- player.* ----
    // give: only items the db knows, capped at the item's max stack (like the starter items). Returns how many went in.
    api.give = [&svc](const std::string &id, int n)
    {
        const gp::ItemDef *d = svc.db.item(id);
        if (!d)
            return 0;
        const int room = std::max(1, int(d->maxQty)) - svc.player.itemCount(d->id);
        const int add = std::min(n, std::max(room, 0));
        if (add > 0)
            svc.player.addItem(d->id, add);
        return add;
    };
    // take / count work on ids directly, so an item from an uninstalled mod can still be counted and removed.
    api.take = [&svc](const std::string &id, int n)
    {
        const int take = std::min(n, svc.player.itemCount(id));
        if (take > 0)
            svc.player.removeItem(id, take);
        return std::max(take, 0);
    };
    api.count = [&svc](const std::string &id)
    { return svc.player.itemCount(id); };
    // Flags are the same string flags the save already stores, so script state persists with no new save code.
    // The host has already checked that a flag being SET carries the calling mod's prefix.
    api.hasFlag = [&svc](const std::string &flag)
    { return svc.player.hasFlag(flag); };
    api.setFlag = [&svc](const std::string &flag)
    { svc.player.setFlag(flag); };

    // The pet's stats. The host has checked the names against careStatNames() / personalityStatNames(); the mapping
    // below must cover every one of them. Changes go through Player::adjustCare / adjustPersonality, so the game's own
    // rules apply (clamping, negative care ignored until 4 quests, halos / lightbulbs clearing "encountered") and the
    // callbacks report the change that was actually made. (AddToStatCounter is not called: the counters are not stored yet.)
    auto careStat = [](const std::string &name, gp::CareStat &out)
    {
        static const std::pair<const char *, gp::CareStat> t[] = {{"hunger", gp::Hunger},         {"health", gp::Health},
                                                                  {"happiness", gp::Happiness},   {"tiredness", gp::Tiredness},
                                                                  {"cleanliness", gp::Cleanliness}};
        for (const auto &[n, s] : t)
            if (name == n)
            {
                out = s;
                return true;
            }
        return false;
    };
    auto personalityStat = [](const std::string &name, gp::PersonalityStat &out)
    {
        static const std::pair<const char *, gp::PersonalityStat> t[] = {{"halos", gp::Halos},   {"pitchforks", gp::Pitchforks},
                                                                         {"stars", gp::Stars}, {"lightbulbs", gp::Lightbulbs}};
        for (const auto &[n, s] : t)
            if (name == n)
            {
                out = s;
                return true;
            }
        return false;
    };
    api.getCare = [&svc, careStat](const std::string &name)
    {
        gp::CareStat s;
        return careStat(name, s) ? svc.player.care[s] : 0;
    };
    api.adjustCare = [&svc, careStat](const std::string &name, int delta)
    {
        gp::CareStat s;
        if (!careStat(name, s))
            return 0;
        const int before = svc.player.care[s];
        svc.player.adjustCare(s, delta);
        return svc.player.care[s] - before;
    };
    api.getPersonality = [&svc, personalityStat](const std::string &name)
    {
        gp::PersonalityStat s;
        return personalityStat(name, s) ? svc.player.personality[s] : 0;
    };
    api.adjustPersonality = [&svc, personalityStat](const std::string &name, int delta)
    {
        gp::PersonalityStat s;
        if (!personalityStat(name, s))
            return 0;
        const int before = svc.player.personality[s];
        svc.player.adjustPersonality(s, delta);
        return svc.player.personality[s] - before;
    };

    // Money: Player::adjustMoney clamps to 0..99999 and returns the change made. (AddToLifetimeEarnings is not ported.)
    api.getMoney = [&svc]
    { return svc.player.money; };
    api.adjustMoney = [&svc](int delta)
    { return svc.player.adjustMoney(delta); };
    // Illness: the host's illnessNames() order is the ROM illness number 0..9, and Player::illnessId uses the same
    // numbers (kHealthy = 10 for none), so no name table is needed here. The pet's sick / medicine animations are not
    // started from here; they belong to the use / eat flows in walk.cpp.
    api.getIllness = [&svc]
    { return svc.player.isHealthy() ? -1 : svc.player.illnessId; };
    api.setIllness = [&svc](int illness)
    { svc.player.illnessId = illness < 0 ? gp::kHealthy : illness; };
}

} // namespace walk