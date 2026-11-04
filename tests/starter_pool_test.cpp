// starter_pool_test.cpp - who the starter room holds: the species whose records say so, their colours, and how the 12 pets are rolled.
// (The colours of the ROM's own three are read from the ROM, so they are not rolled here.)
#include "../src/game/starter_pool.hpp"
#include <iostream>
#include <random>
#include <set>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                   \
    do                                                             \
    {                                                              \
        if (!(c))                                                  \
        {                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                              \
        }                                                          \
    } while (0)

static SpeciesDef def(const std::string &id, int n, bool room)
{
    SpeciesDef d;
    d.id = id;
    d.legacyIndex = n;
    d.defaultPalette = 100 + n;
    d.starterRoom = room;
    return d;
}

int main()
{
    // the base game's three, plus two registered species standing in for a mod's
    SpeciesArt a;
    a.id = "fox:species/a";
    speciesArts().push_back(a);
    a.id = "fox:species/b";
    speciesArts().push_back(a);

    ContentDB db;
    for (int i = 0; i < 3; ++i)
        db.put(def("base:species/" + std::to_string(i), i, true));
    db.put(def("fox:species/a", 3, false));
    SpeciesDef b = def("fox:species/b", 4, true);
    b.starterColors = {7, 8};
    b.starterRareColors = {9};
    b.starterRequires = {"unlocked"};
    db.put(b);

    // only the ROM's three: the room is the ROM's
    {
        const StarterPool p = makeStarterPool(db, {});
        CHECK(p.entries.size() == 3 && p.romRoom() && p.entries[0].romColours);
        std::mt19937 rng(1);
        auto rb = [&](int n) { return int(rng() % unsigned(n)); };
        for (int t = 0; t < 50; ++t)
        {
            int sp[12];
            rollRoomSpecies(p, rb, sp);
            for (int g = 0; g < 4; ++g)
                CHECK(sp[g * 3] + sp[g * 3 + 1] + sp[g * 3 + 2] == 3 && sp[g * 3] != sp[g * 3 + 1] && sp[g * 3 + 1] != sp[g * 3 + 2] && sp[g * 3] != sp[g * 3 + 2]);
        }
    }
    // a species that needs a profile key is offered once it is set; one that is not marked is never offered
    {
        const std::map<std::string, std::string> unlocked = {{"mod.fox.unlocked", "b1"}};
        const StarterPool p = makeStarterPool(db, unlocked);
        CHECK(p.entries.size() == 4 && !p.romRoom() && p.entries[3].species == 4 && !p.entries[3].romColours);
        CHECK(p.entries[3].common == std::vector<int>({7, 8}) && p.entries[3].rare == std::vector<int>({9}) && p.entries[3].fallback == 104);
        std::mt19937 rng(2);
        auto rb = [&](int n) { return int(rng() % unsigned(n)); };
        std::set<int> seen;
        for (int t = 0; t < 200; ++t)
        {
            int sp[12];
            rollRoomSpecies(p, rb, sp);
            for (int g = 0; g < 4; ++g) // three different ones in each group of three
                CHECK(sp[g * 3] != sp[g * 3 + 1] && sp[g * 3 + 1] != sp[g * 3 + 2] && sp[g * 3] != sp[g * 3 + 2]);
            for (int i = 0; i < 12; ++i)
                seen.insert(sp[i]);
        }
        CHECK(seen == std::set<int>({0, 1, 2, 4})); // species 3 is not marked for the room
        // colours: the list (rare list for a rare pet), else the default
        for (int t = 0; t < 50; ++t)
        {
            const int c = poolColour(p.entries[3], false, rb, nullptr), r = poolColour(p.entries[3], true, rb, nullptr);
            CHECK((c == 7 || c == 8) && r == 9);
        }
        StarterPool::Entry plain;
        plain.fallback = 55;
        CHECK(poolColour(plain, true, rb, nullptr) == 55);
        plain.common = {3};
        CHECK(poolColour(plain, true, rb, nullptr) == 3); // no rare list: the common one
    }
    // without the key it is not there
    {
        const StarterPool p = makeStarterPool(db, {});
        CHECK(p.entries.size() == 3 && p.romRoom());
    }
    // fewer than three offered: repeats are allowed
    {
        ContentDB one;
        one.put(def("fox:species/b", 4, true));
        SpeciesDef x = def("base:species/0", 0, false), y = def("base:species/1", 1, false), z = def("base:species/2", 2, false);
        one.put(x);
        one.put(y);
        one.put(z);
        const StarterPool p = makeStarterPool(one, {});
        CHECK(p.entries.size() == 1 || p.entries.size() == 3);
        // the species has a "requires" key here: with none set, the room still is never empty
        const StarterPool p2 = makeStarterPool(db, {});
        CHECK(!p2.entries.empty());
        ContentDB none; // nothing marked at all: the ROM's three
        none.put(def("base:species/0", 0, false));
        CHECK(makeStarterPool(none, {}).romRoom());
        ContentDB solo;
        SpeciesDef s4 = def("fox:species/b", 4, true);
        solo.put(s4);
        const StarterPool p3 = makeStarterPool(solo, {});
        CHECK(p3.entries.size() == 1 && p3.entries[0].species == 4);
        std::mt19937 rng(3);
        auto rb = [&](int n) { return int(rng() % unsigned(n)); };
        int sp[12];
        rollRoomSpecies(p3, rb, sp);
        for (int i = 0; i < 12; ++i)
            CHECK(sp[i] == 4);
    }

    if (g_fail)
        return 1;
    std::cout << "starter_pool_test: all passed\n";
    return 0;
}
