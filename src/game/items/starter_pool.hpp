// starter_pool.hpp - who the starter room (the 12 pets of a new game) may hold, and the colours they come in.
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../../content/db.hpp"
#include "../../loaders/rom.hpp"
#include "../npc/npc_anim_ids.hpp"
#include "../data/profile.hpp"

namespace gp
{

// RollStarterColorFromTable: common table (mode 0) = 18 entries, rare (mode != 0) = 14 entries,
// 0x20 words apart per species. Move this into frontend.hpp once it works.
inline int rollStarterColor(const Rom &rom, int species, int mode, const std::function<int(int)> &randBelow)
{
    if (species > 2)
        return mode;
    const bool rare = mode != 0;
    const int n = rare ? 14 : 18;
    const uint32_t base = (rare ? 0xd180u : 0xd18eu) + 0x20u * uint32_t(species);
    return rom.u16(base + uint32_t(randBelow(n)));
}

// Who the starter room (the 12 pets of a new game) may hold: the species whose records say so (SpeciesDef::starterRoom, and the profile
// keys they require), with the colours they come in there.
struct StarterPool
{
    struct Entry
    {
        int species = 0;
        bool romColours = false; // a base species with no colours of its own: the ROM's tables
        std::vector<int> common, rare;
        int fallback = 0; // the species' default palette
    };
    std::vector<Entry> entries;
    bool romRoom() const // exactly the ROM's three: the room is rolled the way the game does it
    {
        if (entries.size() != 3)
            return false;
        for (size_t i = 0; i < 3; ++i)
            if (entries[i].species != int(i) || !entries[i].romColours)
                return false;
        return true;
    }
};
inline StarterPool makeStarterPool(const ContentDB &db, const std::map<std::string, std::string> &profile)
{
    StarterPool p;
    std::vector<const SpeciesDef *> defs = db.allSpecies();
    std::sort(defs.begin(), defs.end(), [](const SpeciesDef *a, const SpeciesDef *b) { return a->legacyIndex < b->legacyIndex; });
    for (const SpeciesDef *d : defs)
    {
        if (d->legacyIndex < 0 || !knownSpecies(d->legacyIndex) || !starterOffered(*d, profile))
            continue;
        StarterPool::Entry e;
        e.species = d->legacyIndex;
        e.common = d->starterColors;
        e.rare = d->starterRareColors;
        e.fallback = d->defaultPalette;
        e.romColours = d->legacyIndex <= 2 && e.common.empty() && e.rare.empty();
        p.entries.push_back(std::move(e));
    }
    if (p.entries.empty()) // a mod took every species out: the room is never empty
        for (int i = 0; i < 3; ++i)
        {
            StarterPool::Entry e;
            e.species = i;
            e.romColours = true;
            p.entries.push_back(e);
        }
    return p;
}
// A colour for a pet of this entry: its own list (the rare one for a rare pet, else the common one), else the ROM's table, else its default.
inline int poolColour(const StarterPool::Entry &e, bool rare, const std::function<int(int)> &rb, const std::function<int(int species, int mode)> &romTable)
{
    const std::vector<int> &list = rare && !e.rare.empty() ? e.rare : e.common;
    if (!list.empty())
        return list[size_t(rb(int(list.size())))];
    if (e.romColours && romTable)
        return romTable(e.species, rare ? 1 : 0);
    return e.fallback;
}

// The species of the room's 12 pets, in groups of three. The ROM's room (exactly its three species): each group is a permutation of {0,1,2}, rolled the
// way the game does it. With other species in the pool: each group holds three of them, none twice when the pool has three or more.
inline void rollRoomSpecies(const StarterPool &pool, const std::function<int(int)> &rb, int (&sp)[12])
{
    if (pool.romRoom())
    {
        for (int i = 0; i < 12; ++i)
        {
            if (i % 3 == 0)
                sp[i] = rb(3);
            else if (i % 3 == 1)
            {
                int v = rb(3);
                if (v == sp[i - 1])
                {
                    if (rb(2) == 0)
                        v = v + 1 > 2 ? 0 : v + 1;
                    else
                        v = v - 1 < 0 ? 2 : v - 1;
                }
                sp[i] = v;
            }
            else
                sp[i] = 3 - sp[i - 1] - sp[i - 2];
        }
        return;
    }
    const int n = int(pool.entries.size());
    for (int g = 0; g < 4; ++g)
    {
        std::vector<int> bag;
        for (int i = 0; i < n; ++i)
            bag.push_back(pool.entries[size_t(i)].species);
        for (int k = 0; k < 3; ++k)
        {
            const int at = rb(int(bag.size()));
            sp[g * 3 + k] = bag[size_t(at)];
            if (n >= 3)
                bag.erase(bag.begin() + at);
        }
    }
}

} // namespace gp
