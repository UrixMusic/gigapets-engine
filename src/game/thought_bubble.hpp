// thought_bubble.hpp - the "I'm hungry" and "I'm tired" bubbles over the pet in the overworld.
//
// TickCareStatWatchdog (0x04cd30) keeps one timer per condition. Condition 4 (the food bowl bubble, icon tile 0x13e) and
// condition 6 (the ZZZ bubble, icon 0x142) wake when their timer reaches its limit, then: a pet that is walking (state 2)
// is retried 1500 ROM ticks (25 s) later; otherwise, if the stat is low enough (Hunger <= 0x28, Sleepiness <= 0x19) and the
// pet has no bubble up, SpawnMessageBubble(0x18, ...) puts one over it. Hunger sets the next limit from how hungry the pet
// is (2, 4, 8, 16 or 32 minutes); tired leaves its limit alone. Both timers count 59.94 Hz ROM ticks; here they count the
// 30 Hz sim steps (romToSim).
//
// The bubble (UpdateBubbleStateMachine 0x049dc7, DrawActiveBubbles 0x04a0cc): anim 0x18 grows it once, anim 0x17 loops
// with the icon drawn inside at (+0x20, -0x23) until 5.0 s have passed since it spawned, then 0x18 plays backwards. When it
// has gone, a pet that is idle and has never seen it gets the tutorial message (event 13 hungry, 14 tired).
//
// INFERRED: where the tired timer starts and what its limit is (the ROM never writes it, so 0 as far as the code shows).
#pragma once
#include <algorithm>
#include "settings.hpp"
#include "../engine/tuning.hpp"
#include "npc_anim_ids.hpp" // the species records: bubble offsets, decay amounts

namespace gp
{

    constexpr int kThoughtHungerIcon = 0x13e, kThoughtTiredIcon = 0x142;
    constexpr int kThoughtHungerAtMost = 0x28, kThoughtTiredAtMost = 0x19;
    constexpr int kThoughtAnimGrow = 0x18, kThoughtAnimHold = 0x17, kThoughtFps = 7;
    constexpr int kThoughtIconDX = 0x20, kThoughtIconDY = -0x23;
    constexpr int kTutorialHungry = 13, kTutorialTired = 14;

    constexpr long romToSim(long romTicks) { return (romTicks * 3000 + 2997) / 5994; } // x 30 / 59.94, rounded
    constexpr long kThoughtRetryTicks = romToSim(1500);                              // 25 s
    constexpr int kThoughtShowTicks = 150;                                           // 5.0 s from the spawn

    // The hunger timer's next limit: the hungrier, the sooner (0x1c20, 0x3840, 0x7080, 0xe100, 0x1c200 ROM ticks).
    constexpr long hungerInterval(int hunger)
    {
        return romToSim(hunger <= 9 ? 7200 : hunger <= 0x13 ? 14400 : hunger <= 0x1d ? 28800 : hunger <= 0x27 ? 57600 : 115200);
    }

    struct ThoughtTimer
    {
        long accum = -kThoughtRetryTicks, limit = 0; // the hunger timer starts 25 s from firing
    };
    // The tired timer's limit, read from RAM 0x1ec2 in MAME: 0xd2f0 ROM ticks (about 14.7 min; the high word was not read).
    constexpr long kTiredLimit = romToSim(0xd2f0);

    // One sim step of the hunger timer. True = put the bubble up now.
    inline bool hungerBubbleStep(ThoughtTimer &t, int hunger, bool walking, bool bubbleUp)
    {
        if (++t.accum < t.limit)
            return false;
        if (hunger <= kThoughtHungerAtMost && walking) // busy: look again in 25 s
        {
            t.accum = t.limit - kThoughtRetryTicks;
            return false;
        }
        const bool spawn = hunger <= kThoughtHungerAtMost && !bubbleUp;
        t.accum = 0;
        t.limit = hungerInterval(hunger);
        return spawn;
    }

