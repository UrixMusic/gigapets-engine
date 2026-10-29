// quest_nudge.hpp - "Go see Mayor Peter!": the reminder that pops up while the first quest has not been taken.
//
// TickQuestProgressCheck (0x019d4c): while exactly one quest has been completed (g_totalQuestsCompleted starts at 1, so this is
// the start of a new game) and none is active, the pet is free, and the pet is not standing by Mayor Peter (NPC 0x42), a timer
// runs. When it passes its limit the pet runs activity 0x26 with event -1 (TickFirstNPCEncounterSequence): it turns to the front,
// and after a quarter second the message "Go see Mayor Peter!" (ROM string 0xfa31) opens with Peter's portrait. The limit is then
// 180 s, the timer restarted. The timer is restarted with a 30 s limit by DeclineQuest and by every area change that happens
// under the same conditions (Game_ProcessTick 0x01af62).
#pragma once

namespace gp
{

    constexpr int kNudgeMayorNpc = 0x42, kNudgeQuestsCompleted = 1;
    constexpr double kNudgeFirstSeconds = 30.0, kNudgeRepeatSeconds = 180.0;
    constexpr int kNudgeMessageAddr = 0xfa31; // "Go see Mayor Peter!"
    constexpr int kGameTicksPerSecond = 60;   // the game's tick counter is +2 per 30 Hz step

    struct QuestNudge
    {
        long start = 0;
        double limit = kNudgeFirstSeconds;

        // Restart the timer with the first (30 s) limit.
        void restart(long nowTicks)
        {
            start = nowTicks;
            limit = kNudgeFirstSeconds;
        }
        static bool applies(int questsCompleted, int questPhase) { return questsCompleted == kNudgeQuestsCompleted && questPhase == 0; }

        // Call every tick the pet is free. `nearNpc` is the NPC the pet is beside (-1 for none). True when the reminder is due now;
        // the timer has then been restarted with the 180 s limit.
        bool due(long nowTicks, int questsCompleted, int questPhase, int nearNpc)
        {
            if (!applies(questsCompleted, questPhase) || nearNpc == kNudgeMayorNpc)
                return false;
            if (double(nowTicks - start) / kGameTicksPerSecond <= limit)
                return false;
            start = nowTicks;
            limit = kNudgeRepeatSeconds;
            return true;
        }
    };

} // namespace gp
