// tuning.hpp - the game's gameplay numbers as variables. Each default is the value the ROM (or the measurements behind it) gave;
// nothing changes it but a mod (engine.set("tuning.digReach", 40)), so the base game plays exactly as before.
// The constants these came from (kDigReach, kMaxCare, ...) stay where they were as the documented defaults, and
// tests/tuning_test.cpp fails if a default here drifts from its constant.
//
// Not here on purpose: things that size arrays or screens (trick slots, code lengths), ROM addresses, sprite and sound ids, and
// pixel layouts. Tables of content (find weights, prize pools, stat gates) belong in records, not in numbers.
#pragma once

namespace gp
{
    struct Tuning
    {
        // game speed and the camera
        double tickHz = 30.0; // simulation steps a second (the game's own rate); scripts' 30 Hz hooks follow it
        int camY = 145;       // the camera sits this far above the pet (cam.y = pos.y - camY)
        int probeDY = -4;     // collision is tested this many pixels above the pet's feet

        // digging and finds
        int digReach = 30;         // px around the pet that a dig reaches
        int holeRingSize = 5;      // holes that stay open before the oldest fills in
        int randomSpawnBatch = 6;  // random finds placed on entering an area
        int spawnRetries = 200;    // tries to find a free spot for a random find
        int findDudPercent = 15;   // chance in 100 that a random find is nothing (a dud)

        // money, care and personality
        int maxMoney = 99999;
        int maxCare = 100;
        int maxPersonality = 9999;
        int negativeCareMinQuests = 4; // care stats ignore drops until this many quests are done
        float sellFactor = 0.8f;       // what the seller pays, of the price

        // washing and smelly neighbours
        int washHygiene = 100;     // what a wash adds to Cleanliness
        int washMoodLimit = 0x4a;  // a wash cheers the pet up if Happiness is at most this
        int washMoodBonus = 10;
        int stinkHygieneDrop = 0x50; // what catching a smell costs a clean pet
        int stinkCountNeeded = 45;   // checks beside a smelly NPC before the smell passes on
        int stinkCheckTicks = 23;    // steps between those checks (45 ROM ticks)

        // NPC behaviour
        int idleMinTicks = 30; // how long an idle NPC stands before it acts again (steps, 30 a second)
        int idleMaxTicks = 91; // exclusive
        int approachDX = 0x4b; // befriending: the pet stands this far from the NPC
        int closerDX = 0x14;   // ... steps this much closer for a friend
        int awayDX = 0x1e;     // ... walks this far away from one that is not
        int giveUpTicks = 120; // how long the pet tries to get there

        // the stats wearing down
        int decayAfterQuests = 3;        // hunger and sleepiness only wear down once MORE than this many quests are done
        int tiredDecayRomTicks = 20000;  // between two drops of Sleepiness, in the ROM's 59.94 Hz ticks (about 5.6 minutes)
        int hungerDecayRomTicks = 4000;  // ... of Hunger (about 67 seconds)

        // quest reminder
        int nudgeQuests = 1; // shows while exactly this many quests are done (the game counts from 1) and none is active
        int nudgeNpc = 0x42; // the base NPC (record number 0..77) it sends the pet to: no reminder beside it, and its portrait
        double nudgeFirstSeconds = 30.0;
        double nudgeRepeatSeconds = 180.0;
    };

    inline Tuning &tuning()
    {
        static Tuning t;
        return t;
    }
} // namespace gp
