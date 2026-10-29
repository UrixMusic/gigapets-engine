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

    constexpr int colorBase(int species) { return species >= 0 && species < 3 ? kColorBase[species] : kColorBase[0]; }

    // The colour for a preset number.
    constexpr int machinePalette(int species, int index) { return colorBase(species) + kColorPresetTable[((index % kColorPresets) + kColorPresets) % kColorPresets]; }

    // Where the pet's colour sits among the presets, or -1 when it is not one of them (a potion's, say).
    constexpr int resyncIndex(int species, int appearance)
    {
        const int v = appearance - colorBase(species);
        for (int i = 0; i < kColorPresets; ++i)
            if (kColorPresetTable[i] == v)
                return i;
        return -1;
    }

    // The next preset, forward (+1) or backward (-1), wrapping round the 36.
    constexpr int stepIndex(int index, int dir) { return ((index + dir) % kColorPresets + kColorPresets) % kColorPresets; }

    // The direction of this use given the last one's (0 before any): the first goes forward, then they alternate.
    constexpr int nextDirection(int last) { return last > 0 ? -1 : 1; }

} // namespace gp
