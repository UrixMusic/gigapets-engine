// side_quest.hpp - the state of a mod's side quests (record `side_quest`): two flags on the player, the HUD lines, the hand-in and the reward.
// The conversation that offers and completes them is in walk/walk_npc_talk.hpp.
#pragma once
#include "player.hpp"
#include "../content/records.hpp"
#include <algorithm>

namespace gp
{
    enum class SideState
    {
        Idle,   // not taken (or handed in, and repeatable)
        Active, // taken, not handed in
        Done
    };

    inline std::string sideActiveFlag(const Id &id) { return "sq:" + id + ":active"; }
    inline std::string sideDoneFlag(const Id &id) { return "sq:" + id + ":done"; }
    inline std::string sideHudId(const Id &id, size_t k) { return "sq:" + id + "#" + std::to_string(k); } // the HUD line of its k-th objective

    inline SideState sideState(const Player &p, const SideQuestDef &q)
    {
        if (p.hasFlag(sideActiveFlag(q.id)))
            return SideState::Active;
        return p.hasFlag(sideDoneFlag(q.id)) ? SideState::Done : SideState::Idle;
    }

    // Whether the giver offers it now.
    inline bool sideOfferable(const Player &p, const SideQuestDef &q)
    {
        const SideState s = sideState(p, q);
        if (s == SideState::Active || (s == SideState::Done && !q.repeatable))
            return false;
        return q.availableAt(p.questsCompleted) && (q.requires_.empty() || p.hasFlag(q.requires_));
    }

    // Everything asked for is in the bag.
    inline bool sideHasAll(const Player &p, const SideQuestDef &q)
    {
        for (const QuestEntry &o : q.objectives)
            if (p.itemCount(o.item) < o.count)
                return false;
        return true;
    }

    // Accepted: the flag and the HUD lines (one per item to bring, "have/need  name").
    inline void sideStart(Player &p, const SideQuestDef &q)
    {
        p.flags.erase(sideDoneFlag(q.id));
        p.setFlag(sideActiveFlag(q.id));
        for (size_t k = 0; k < q.objectives.size(); ++k)
        {
            ScriptObjective o;
            o.id = sideHudId(q.id, k);
            o.item = q.objectives[k].item;
            o.need = q.objectives[k].count;
            p.setObjective(std::move(o));
        }
    }

    // Handed in: the items go, the HUD lines go, it is done. The reward is separate (sideReward), so a script can take it over.
    inline void sideFinish(Player &p, const SideQuestDef &q)
    {
        for (const QuestEntry &o : q.objectives)
            p.removeItem(o.item, o.count);
        for (size_t k = 0; k < q.objectives.size(); ++k)
            p.clearObjective(sideHudId(q.id, k));
        p.flags.erase(sideActiveFlag(q.id));
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
} // namespace gp
