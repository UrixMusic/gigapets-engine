// starter_room_test.cpp <rom> - the real starter scene (frontend_demo.cpp) rolled with a pool: the ROM's three give the ROM's room, and a pool
// with a mod species puts it in the room in its own colours. Needs the ROM (the base species' colours are in it).
#define GP_NO_MAIN
bool smoothCam = false; // the unity build gets this from walk.cpp
#include "../src/app/frontend_demo.cpp"
#include <iostream>
#include <set>

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

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: starter_room_test <rom>\n";
        return 2;
    }
    gp::Rom rom(argv[1]);
    std::mt19937 rng(7);
    auto rb = [&](int n) { return int(rng() % unsigned(n)); };

    // the ROM's three: three of each kind in groups, colours from the ROM
    gp::ContentDB base;
    gp::importRom(rom, base, 0);
    {
        const gp::StarterPool pool = gp::makeStarterPool(base, {});
        CHECK(pool.romRoom());
        for (int t = 0; t < 20; ++t)
        {
            StarterScene sc;
            sc.start(rom, rb, pool);
            CHECK(sc.pets.size() == 12);
            for (int g = 0; g < 4; ++g)
                CHECK(sc.pets[size_t(g * 3)].species + sc.pets[size_t(g * 3 + 1)].species + sc.pets[size_t(g * 3 + 2)].species == 3);
            for (const auto &p : sc.pets)
                CHECK(p.colour > 0);
        }
    }
    // a mod species with its own colours (a species number the registry knows)
    {
        gp::SpeciesArt a;
        a.id = "fox:species/x";
        gp::speciesArts().push_back(a); // number 3
        gp::SpeciesDef d;
        d.id = "fox:species/x";
        d.legacyIndex = 3;
        d.defaultPalette = 1234;
        d.starterRoom = true;
        d.starterColors = {2001, 2002};
        d.starterRareColors = {2003};
        base.put(d);
        const gp::StarterPool pool = gp::makeStarterPool(base, {});
        CHECK(!pool.romRoom() && pool.entries.size() == 4);
        std::set<int> species;
        for (int t = 0; t < 40; ++t)
        {
            StarterScene sc;
            sc.start(rom, rb, pool);
            for (int g = 0; g < 4; ++g)
                CHECK(sc.pets[size_t(g * 3)].species != sc.pets[size_t(g * 3 + 1)].species && sc.pets[size_t(g * 3 + 1)].species != sc.pets[size_t(g * 3 + 2)].species);
            for (const auto &p : sc.pets)
            {
                species.insert(p.species);
                if (p.species == 3)
                    CHECK(p.colour == 2001 || p.colour == 2002 || p.colour == 2003);
                else
                    CHECK(p.colour > 0 && p.colour < 2000);
            }
        }
        CHECK(species.count(3) && species.count(0) && species.count(1) && species.count(2));
        // none of its anims set: the room just does not play them (no crash)
        StarterScene sc;
        sc.start(rom, rb, pool);
        for (const auto &p : sc.pets)
            if (p.species == 3)
                CHECK(sc.kIdle(p.species) == -1 && sc.kWalk(p.species) == -1);
    }
    if (g_fail)
        return 1;
    std::cout << "starter_room_test: all passed\n";
    return 0;
}
