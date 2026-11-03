// color_machine.hpp - the colour-change machine in the spaceship (area 8, object 1).
//
// Z at the machine starts activity 0x29 (TickPetColorMachineSequence 0x015f9c): the pet turns to face front, the machine finds
// the pet's colour among the 36 presets (ResyncStarterCandidateIndex 0x0191ba), plays sound 0x5c, and the pet's colour then
// cycles through the presets every 8 ROM ticks (ScrollStarterCandidateSelection 0x019200, run by MiniPet_HandleOverworldInput
// while the pet is in state 8). Each use runs the opposite way round from the last one (the first goes forward). It runs only
// while Z is HELD: the activity starts the scrolling only if the action button is still down once the pet faces front (bit 0x10
// of IO_CartInterfaceStatus, read as the button's state), and state 8 ends on the button's RELEASE event (event 4 in the up
// list of MiniPet_HandleOverworldInput). Every step sets the base colour (g_starterExtraField), the shown one and the saved
// one, so a wash afterwards brings back THIS colour.
//
// INFERRED: that IO bit being the action button (it fits: held = scroll, let go = stop).
#pragma once
#include <functional>
#include <vector>
#include "../content/records.hpp"
#include "npc_anim_ids.hpp" // the species' colour ranges

namespace gp
{

    constexpr int kColorMachineActivity = 0x29, kColorMachineSound = 0x5c;
    constexpr int kColorPresets = 36;
    // g_petPalettePresetTable at 0xd15c.
    constexpr int kColorPresetTable[kColorPresets] = {
        0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28};
    constexpr int kColorBase[3] = {0x1df, 0x29e, 0x31e}; // dog, monkey, panda
    // Eight ROM ticks (59.94 Hz) between two colours is 4.004 steps of the 30 Hz sim.
    constexpr int kColorStepTicks = 4;

    inline int colorBase(int species) { return artOf(species).colorBase; }

    // The colour for a preset number.
    inline int machinePalette(int species, int index) { return colorBase(species) + kColorPresetTable[((index % kColorPresets) + kColorPresets) % kColorPresets]; }

    // Where the pet's colour sits among the presets, or -1 when it is not one of them (a potion's, say).
    inline int resyncIndex(int species, int appearance)
    {
        const int v = appearance - colorBase(species);
        for (int i = 0; i < kColorPresets; ++i)
            if (kColorPresetTable[i] == v)
                return i;
        return -1;
    }

    // The next preset, forward (+1) or backward (-1), wrapping round the 36.
    constexpr int stepIndex(int index, int dir) { return ((index + dir) % kColorPresets + kColorPresets) % kColorPresets; }

    // The same with the presets the db holds (a mod's may differ in order and number).
    inline int machinePalette(int species, int index, const std::vector<int> &presets)
    {
        const int n = int(presets.size());
        return n == 0 ? colorBase(species) : colorBase(species) + presets[size_t(((index % n) + n) % n)];
    }
    inline int resyncIndex(int species, int appearance, const std::vector<int> &presets)
    {
        const int v = appearance - colorBase(species);
        for (int i = 0; i < int(presets.size()); ++i)
            if (presets[size_t(i)] == v)
                return i;
        return -1;
    }
    inline int stepIndex(int index, int dir, int count) { return count <= 0 ? 0 : ((index + dir) % count + count) % count; }

    // What the machine cycles through for one species, as palette numbers, in the order of the db's `presets` list. An entry below
    // kModPaletteBase is an offset from the species' base colour (the ROM's own); an entry of a mod's palette is the palette itself,
    // offered only to the species it names (paletteSpecies(n): -1 = any). The potions' colours are not in the list, so the machine
    // never lands on them: they stay exclusive to the potions.
    inline std::vector<int> machineColours(int species, const std::vector<int> &presets, const std::function<int(int)> &paletteSpecies)
    {
        std::vector<int> out;
        for (int e : presets)
        {
            if (!isModPalette(e))
                out.push_back(colorBase(species) + e);
            else if (const int only = paletteSpecies ? paletteSpecies(e) : -1; only < 0 || only == species)
                out.push_back(e);
        }
        return out;
    }
    // Where a palette sits in that list, -1 if it is not one of them (a potion's colour, say).
    inline int colourIndex(const std::vector<int> &colours, int palette)
    {
        for (int i = 0; i < int(colours.size()); ++i)
            if (colours[size_t(i)] == palette)
                return i;
        return -1;
    }
    // The palette at a place in the list, wrapping round; `fallback` for an empty list.
    inline int colourAt(const std::vector<int> &colours, int index, int fallback)
    {
        const int n = int(colours.size());
        return n == 0 ? fallback : colours[size_t(((index % n) + n) % n)];
    }

    // The direction of this use given the last one's (0 before any): the first goes forward, then they alternate.
    constexpr int nextDirection(int last) { return last > 0 ? -1 : 1; }

} // namespace gp
