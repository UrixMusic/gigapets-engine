// pet_voice.hpp - PlaySoundFromCategory (0x018e70): the sound a pet (or an NPC pet) makes for a reaction.
// g_soundEffectTable at word 0xd1e0 is 13 sound ids per species (0..2), indexed by category; the call is
// PlaySoundEffect(table[species*13 + category], volume, 0).
// Categories seen in the ROM: 1/2 idle noise, 3 bark, 4 positive, 5/6 chew (RepeatAnimThenPause), 7 negative,
// 8 bored, 9 emote, 0xa cure illness (the gulp), 0xb sad, 0xc refusal.
#pragma once
#include <algorithm>

namespace gp
{
    constexpr int kPetVoiceCategories = 13;

    constexpr int kPetVoice[3][kPetVoiceCategories] = {
        {0x03, 0x04, 0x05, 0x0c, 0x0d, 0x07, 0x08, 0x0a, 0x10, 0x11, 0x0f, 0x0e, 0x0b},
        {0x28, 0x29, 0x2a, 0x25, 0x26, 0x07, 0x08, 0x23, 0x2b, 0x2c, 0x0f, 0x27, 0x24},
        {0x3c, 0x3d, 0x3e, 0x33, 0x34, 0x2f, 0x30, 0x31, 0x40, 0x41, 0x3f, 0x3b, 0x32},
    };

    // The sound effect id for (species, category), or -1 for a category outside the table.
    constexpr int petVoiceId(int species, int category)
    {
        if (category < 0 || category >= kPetVoiceCategories)
            return -1;
        const int s = species < 0 ? 0 : species > 2 ? 2 : species;
        return kPetVoice[s][category];
    }
}
