#pragma once
#include <array>
#include <vector>
#include <utility>
#include <algorithm>
#include "pending_event.hpp" // RandFn
#include "../content/records.hpp" // QuestEntry, QuestDef

namespace gp
{
    struct QuestState
    {
        QuestDef def;
        int phase = 0;
        bool hudActive = false, readyToTurnIn = false, noItemReactionPlayed = false;
        int timerStart = 0; // g_questTimerVisible: tick baseline, 0 = none
        int dialogueCall = 0;
        std::array<int, 3> startHeld{};
        int prevTypeMode = -1, prevReceiver = -1; // survive loads, like the globals
    };

    // Tail of LoadScriptedQuestData: new quest, all flags reset, "previous" values kept.
    inline void loadQuest(QuestState &q, QuestDef d)
    {
        const int pt = q.prevTypeMode, pr = q.prevReceiver;
        q = QuestState{};
        q.def = std::move(d);
        q.prevTypeMode = pt;
        q.prevReceiver = pr;
    }

    struct QuestLine
    {
        enum Kind
        {
            None,
            Text,
            Confirm,
            Action
        } kind = None;
        int text = -1, action = 0;
    };

    // ShowQuestIntroDialogue shows offerText[0] after setting dialogueCall = 0.
    // ShowQuestOfferDialogue = this. Generic quests return text = -1 (generate it): call 1 Text, call 2 Confirm.
    inline QuestLine offerStep(QuestState &q)
    {
        const auto &L = q.def.offerText;
        const int i = ++q.dialogueCall;
        if (q.def.generic)
            return i == 1 ? QuestLine{QuestLine::Text} : i == 2 ? QuestLine{QuestLine::Confirm}
                                                                : QuestLine{};
        if (i >= int(L.size()))
            return {};
        if (L[i] < 0)
            return {QuestLine::Action, -1, L[i]};
        return {i + 1 >= int(L.size()) ? QuestLine::Confirm : QuestLine::Text, L[i], 0};
    }

    struct QuestAdvance
    {
        enum Kind
        {
            Text,
            Action,
            NextQuest
        } kind = NextQuest;
        int text = -1, action = 0;
    };

    // AdvanceToNextQuestSequence. NextQuest: caller loads quest (questsCompleted + 1, check the arg in the disassembly).
    inline QuestAdvance advanceStep(QuestState &q)
    {
        if (!q.def.generic)
        {
            const auto &C = q.def.completionText;
            if (q.dialogueCall + 1 < int(C.size()))
            {
                const int v = C[++q.dialogueCall];
                return v >= 0 ? QuestAdvance{QuestAdvance::Text, v, 0} : QuestAdvance{QuestAdvance::Action, -1, v};
            }
        }
        else if (q.dialogueCall == 0)
        {
            q.dialogueCall = 1;
            return {QuestAdvance::Action, -1, -2};
        }
        return {};
    }

    inline int questSpawnCount(int count, int done)
    {
        if (count == 1)
            return 1;
        if (done < 0x23)
            return count * 2;
        // 0x23..0x45: ROM does count * X in soft float, compared with count + 1. X not visible in the decompile. GUESS: 1.5.
        if (done <= 0x45)
            return std::max(count + 1, count * 3 / 2);
        return count + 1;
    }

    // ProcessQuestTurnIn (really "accept"), before the dialogue. spawn(areaTier, item, state, flagged).
    template <class Held, class Spawn>
    void acceptQuest(QuestState &q, int done, const RandFn &rnd, Held held, Spawn spawn, bool spawnObjectives = true)
    {
        if (spawnObjectives) // QuestTypeDef::spawnsOnAccept
            for (size_t k = 0; k < q.def.objectives.size() && k < 3; ++k)
            {
                const QuestEntry &o = q.def.objectives[k];
                const int n = questSpawnCount(o.count, done);
                for (int i = 0; i < n; ++i)
                {
                    const int st = o.w2 == 0 ? 2 : (o.count < 2 ? 0 : (rnd(1, 5) == 1 ? 2 : 0)); // 2 visible, 0 buried
                    spawn(q.def.areaTier, o.item, st, true);
                }
                q.startHeld[k] = held(o.item);
            }
        q.phase = 1;
    }

