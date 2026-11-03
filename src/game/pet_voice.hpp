// pet_voice.hpp - PlaySoundFromCategory (0x018e70): the sound a pet (or an NPC pet) makes for a reaction.
// g_soundEffectTable at word 0xd1e0 is 13 sound ids per species (0..2), indexed by category; the call is
// PlaySoundEffect(table[species*13 + category], volume, 0).
// Categories seen in the ROM: 1/2 idle noise, 3 bark, 4 positive, 5/6 chew (RepeatAnimThenPause), 7 negative,
// 8 bored, 9 emote, 0xa cure illness (the gulp), 0xb sad, 0xc refusal.
#pragma once
#include "npc_anim_ids.hpp" // SpeciesArt::voice holds the sound ids (the base species' are the ROM's table at word 0xd1e0)

namespace gp
{
    // The sound effect id for (species, category), or -1 for a category outside the table. A species number past the last one uses the last.
    inline int petVoiceId(int species, int category)
    {
        if (category < 0 || category >= kPetVoiceCategories)
            return -1;
        const std::vector<SpeciesArt> &v = speciesArts();
        const int s = species < 0 ? 0 : size_t(species) >= v.size() ? int(v.size()) - 1 : species;
        return v[size_t(s)].voice[category];
    }
}
