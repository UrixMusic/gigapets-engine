#pragma once
#include <functional>
#include <string>
#include <vector>
#include "dialogue.hpp"
#include "quest.hpp"
#include "quest_gen.hpp"
#include "quest_text.hpp"

namespace gp
{
    struct QuestHooks
    {
        std::function<std::string(int)> text; // text id -> final string (pet name applied)
        std::function<int(int)> held;
        std::function<void(int, int)> remove;
        std::function<void(int, int, int, bool)> spawn; // areaTier, item, state (2 visible / 0 buried), flagged: random free slot
        std::function<void(int, int, int, bool)> place; // item, x, y, flagged: pending event at a position
        std::function<void()> randomEvent;              // the extra random event after a -2 reward
        std::function<void()> clearFlagged;             // ConsumeAllFlaggedPendingEvents
        std::function<void(int)> sfx;
        std::function<int()> camX, camY;
        std::function<int()> receiverY; // y of the live receiver NPC (return camY if it isn't in the area)
        std::function<int()> nowTick;
        std::function<void(DialogueBox &, int)> portrait; // set the speaker portrait on a box (pool id)
        std::function<void()> onNextQuest;                // fired after the next quest has loaded
        RandFn rnd;
    };

    // Captures `this` in callbacks: don't move or copy it once a box is open.
    struct QuestRuntime
    {
        QuestState q;
        std::vector<QuestDef> table; // scripted records
        QuestWorld world;            // set world.areaTier to enable generic quests
        QuestTextEnv txt;            // generic quest text builders (all optional; placeholders if unset)
        int counter = 1;             // g_totalQuestsCompleted: the first quest has id 1
        Dialogue *dlg = nullptr;
        QuestHooks h;

        int lastId() const { return table.empty() ? -1 : table.back().id; }

        int statusText(std::string &out) const
        {
            out.clear();
            if (q.def.id < 0 || !textReady())
                return -1;
            return questStatusText(q, txt, h.held, out);
        }

        void load(int n) // LoadScriptedQuestData
        {
            counter = n > lastId() ? lastId() + 1 : n;
            QuestDef d;
            bool scripted = false;
            for (const auto &r : table)
                if (r.id == counter)
                {
                    d = r;
                    scripted = true;
                    break;
                }
            if (!scripted)
            {
                if (world.areaTier)
                {
                    d = rollNextQuest(counter, q.prevReceiver, q.prevTypeMode, table, world, h.rnd);
                    d.id = counter;
                }
                d.generic = true; // id stays -1 (talk() ignores it) until world is set
            }
            loadQuest(q, std::move(d));
        }

        // Call first in talkToNpc. True = handled.
        bool talk(int poolId)
        {
            if (q.def.id < 0)
                return false;
            if (poolId == q.def.giver && q.phase == 0)
            {
                speaker = poolId;
                intro();
                return true;
            }
            if (poolId == q.def.receiver && q.phase != 0)
            {
                speaker = poolId;
                turnIn();
                return true;
            }
            return false;
        }

        // Call once per game tick. nowTick = same counter as hooks.nowTick.
        void tick(int nowTick)
        {
            if (q.timerStart == 0)
                return;
            switch (checkTimer(q, elapsedSeconds(nowTick, q.timerStart)))
            {
            case QuestTimer::Ok:
                break;
            case QuestTimer::Overrun:
                break; // mini-pet "no item" reaction: not ported
            case QuestTimer::Failed:
                failQuest(q, h.held, h.remove);
                if (h.clearFlagged)
                    h.clearFlagged();
                if (dlg)
                {
                    speaker = -1;
                    dlg->show(messageBox(failText()));
                }
                break;
            }
        }

        // StartQuestRewardActivity codes. -7 (0x1f) and -8..-11 (0x20..0x23) are provisional.
        void reward(int code, std::function<void()> done)
        {
            switch (code)
            {
            case -2: // 0x18: bonus table scattered, unflagged
            {
                for (const auto &s : scatter(q.def.bonus, false, h.camX(), h.camY(), h.receiverY(), h.rnd))
                    h.place(s.item, s.x, s.y, s.flagged);
                if (counter != 0x17 && h.rnd(1, 0x65) < 0x33)
                    h.randomEvent();
                h.sfx(0x54);
                break;
            }
            case -7: // 0x1f: spawn table dropped near the player, flagged
                for (const auto &s : scatter(q.def.spawn, true, h.camX(), h.camY(), h.receiverY(), h.rnd))
                    h.place(s.item, s.x, s.y, s.flagged);
                h.sfx(0x54);
                break;
            default:
                break; // -8..-11: minipet cartridge sequences, not ported
            }
            done();
        }

