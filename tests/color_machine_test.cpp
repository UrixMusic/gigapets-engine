// tests/color_machine_test.cpp - the spaceship colour machine's rules (game/color_machine.hpp).
#include <cstdio>
#include "../src/game/color_machine.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("36 presets, the first is +1 and the last +0x28", kColorPresets == 36 && kColorPresetTable[0] == 1 && kColorPresetTable[35] == 0x28);
    check("the table never has 5, 0xd, 0x1a or 0x1f", resyncIndex(0, 0x1df + 5) == -1 && resyncIndex(0, 0x1df + 0xd) == -1 &&
                                                           resyncIndex(0, 0x1df + 0x1a) == -1 && resyncIndex(0, 0x1df + 0x1f) == -1);
    check("palette for species and index: dog 0, monkey 3, panda 35", machinePalette(0, 0) == 0x1e0 && machinePalette(1, 3) == 0x29e + 4 && machinePalette(2, 35) == 0x31e + 0x28);
    check("resync finds the index back", resyncIndex(1, machinePalette(1, 17)) == 17 && resyncIndex(2, machinePalette(2, 0)) == 0);
    check("a colour from another species is not found", resyncIndex(0, 0x29e + 1) == -1);
    check("stepping wraps both ways", stepIndex(35, 1) == 0 && stepIndex(0, -1) == 35 && stepIndex(10, 1) == 11 && stepIndex(10, -1) == 9);
    check("the first use goes forward, then they alternate", nextDirection(0) == 1 && nextDirection(1) == -1 && nextDirection(-1) == 1);
    check("a colour every 4 sim steps", kColorStepTicks == 4);

    std::printf(fails ? "color_machine_test: %d failure(s)\n" : "color_machine_test: all passed\n", fails);
    return fails ? 1 : 0;
}