    // StartQuest (the Accept handler's follow-up).
    inline void startQuest(QuestState &q, int nowTick)
    {
        q.hudActive = true;
        if (q.def.typeMode == 1)
            q.timerStart = nowTick ? nowTick : 1;
    }

    enum class TurnIn
    {
        NoQuest,
        Missing,
        Done
    };

    // HandleQuestNPCInteraction. Done: caller deletes flagged events, shows completionText[0].
    // Phase stays 1 until the next quest loads.
    template <class Held, class Remove>
    TurnIn tryTurnIn(QuestState &q, Held held, Remove remove)
    {
        if (q.phase == 0)
            return TurnIn::NoQuest;
        for (const QuestEntry &o : q.def.objectives)
            if (held(o.item) < o.count)
                return TurnIn::Missing;
        q.timerStart = 0;
        q.hudActive = q.noItemReactionPlayed = q.readyToTurnIn = false;
        q.prevTypeMode = q.def.typeMode;
        q.prevReceiver = q.def.receiver;
        for (const QuestEntry &o : q.def.objectives)
            remove(o.item, o.count);
        q.dialogueCall = 0;
        return TurnIn::Done;
    }

    enum class QuestTimer
    {
        Ok,
        Overrun,
        Failed
    };

    // The timed-quest part of TickQuestProgressCheck. elapsed is in the ROM's unit (seconds).
    inline QuestTimer checkTimer(QuestState &q, int elapsed)
    {
        if (q.phase == 0 || q.def.typeMode != 1 || q.timerStart == 0 || elapsed <= q.def.timeLimit)
            return QuestTimer::Ok;
        if (elapsed - q.def.timeLimit > 2)
            return QuestTimer::Failed;
        if (q.noItemReactionPlayed)
            return QuestTimer::Ok;
        q.noItemReactionPlayed = true;
        return QuestTimer::Overrun; // play the mini-pet "no item" reaction
    }

    // Failure: phase 0 and the same quest is offered again. Removes only what was picked up during the quest.
    template <class Held, class Remove>
    void failQuest(QuestState &q, Held held, Remove remove)
    {
        q.phase = 0;
        q.timerStart = 0;
        q.hudActive = q.noItemReactionPlayed = false;
        for (size_t k = 0; k < q.def.objectives.size() && k < 3; ++k)
            remove(q.def.objectives[k].item, held(q.def.objectives[k].item) - q.startHeld[k]);
        // caller: delete flagged events, show "Quest failed! Go see <giver> <prep> <area> to try again."
    }
    // Game tick counter: +2 per 30 Hz tick (60 per second). The timer and its HUD both divide by 60.
    inline int elapsedSeconds(int counter, int start) { return (counter - start) / 60; }

    // DrawQuestRewardHud, timer half.
    inline bool timerShown(const QuestState &q)
    {
        return q.phase != 0 && q.hudActive && q.def.typeMode == 1 && q.timerStart != 0;
    }
    inline int timerRemaining(const QuestState &q, int elapsed)
    {
        return q.def.timeLimit - std::min(elapsed, q.def.timeLimit);
    }

    // GetQuestReminderDialogue. roll = RandRange(1, 4), i.e. 1..3 (always rolled, even for modes 2/3/6/7).
    inline const char *reminderKey(const QuestTypeDef &t, int roll)
    {
        if (!t.reminderKey.empty())
            return t.reminderKey.c_str();
        return roll == 1 ? "reminder_default_1" : roll == 2 ? "reminder_default_2"
                                                            : "reminder_default_3";
    }
}