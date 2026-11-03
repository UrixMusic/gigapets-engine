// tuning_test.cpp - every tuning default equals the constant (the ROM's or measured value) it replaced, so the base game plays as
// before until a mod sets something. Also: the values are read where they are used.
#include "../engine/tuning.hpp"
#include "../game/encounter_sequence.hpp"
#include "../game/npc_brain.hpp"
#include "../game/npc_stink.hpp"
#include "../game/pending_event.hpp"
#include "../game/pet_wash.hpp"
#include "../game/player.hpp"
#include "../game/quest_nudge.hpp"
#include "../game/sale.hpp"
#include "../game/thought_bubble.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                             \
    do                                                       \
    {                                                        \
        if (!(c))                                            \
        {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while (0)

int main()
{
    using namespace gp;
    const Tuning t;
    CHECK(t.findDudPercent == 15); // PickRandomEventItemId: RandRange(1, 101) < 16
    CHECK(t.digReach == kDigReach && t.holeRingSize == kHoleRingSize && t.randomSpawnBatch == kRandomSpawnBatch && t.spawnRetries == kSpawnRetries);
    CHECK(t.maxMoney == kMaxMoney && t.maxCare == kMaxCare && t.maxPersonality == kMaxPersonality && t.negativeCareMinQuests == kNegativeCareMinQuests);
    CHECK(t.sellFactor == kSellFactor);
    CHECK(t.washHygiene == kWashHygiene && t.washMoodLimit == kWashMoodLimit && t.washMoodBonus == kWashMoodBonus);
    CHECK(t.stinkHygieneDrop == kStinkHygieneDrop && t.stinkCountNeeded == kStinkCountNeeded && t.stinkCheckTicks == kStinkCheckTicks);
    CHECK(t.idleMinTicks == kIdleMinTicks && t.idleMaxTicks == kIdleMaxTicks);
    CHECK(t.approachDX == EncounterSequence::kApproachDX && t.closerDX == EncounterSequence::kCloserDX &&
          t.awayDX == EncounterSequence::kAwayDX && t.giveUpTicks == EncounterSequence::kGiveUpTicks);
    CHECK(t.nudgeFirstSeconds == kNudgeFirstSeconds && t.nudgeRepeatSeconds == kNudgeRepeatSeconds);
    CHECK(t.nudgeQuests == kNudgeQuestsCompleted && t.nudgeNpc == kNudgeMayorNpc && t.decayAfterQuests == kDecayAfterQuests);
    CHECK(romToSim(t.tiredDecayRomTicks) == kTiredDecayLimit && romToSim(t.hungerDecayRomTicks) == kHungerDecayLimit);
    CHECK(t.tickHz == 30.0 && t.camY == 145 && t.probeDY == -4); // (these three live in walk.cpp)

    { // the decay start and the reminder's quest count are read where they are used
        long a = 0;
        CHECK(!decayStep(a, 1, 3) && decayStep(a, 1, 4));
        tuning().decayAfterQuests = 6;
        a = 0;
        CHECK(!decayStep(a, 1, 6) && decayStep(a, 1, 7));
        tuning().decayAfterQuests = 3;
        QuestNudge n;
        CHECK(QuestNudge::applies(1, 0) && !QuestNudge::applies(2, 0));
        tuning().nudgeQuests = 2;
        CHECK(QuestNudge::applies(2, 0) && !QuestNudge::applies(1, 0) && !QuestNudge::applies(2, 1));
        n.restart(0);
        CHECK(n.due(60 * 31, 2, 0, -1) && !n.due(60 * 32, 2, 0, -1)); // 30 s, then the repeat limit
        tuning().nudgeQuests = 1;
        tuning().nudgeNpc = 7;
        n.restart(0);
        CHECK(!n.due(60 * 31, 1, 0, 7));   // beside the chosen npc: no reminder
        CHECK(n.due(60 * 31, 1, 0, 0x42)); // and the old one no longer counts
        tuning().nudgeNpc = 0x42;
    }

    // the care and money caps are read where they are applied
    Player p;
    p.money = 0;
    p.adjustMoney(1000000);
    CHECK(p.money == 99999);
    tuning().maxMoney = 500000;
    p.adjustMoney(1000000);
    CHECK(p.money == 500000);
    tuning().maxMoney = 99999;
    p.adjustCare(Cleanliness, -1000); // below the quest gate this is ignored, above it clamps at 0
    p.questsCompleted = 10;
    p.adjustCare(Cleanliness, 1000);
    CHECK(p.care[Cleanliness] == 100);
    tuning().maxCare = 250;
    p.adjustCare(Cleanliness, 1000);
    CHECK(p.care[Cleanliness] == 250);
    tuning().maxCare = 100;

    { // the stat filter (a mod's onStatChange seam): money, care and personality all pass through it
        Player q;
        q.questsCompleted = 10;
        int calls = 0, lastKind = -1, lastStat = -1;
        q.deltaFilter = [&](int kind, int stat, int delta)
        {
            ++calls;
            lastKind = kind;
            lastStat = stat;
            return kind == 2 ? delta * 2 : kind == 1 ? 0 : delta + 1;
        };
        q.money = 100;
        q.adjustMoney(10);
        CHECK(q.money == 120 && lastKind == 2);
        q.care[Hunger] = 50;
        q.adjustCare(Hunger, -4); // -3 after the filter
        CHECK(q.care[Hunger] == 47 && lastKind == 0 && lastStat == int(Hunger));
        q.personality[Stars] = 7;
        q.adjustPersonality(Stars, 5); // cancelled
        CHECK(q.personality[Stars] == 7 && lastKind == 1 && lastStat == int(Stars));
        CHECK(calls == 3);
        q.deltaFilter = nullptr;
        q.adjustMoney(10);
        CHECK(q.money == 130);
    }
    if (fails)
        std::printf("%d check(s) failed\n", fails);
    else
        std::printf("tuning_test: all passed\n");
    return fails ? 1 : 0;
}