    private:
        int speaker = -1; // pool id whose portrait goes on the boxes

        bool textReady() const { return txt.npcName && txt.areaName && txt.itemName && txt.rnd; }

        std::string textFor(int id) { return id >= 0 ? h.text(id) : "(generated quest text)"; }

        std::string failText()
        {
            if (!textReady())
                return "Quest failed!";
            const int a = txt.npcTier ? txt.npcTier(q.def.giver) : 0;
            return "Quest failed! Go see " + txt.npcName(q.def.giver) + areaPrep(a) + txt.areaName(a) + " to try again.";
        }

        void show(DialogueBox b)
        {
            if (h.portrait && speaker >= 0)
                h.portrait(b, speaker);
            dlg->show(std::move(b));
        }

        void sayStr(std::string s, std::function<void()> next)
        {
            auto b = messageBox(std::move(s));
            b.onYes = std::move(next);
            show(std::move(b));
        }
        void say(int textId, std::function<void()> next) { sayStr(textFor(textId), std::move(next)); }

        void ask(int textId)
        {
            // generic quests: the confirm prompt text is not known yet (placeholder)
            auto b = messageBox(textId >= 0 ? textFor(textId) : std::string("(accept prompt text)"));
            b.kind = BoxKind::Confirm;
            b.onYes = [this]
            { accept(); };
            b.onNo = [] { /* DeclineQuest: quest-1 nudge skipped */ };
            show(std::move(b));
        }

        void intro()
        {
            q.dialogueCall = 0;
            const auto &L = q.def.offerText;
            if (q.def.generic)
            {
                offer({QuestLine::Text, -1, 0});
                return;
            }
            if (L.empty())
                return;
            if (L[0] < 0)
                offer({QuestLine::Action, -1, L[0]});
            else
                offer({L.size() == 1 ? QuestLine::Confirm : QuestLine::Text, L[0], 0});
        }
        void offer(QuestLine l)
        {
            auto next = [this]
            { offer(offerStep(q)); };
            switch (l.kind)
            {
            case QuestLine::Text:
                if (q.def.generic && textReady())
                    sayStr(requestText(q.def, txt), next);
                else
                    say(l.text, next);
                break;
            case QuestLine::Confirm:
                ask(l.text);
                break;
            case QuestLine::Action:
                reward(l.action, next);
                break;
            default:
                break;
            }
        }
        void accept()
        {
            acceptQuest(q, counter, h.rnd, h.held, h.spawn);
            startQuest(q, h.nowTick());
        }

        void turnIn()
        {
            switch (tryTurnIn(q, h.held, h.remove))
            {
            case TurnIn::NoQuest:
                break;
            case TurnIn::Missing:
            {
                const int roll = h.rnd ? h.rnd(1, 4) : 1;
                show(messageBox(reminderText(q.def.typeMode, roll)));
                break;
            }
            case TurnIn::Done:
            {
                h.clearFlagged();
                auto next = [this]
                { advance(advanceStep(q)); };
                const auto &C = q.def.completionText;
                if (q.def.generic)
                    sayStr(h.rnd ? completeText(q.def.typeMode, h.rnd) : std::string("(generated quest text)"), next);
                else if (C.empty())
                    advance({});
                else if (C[0] >= 0)
                    say(C[0], next);
                else
                    reward(C[0], next);
                break;
            }
            }
        }
        void advance(QuestAdvance a)
        {
            auto next = [this]
            { advance(advanceStep(q)); };
            switch (a.kind)
            {
            case QuestAdvance::Text:
                say(a.text, next);
                break;
            case QuestAdvance::Action:
                reward(a.action, next);
                break;
            case QuestAdvance::NextQuest:
                load(counter + 1);
                if (h.onNextQuest)
                    h.onNextQuest();
                if (q.def.giver == q.prevReceiver)
                {
                    speaker = q.def.giver;
                    intro();
                }
                else if (q.def.id >= 0 && textReady())
                    sayStr(hintText(q.def.giver, txt), [] {}); // "go see X" hint
                break;
            }
        }
    };
}