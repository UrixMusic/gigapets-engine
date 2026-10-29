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
        std::function<std::string(int)> text;                  // text id -> final string (pet name applied)
        std::function<int(const Id &)> held;                   // quest.hpp:117, 145, 185
        std::function<void(const Id &, int)> remove;           // quest.hpp:152
        std::function<void(int, const Id &, int, bool)> spawn; // quest.hpp:115 (areaTier, item, st, flag)
        std::function<void(const Id &, int, int, bool)> place; // already done
        std::function<void()> randomEvent;                     // the extra random event after a -2 reward
        std::function<void()> clearFlagged;                    // ConsumeAllFlaggedWorldItems
        std::function<void(int)> sfx;
        // StartQuestRewardActivity: the pet is held for `ticks` sim steps (30 Hz) before `done` runs. Pressing Z ends the hold
        // early. Unset = no hold (done runs at once).
        std::function<void(int ticks, std::function<void()> done)> hold;
        std::function<int()> camX, camY;
        std::function<int()> receiverY; // y of the live receiver NPC (return camY if it isn't in the area)
        std::function<int()> nowTick;
        std::function<void(DialogueBox &, int)> portrait; // set the speaker portrait on a box (pool id)
        std::function<void()> onNextQuest;                // fired after the next quest has loaded
        std::function<void()> onDecline;                  // DeclineQuest: the player said no to a quest
        // Fired when the receiver takes the hand-in (objectives already removed), before the completion dialogue.
        // Returns true to skip the quest's built-in bonus rewards (the -2 / -7 activities); the thank-you text and
        // moving on to the next quest still happen. Unset = false.
        std::function<bool()> onComplete;
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
            skipRewards_ = false;
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
            if (skipRewards_) // a script's onQuestComplete took the rewards over
            {
                done();
                return;
            }
            // The pet is held while the activity runs (RunNPCBefriendEncounter 0x016521 / TickActor_BefriendSuccessRewardSequence
            // 0x016701): 0.5 s first, then the items drop, and the bonus one (0x18) holds it 1.5 s more. Z cuts a wait short.
            auto after = [this](int ticks, std::function<void()> f)
            {
                if (h.hold)
                    h.hold(ticks, std::move(f));
                else
                    f();
            };
            switch (code)
            {
            case -2: // 0x18: bonus table scattered, unflagged
                after(kRewardLeadTicks, [this, done, after]
                      {
                    for (const auto &s : scatter(q.def.bonus, false, h.camX(), h.camY(), h.receiverY(), h.rnd))
                        h.place(s.item, s.x, s.y, s.flagged);
                    if (counter != 0x17 && h.rnd(1, 0x65) < 0x33)
                        h.randomEvent();
                    h.sfx(0x54);
                    after(kBonusTailTicks, done); });
                return;
            case -7: // 0x1f: spawn table dropped near the player, flagged
                after(kRewardLeadTicks, [this, done]
                      {
                    for (const auto &s : scatter(q.def.spawn, true, h.camX(), h.camY(), h.receiverY(), h.rnd))
                        h.place(s.item, s.x, s.y, s.flagged);
                    h.sfx(0x54);
                    done(); });
                return;
            default:
                break; // -8..-11: minipet cartridge sequences, not ported
            }
            done();
        }
        static constexpr int kRewardLeadTicks = 15; // 0.5 s (constant 0x3f000000 in both activities)
        static constexpr int kBonusTailTicks = 45;  // 1.5 s (0x3fc00000) after the bonus drop

    private:
        int speaker = -1; // pool id whose portrait goes on the boxes
        bool skipRewards_ = false; // set at turn-in when onComplete returned true; cleared when the next quest loads

        bool textReady() const { return txt.npcName && txt.areaName && txt.itemName && txt.rnd; }

        std::string textFor(int id) { return id >= 0 ? h.text(id) : txt.s("placeholder_generated"); }

        std::string failText()
        {
            if (!textReady())
                return txt.s("quest_failed_short");
            const int a = txt.npcTier ? txt.npcTier(q.def.giver) : 0;
            return fillTemplate(txt.s("quest_failed"), {{"npc", txt.npcName(q.def.giver)}, {"prep", txt.prep(a)}, {"area", txt.areaName(a)}});
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
            auto b = messageBox(textId >= 0 ? textFor(textId) : acceptText(txt));
            b.kind = BoxKind::Confirm;
            b.onYes = [this]
            { accept(); };
            // DeclineQuest: the giver says it is fine, come back later (and the reminder timer starts, quest_nudge.hpp)
            b.onNo = [this]
            {
                if (h.onDecline)
                    h.onDecline();
                sayStr(declineText(txt), [] {});
            };
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
            acceptQuest(q, counter, h.rnd, h.held, h.spawn, world.type(q.def.typeMode).spawnsOnAccept);
            showObjectives();
        }

        // After Yes (ProcessQuestTurnIn, its last part): the giver's objective screen, type 7 (DrawQuestObjectiveScreen). The
        // list is "Find:" with the objectives, or "Take:" with the spawn table when there are no objectives; then "Bring To:" the
        // receiver ("Me" when it is the giver) and, if there is a list, "Look In:" the quest's area. The HUD and the timer
        // start once it is closed (StartQuest).
        void showObjectives()
        {
            // StartQuestWithPayment (the callback when the quest has a spawn table, like the food quests' money bag): the HUD
            // comes on and reward -7 drops the spawn table near the pet. Without one, just StartQuest.
            auto start = [this]
            {
                startQuest(q, h.nowTick());
                if (!q.def.spawn.empty())
                    reward(-7, [] {});
            };
            if (!textReady() || !dlg)
            {
                start();
                return;
            }
            DialogueBox b;
            b.kind = BoxKind::Quest;
            b.layout = 2;
            const bool take = q.def.objectives.empty() && !q.def.spawn.empty();
            const auto &rows = take ? q.def.spawn : q.def.objectives;
            b.questHeading = txt.s(take ? "objective_take" : "objective_find");
            for (size_t k = 0; k < rows.size() && k < 3; ++k)
                b.questRows.emplace_back(rows[k].count, txt.itemName(rows[k].item, rows[k].count));
            if (q.def.giver == q.def.receiver)
                b.questBringTo = txt.s("objective_me");
            else
            {
                const int t = txt.npcTier ? txt.npcTier(q.def.receiver) : 0;
                b.questBringTo = txt.npcName(q.def.receiver) + " " + txt.prep(t) + " " + txt.areaName(t);
            }
            if (!q.def.objectives.empty())
                b.questLookIn = txt.areaName(q.def.areaTier);
            b.onYes = start;
            show(std::move(b));
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
                show(messageBox(txt.s(reminderKey(world.type(q.def.typeMode), roll))));
                break;
            }
            case TurnIn::Done:
            {
                skipRewards_ = h.onComplete && h.onComplete();
                h.clearFlagged();
                auto next = [this]
                { advance(advanceStep(q)); };
                const auto &C = q.def.completionText;
                if (q.def.generic)
                    sayStr(txt.rnd ? completeText(q.def.typeMode, txt) : txt.s("placeholder_generated"), next);
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