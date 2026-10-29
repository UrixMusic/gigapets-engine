// pet_wash.hpp - washing the pet: menu bar button 4 (outside and in a house) starts pet activity 0x27,
// HandlePetWashInteraction (0x015da2). Three stages:
//   1. TickActor_ReactionState: a sitting pet gets up first. Then SpawnMessageBubble(anim 0xe, palette 0x28c, at the pet,
//      rate 14, one-shot) and the pet idles.
//   2. When the bubble animation has finished: AdjustCareStat(Cleanliness, 100); the displayed colour goes back to the
//      pet's ORIGINAL colour (g_petDisplayAppearanceIdSaved = g_starterExtraField), undoing any colour potion; saved.
//   3. PlayPositiveReactionAnim(pet, 3 repeats, variant 2). Then, if Happiness <= 0x4a (cmp r1,0x4a / jle at 0x015ec0):
//      AdjustCareStat(Happiness, 10). The pet goes back to normal.
// No money, no item: it is free and can be repeated.
#pragma once
#include "player.hpp"

namespace gp
{

    constexpr int kWashActivity = 0x27;                          // StartPetActivity id from the bar
    constexpr int kWashBubbleAnim = 0xe, kWashBubbleFps = 14;    // SpawnMessageBubble(0xe, 0x28c, ..., rate 0xe)
    constexpr int kWashHygiene = 100;                            // AdjustCareStat(5, 100): a full wash
    constexpr int kWashHappyRepeats = 3, kWashHappyVariant = 2;  // PlayPositiveReactionAnim(pet, 3, 2)
    constexpr int kWashMoodLimit = 0x4a, kWashMoodBonus = 10;    // AdjustCareStat(3, 10) when happiness <= 0x4a

    // Stage 2, when the bubble is done.
    inline void applyWash(Player &p)
    {
        p.adjustCare(Cleanliness, kWashHygiene);
        p.washPet(); // appearance = the original colour
    }

    // Stage 3, after the happy reaction.
    inline void applyWashMood(Player &p)
    {
        if (p.care[Happiness] <= kWashMoodLimit)
            p.adjustCare(Happiness, kWashMoodBonus);
    }

    // Game ticks (30 Hz) until a one-shot bubble animation of `frames` frames has finished: the frame number is
    // ticks * fps / 30 and the animation is done once it passes the last frame.
    inline int bubbleTicks(int frames, int fps = kWashBubbleFps)
    {
        if (frames <= 0)
            return 0;
        int t = 0;
        while (t * fps / 30 < frames)
            ++t;
        return t;
    }

    // The colour to display after a wash: the original colour, or the current one when none is known (a run without a
    // save has no original colour recorded).
    inline int washedPalette(const Player &p, int currentPalette)
    {
        return p.appearance > 0 ? p.appearance : currentPalette;
    }

} // namespace gp