    inline bool tiredBubbleStep(ThoughtTimer &t, int sleepiness, bool walking, bool bubbleUp)
    {
        if (++t.accum < t.limit)
            return false;
        if (walking)
        {
            t.accum = t.limit - kThoughtRetryTicks;
            return false;
        }
        const bool spawn = sleepiness <= kThoughtTiredAtMost && !bubbleUp;
        if (sleepiness > kThoughtTiredAtMost || spawn) // a pet already showing a bubble keeps its count
            t.accum = 0;
        return spawn;
    }

    // Condition 5, the sick bubble (icon 0x141): a sick pet at rest. Walking retries in 25 s; a healthy pet just restarts the count.
    constexpr int kThoughtSickIcon = 0x141, kThoughtMoodIcon = 0x140, kThoughtMoodAtMost = 0x19;
    // Both limits read from RAM in MAME (0x1ebd and 0x1ec9): 0xe10 ROM ticks (60 s). Only the low word was read.
    constexpr long kSickLimit = romToSim(0xe10), kMoodLimit = romToSim(0xe10);
    inline bool sickBubbleStep(ThoughtTimer &t, bool sick, bool walking, bool bubbleUp)
    {
        if (++t.accum < t.limit)
            return false;
        if (walking)
        {
            t.accum = t.limit - kThoughtRetryTicks;
            return false;
        }
        t.accum = 0;
        return sick && !bubbleUp;
    }
    // Condition 7, the unhappy bubble (icon 0x140): Mood (Happiness) at 0x19 or below. A walking pet just restarts the count.
    inline bool moodBubbleStep(ThoughtTimer &t, int mood, bool walking, bool bubbleUp)
    {
        if (++t.accum < t.limit)
            return false;
        t.accum = 0;
        return !walking && mood <= kThoughtMoodAtMost && !bubbleUp;
    }

    // ---- where a bubble hangs: GetAnimFrameOffset_A (0x049bbe, the thought bubbles) and _B (0x049abb, the overhead loop) ----
    // The offset from the pet's feet. dir: 0 facing left, 1 facing right, 2 up or down. Species 0 is the dog.
    struct BubbleOffset
    {
        int dx, dy;
    };
    // The record lists the animations that lift a bubble further (the panda's raised art: anims 0xed, 0xee, 0xf0..0xf3, 0xe4, 0xe5).
    inline int bubbleRaised(const SpeciesArt &art, int anim)
    {
        return std::find(art.raisedAnims.begin(), art.raisedAnims.end(), anim) != art.raisedAnims.end() ? art.bubbleRaise : 0;
    }
    inline bool pandaRaised(int anim) { return bubbleRaised(artOf(2), anim) > 0; } // (the third base species' record)
    inline BubbleOffset thoughtOffset(int species, int dir, int anim)
    {
        const SpeciesArt &art = artOf(species);
        return {art.thoughtDX[dir == 1 ? 1 : dir == 0 ? 0 : 2], art.thoughtDY - bubbleRaised(art, anim)};
    }
    inline BubbleOffset overheadOffset(int species, int dir, int anim)
    {
        const SpeciesArt &art = artOf(species);
        const int dx = dir == 1 ? 0x12 : dir == 0 ? -0x12 : 0;
        return {dx, art.overheadDY - bubbleRaised(art, anim)};
    }

    // ---- the stink cloud (UpdateIdleBehaviorTick 0x0148d8): Cleanliness below 0x1a keeps anim 6 looping over the pet ----
    constexpr int kStinkAnim = 6, kStinkFps = 14, kStinkBelow = 0x1a;
    // Pixels to move every overhead bubble down, tuned by eye (both the cloud and the thought bubbles sat about 10 px high with
    // the ROM offsets, so the pet's feet are probably not quite where its ROM position is).
    // It differs by species, all tuned by eye: dog 15, monkey 20, panda 21.
    inline int bubbleLower(int species) { return artOf(species).bubbleLower; }

