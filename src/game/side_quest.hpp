// side_quest.hpp - the state of a mod's side quests (record `side_quest`): two flags on the player, what each objective needs, the hand-in and the reward.
// The conversation that offers and completes them is in walk/walk_npc_talk.hpp; the HUD lines are drawn from the db and the player (nothing about them is saved).
#pragma once
#include "player.hpp"
#include "../content/records.hpp"
#include <algorithm>

namespace gp
{
    enum class SideState
    {
        Idle,   // not taken (or handed in, and repeatable)
        Active, // taken, not finished
        Done
    };

    inline std::string sideActiveFlag(const Id &id) { return "sq:" + id + ":active"; }
    inline std::string sideDoneFlag(const Id &id) { return "sq:" + id + ":done"; }

    inline std::string sideTimeKey(const Id &id) { return "sq:" + id + ":steps"; } // simulation steps it has been running (a counter, so it is saved)

    inline SideState sideState(const Player &p, const SideQuestDef &q)
    {
        if (p.hasFlag(sideActiveFlag(q.id)))
            return SideState::Active;
        return p.hasFlag(sideDoneFlag(q.id)) ? SideState::Done : SideState::Idle;
    }

    inline const char *sideStateName(SideState s) { return s == SideState::Active ? "active" : s == SideState::Done ? "done" : "idle"; }

    inline int counterOf(const Player &p, const std::string &name)
    {
        const auto it = p.counters.find(name);
        return it == p.counters.end() ? 0 : it->second;
    }

    // Seconds left on a timed quest (at `tickHz` steps a second); -1 if it has no limit.
    inline double sideTimeLeft(const Player &p, const SideQuestDef &q, double tickHz)
    {
        if (q.timeLimit <= 0)
            return -1;
        return std::max(0.0, q.timeLimit - double(counterOf(p, sideTimeKey(q.id))) / std::max(1.0, tickHz));
    }

    // Whether the offer is made now.
    inline bool sideOfferable(const Player &p, const SideQuestDef &q)
    {
        const SideState s = sideState(p, q);
        if (s == SideState::Active || (s == SideState::Done && !q.repeatable))
            return false;
        return q.availableAt(p.questsCompleted) && (q.requires_.empty() || p.hasFlag(q.requires_));
    }

    // How far one objective is: have (capped at need) out of need. A flag is 0 or 1 out of 1.
    inline std::pair<int, int> sideProgress(const Player &p, const SideObjective &o)
    {
        switch (o.kind)
        {
        case SideObjective::Item:
            return {std::min(p.itemCount(o.item), o.count), o.count};
        case SideObjective::Counter:
            return {std::min(counterOf(p, o.counter), o.count), o.count};
        default:
            return {p.hasFlag(o.flag) ? 1 : 0, 1};
        }
    }
    inline bool sideObjectiveDone(const Player &p, const SideObjective &o)
    {
        const auto pr = sideProgress(p, o);
        return pr.first >= pr.second;
    }

    // Every objective is done.
    inline bool sideHasAll(const Player &p, const SideQuestDef &q)
    {
        for (const SideObjective &o : q.objectives)
            if (!sideObjectiveDone(p, o))
                return false;
        return true;
    }

    // Accepted (or started by a script): the flag. Flags and counters its objectives watch start again from nothing, so a quest that comes round again (or one whose
    // flag was set before it was taken) asks for the thing afresh.
    inline void sideStart(Player &p, const SideQuestDef &q)
    {
        p.flags.erase(sideDoneFlag(q.id));
        p.counters.erase(sideTimeKey(q.id));
        for (const SideObjective &o : q.objectives)
        {
            if (o.kind == SideObjective::Flag)
                p.flags.erase(o.flag);
            else if (o.kind == SideObjective::Counter)
                p.counters.erase(o.counter);
        }
        p.setFlag(sideActiveFlag(q.id));
    }

    // Ended without finishing (a script gave up on it): back to idle, nothing paid.
    inline void sideAbandon(Player &p, const SideQuestDef &q)
    {
        p.flags.erase(sideActiveFlag(q.id));
        p.flags.erase(sideDoneFlag(q.id));
        p.counters.erase(sideTimeKey(q.id));
    }

    // Finished: the items asked for go, it is done. The reward is separate (sideReward), so a script can take it over.
    inline void sideFinish(Player &p, const SideQuestDef &q)
    {
        for (const SideObjective &o : q.objectives)
            if (o.kind == SideObjective::Item)
                p.removeItem(o.item, std::min(o.count, p.itemCount(o.item)));
        p.flags.erase(sideActiveFlag(q.id));
        p.counters.erase(sideTimeKey(q.id));
        p.setFlag(sideDoneFlag(q.id));
    }

    // The reward: money, and the items (up to what the bag holds of each).
    template <class MaxQty>
    inline void sideReward(Player &p, const SideQuestDef &q, MaxQty &&maxQty)
    {
        if (q.rewardMoney)
            p.adjustMoney(q.rewardMoney);
        for (const QuestEntry &e : q.rewardItems)
        {
            const int room = std::max(0, maxQty(e.item) - p.itemCount(e.item));
            if (const int n = std::min(room, e.count))
                p.addItem(e.item, n);
        }
    }

    // The HUD line of one objective: "have/need  name" (a flag counts 0/1 or 1/1). `itemName` and `text` look names up.
    template <class ItemName, class TextOf>
    inline std::string sideLine(const Player &p, const SideObjective &o, ItemName &&itemName, TextOf &&text)
    {
        const auto pr = sideProgress(p, o);
        std::string name = !o.text.empty() ? text(o.text) : o.kind == SideObjective::Item ? itemName(o.item) : std::string();
        return std::to_string(pr.first) + "/" + std::to_string(pr.second) + "  " + name;
    }
} // namespace gp
