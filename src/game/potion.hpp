// potion.hpp - colour potions (item type 16, items 18..22).
//
// ItemMenuScreen (0x02c610), item type 0x10: the item's w9 (0..4) picks the colour, and the pet's appearance becomes
// ROM[0xb061 + species * 5 + w9] (SetPetDisplayAppearanceId 0x018f39, also saved). Nothing in that branch removes the item,
// so a potion is not used up. Back in the world the pet runs activity 0x13 (TickActor_ShockedEncounterSequence): it turns
// to face front, plays its emote, an exclamation bubble (anim 0xb, then 0xc, rate 12) appears over its head, and after a
// half second it plays the shocked reaction. A wash puts the original colour back (pet_wash.hpp).
#pragma once
#include <cstdint>

namespace gp
{

    constexpr int kPotionActivity = 0x13;
    constexpr int kPotionCount = 5, kPotionSpecies = 3;
    constexpr int kExclaimIntroAnim = 0xb, kExclaimOutroAnim = 0xc, kExclaimFps = 12;

    // The ROM's table at 0xb061, species-major.
    constexpr uint16_t kPotionPalettes[kPotionSpecies][kPotionCount] = {
        {0x1df, 0x1e4, 0x1ec, 0x1f9, 0x1fe},
        {0x29e, 0x2a3, 0x2ab, 0x2b8, 0x2bd},
        {0x31e, 0x323, 0x32b, 0x338, 0x33d},
    };

    // The appearance a potion gives a pet of this species; -1 for a potion number or species outside the table.
    constexpr int potionPalette(int species, int potion)
    {
        return species >= 0 && species < kPotionSpecies && potion >= 0 && potion < kPotionCount ? kPotionPalettes[species][potion] : -1;
    }

    // Game ticks until the exclamation has finished: the intro, then the outro, each once at kExclaimFps.
    constexpr int exclaimSteps(int frames) { return frames <= 0 ? 0 : (frames * 30 + kExclaimFps - 1) / kExclaimFps; }

    struct ExclaimView
    {
        int anim = kExclaimIntroAnim, frame = 0;
        bool done = false;
    };
    constexpr ExclaimView exclaimView(int ticks, int introFrames, int outroFrames)
    {
        ExclaimView v;
        const int intro = exclaimSteps(introFrames), outro = exclaimSteps(outroFrames);
        if (ticks < intro)
        {
            v.frame = ticks * kExclaimFps / 30;
            return v;
        }
        v.anim = kExclaimOutroAnim;
        if (ticks - intro >= outro)
        {
            v.done = true;
            return v;
        }
        v.frame = (ticks - intro) * kExclaimFps / 30;
        return v;
    }

} // namespace gp