    // ---- the personality bubbles (UpdateIdleBehaviorTick 0x0148d8) ----
    // With the pet clean (a stinky pet shows the cloud instead), one bubble loops over its head when its personality leans far
    // enough: Halos more than 299 above Pitchforks -> bubble 7; Pitchforks more than 299 above Halos -> bubble 0xd; neither, and
    // Stars above 299 -> bubble 0xf. Each plays once at 14 fps and is put up again the same tick, so it loops. -1 = none.
    constexpr int kPersonalityBubbleLead = 299, kPersonalityBubbleFps = 14;
    constexpr int kHaloBubble = 7, kPitchforkBubble = 0xd, kStarBubble = 0xf;
    constexpr int personalityBubble(int halos, int pitchforks, int stars)
    {
        if (halos - pitchforks > kPersonalityBubbleLead)
            return kHaloBubble;
        if (pitchforks - halos > kPersonalityBubbleLead)
            return kPitchforkBubble;
        return stars > kPersonalityBubbleLead ? kStarBubble : -1;
    }

    // ---- the stats wearing down (TickCareStatWatchdog conditions 1 and 2) ----
    // Once more than 3 quests are done, every 20000 ROM ticks (about 5.6 min) Sleepiness drops, and every 4000 (67 s) Hunger
    // drops. The amount depends on the species (RAM 0x1a90 = g_playerSpecies): 2 for species 0 on sleepiness, 2 for species 2
    // on hunger, 1 otherwise. Limits read from RAM 0x1e9f and 0x1ea6 in MAME. The count restarts whether or not it dropped.
    constexpr long kTiredDecayLimit = romToSim(20000), kHungerDecayLimit = romToSim(4000);
    constexpr int kDecayAfterQuests = 3; // the default of tuning().decayAfterQuests
    inline int tiredDecayAmount(int species) { return artOf(species).tiredDecay; }
    inline int hungerDecayAmount(int species) { return artOf(species).hungerDecay; }
    inline bool decayStep(long &accum, long limit, int questsCompleted)
    {
        if (++accum < limit)
            return false;
        accum = 0;
        return questsCompleted > tuning().decayAfterQuests;
    }
    // Falling into "hungry" brings the hungry bubble round in 25 s; the tired bubble gets that after every drop.
    constexpr bool crossedHungry(int before, int after) { return before > kThoughtHungerAtMost && after <= kThoughtHungerAtMost; }

    // Which frame of which anim is showing `ticks` steps after the spawn (growFrames = frames of 0x18, holdFrames of 0x17).
    struct ThoughtView
    {
        int anim = kThoughtAnimGrow, frame = 0;
        bool icon = false, done = false;
    };
    constexpr int thoughtStepsFor(int frames) { return (frames * 30 + kThoughtFps - 1) / kThoughtFps; }
    inline ThoughtView thoughtView(int ticks, int growFrames, int holdFrames)
    {
        ThoughtView v;
        const int grow = thoughtStepsFor(growFrames);
        if (ticks < grow && ticks < kThoughtShowTicks)
        {
            v.frame = ticks * kThoughtFps / 30;
            return v;
        }
        if (ticks < kThoughtShowTicks)
        {
            v.anim = kThoughtAnimHold;
            v.icon = true;
            v.frame = holdFrames > 0 ? (ticks - grow) * kThoughtFps / 30 % holdFrames : 0;
            return v;
        }
        const int back = ticks - kThoughtShowTicks;
        if (back >= grow)
        {
            v.done = true;
            v.frame = 0;
            return v;
        }
        v.frame = growFrames - 1 - back * kThoughtFps / 30; // played backwards
        return v;
    }

    // When the bubble has gone: the tutorial for it, if the pet is resting and has not seen it. -1 = none.
    constexpr int thoughtTutorial(bool on, unsigned shown, int icon, bool idle)
    {
        if (!idle)
            return -1;
        if (icon == kThoughtHungerIcon && isFirstTime(on, shown, kTutorialHungry))
            return kTutorialHungry;
        if (icon == kThoughtTiredIcon && isFirstTime(on, shown, kTutorialTired))
            return kTutorialTired;
        return -1;
    }

} // namespace gp
