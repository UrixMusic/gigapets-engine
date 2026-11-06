// pool_tables_test.cpp - the friend pools (gifts, advice, default lines) and the colour machine presets: the base records hold what the
// old constants did, the db-fed overloads draw the same, and a mod's lists are what gets used.
#include "../src/game/pet/befriend.hpp"
#include "../src/game/pet/color_machine.hpp"
#include "../src/game/minigames/minigame_defs.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                             \
    do                                                       \
    {                                                        \
        if (!(c))                                            \
        {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while (0)

int main()
{
    using namespace gp;
    MinigameDef fp, cm;
    for (const MinigameDef &m : baseMinigames())
    {
        if (m.id == "base:minigame/friend_pools")
            fp = m;
        if (m.id == "base:minigame/colour_machine")
            cm = m;
    }
    CHECK(fp.list("gifts").size() == 39 && fp.idList("advice").size() == 47 && fp.idList("defaultLines").size() == 6);
    CHECK(fp.idList("advice").front() == "base:text/743" && fp.idList("advice").back() == "base:text/789"); // 0x2e7 .. 0x315
    CHECK(fp.idList("defaultLines").front() == "base:text/737" && fp.itemLists.count("gifts"));
    CHECK(cm.list("presets").size() == 36 && cm.get("stepTicks") == 4 && cm.list("presets")[0] == 1 && cm.list("presets")[35] == 0x28);

    // the same random numbers pick the same entries as the built-in tables
    for (unsigned seed = 1; seed <= 30; ++seed)
    {
        Rng a, b, c, d, e, f;
        for (Rng *r : {&a, &b, &c, &d, &e, &f})
            r->state = seed * 2654435761u + 1;
        CHECK(rollFriendGift(a) == rollFriendGift(b, fp.list("gifts")));
        CHECK(friendAdviceId(c) == pickTextId(d, fp.idList("advice")));
        CHECK(defaultLineId(e) == pickTextId(f, fp.idList("defaultLines")));
    }
    // an empty pool gives nothing
    {
        Rng r;
        CHECK(rollFriendGift(r, {}) == -1 && pickTextId(r, {}).empty());
    }

    // the colour machine over the record's presets behaves like the constants, and over a mod's own list
    for (int species = 0; species < 3; ++species)
        for (int i = -3; i < 40; ++i)
            CHECK(machinePalette(species, i) == machinePalette(species, i, cm.list("presets")));
    CHECK(resyncIndex(1, machinePalette(1, 17)) == resyncIndex(1, machinePalette(1, 17), cm.list("presets")));
    CHECK(stepIndex(35, 1) == stepIndex(35, 1, 36) && stepIndex(0, -1) == stepIndex(0, -1, 36));
    {
        const std::vector<int> mine{9, 8, 7};
        CHECK(machinePalette(0, 0, mine) == 0x1df + 9 && machinePalette(0, 4, mine) == 0x1df + 8 && machinePalette(0, -1, mine) == 0x1df + 7);
        CHECK(resyncIndex(0, 0x1df + 8, mine) == 1 && resyncIndex(0, 0x1df + 1, mine) == -1);
        CHECK(stepIndex(2, 1, 3) == 0 && stepIndex(0, -1, 3) == 2 && stepIndex(5, 1, 0) == 0);
        CHECK(machinePalette(1, 0, {}) == 0x29e); // an empty list is just the base colour
    }
    { // the machine's colours: the ROM's offsets become the species' palettes, a mod palette is offered to the species it names, potions never appear
        const int ember = kModPaletteBase + 1, dogOnly = kModPaletteBase + 2;
        auto speciesOf = [&](int n) { return n == dogOnly ? 0 : -1; };
        const std::vector<int> presets{1, 2, ember, dogOnly, 3};
        const std::vector<int> dog = machineColours(0, presets, speciesOf), monkey = machineColours(1, presets, speciesOf);
        CHECK(dog == std::vector<int>({0x1df + 1, 0x1df + 2, ember, dogOnly, 0x1df + 3}));
        CHECK(monkey == std::vector<int>({0x29e + 1, 0x29e + 2, ember, 0x29e + 3})); // the dog-only palette is skipped
        CHECK(colourIndex(dog, ember) == 2 && colourIndex(dog, 0x1df + 3) == 4 && colourIndex(monkey, dogOnly) == -1);
        CHECK(colourIndex(dog, 0x1df + 0x20) == -1); // a potion colour is not in the list
        CHECK(colourAt(dog, 5, 0) == dog[0] && colourAt(dog, -1, 0) == dog[4] && colourAt({}, 3, 77) == 77);
        // the base game's presets give exactly the old palettes
        for (int sp = 0; sp < 3; ++sp)
        {
            std::vector<int> all(std::begin(kColorPresetTable), std::end(kColorPresetTable));
            const std::vector<int> cols = machineColours(sp, all, nullptr);
            bool same = cols.size() == 36;
            for (int i = 0; i < 36 && same; ++i)
                same = cols[size_t(i)] == machinePalette(sp, i);
            CHECK(same);
        }
    }
    if (fails)
        std::printf("%d check(s) failed\n", fails);
    else
        std::printf("pool_tables_test: all passed\n");
    return fails ? 1 : 0;
}
