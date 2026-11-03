// prize_pool_test.cpp - the minigame prize pickers: the base pools behave as before, a mod's pools are used, and pools a mod made
// too small or empty never hang the game.
#include "../game/item_pools.hpp"
#include "../game/minigame_defs.hpp"
#include "../game/three_in_a_row.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <set>

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

static std::function<int(int, int)> seeded(unsigned seed)
{
    auto g = std::make_shared<std::mt19937>(seed);
    return [g](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi - 1)(*g); };
}

int main()
{
    using namespace gp;
    // the record built from the constants holds the same lists
    MinigameDef pools;
    for (const MinigameDef &m : baseMinigames())
        if (m.id == "base:minigame/prize_pools")
            pools = m;
    CHECK(pools.list("first").size() == 19 && pools.list("general").size() == 46);
    CHECK(pools.list("prizeA").size() == 2 && pools.list("prizeB").size() == 4 && pools.list("prizeC").size() == 8 && pools.list("prizeD").size() == 5);
    CHECK(pools.get("special") == 0xb1);

    // the same random numbers give the same sets with the record's lists as with the built-in pools (the base game is unchanged)
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        CHECK(buildUniqueRandomSet(6, 2, seeded(seed)) == buildUniqueRandomSet(6, 2, seeded(seed), pools.list("first"), pools.list("general")));
        CHECK(ThreeInARow::pickItems(7, seeded(seed)) == ThreeInARow::pickItems(7, seeded(seed), pools.list("first"), pools.list("general")));
        const ThreeInARow::PrizeTiers t{pools.list("prizeA"), pools.list("prizeB"), pools.list("prizeC"), pools.list("prizeD"), 0xb1};
        CHECK(ThreeInARow::pickPrizeItems(8, true, seeded(seed)) == ThreeInARow::pickPrizeItems(8, true, seeded(seed), t));
    }

    // a mod's pools are what gets drawn
    {
        const std::vector<int> first{100, 101}, general{200, 201, 202, 203, 204, 205, 206};
        const std::vector<int> set = buildUniqueRandomSet(6, 2, seeded(5), first, general);
        CHECK(set.size() == 6 && std::set<int>(set.begin(), set.end()).size() == 6);
        int fromFirst = 0;
        for (int v : set)
            fromFirst += (v == 100 || v == 101);
        CHECK(fromFirst == 2 && std::all_of(set.begin(), set.end(), [](int v) { return v == 100 || v == 101 || (v >= 200 && v <= 206); }));
        const ThreeInARow::PrizeTiers t{{1}, {2}, {3}, {4, 5, 6}, 9};
        const std::vector<int> prize = ThreeInARow::pickPrizeItems(5, true, seeded(5), t);
        CHECK(prize.size() == 5 && std::count(prize.begin(), prize.end(), 9) == 1 && std::count(prize.begin(), prize.end(), 1) == 1 &&
              std::count(prize.begin(), prize.end(), 2) == 1 && std::count(prize.begin(), prize.end(), 3) == 1);
    }
    // too few different items: the set stops short; empty pools give nothing; neither hangs
    {
        const std::vector<int> few{7, 8};
        const std::vector<int> set = buildUniqueRandomSet(6, 2, seeded(3), few, few);
        CHECK(set.size() == 2);
        CHECK(buildUniqueRandomSet(6, 2, seeded(3), {}, {}).empty());
        CHECK(ThreeInARow::pickItems(6, seeded(3), {}, {}).empty());
        CHECK(ThreeInARow::pickItems(6, seeded(3), few, few).size() == 2);
        const ThreeInARow::PrizeTiers none{{}, {}, {}, {}, 9};
        CHECK(ThreeInARow::pickPrizeItems(5, true, seeded(3), none) == std::vector<int>({9}));
        CHECK(ThreeInARow::pickPrizeItems(5, false, seeded(3), none).empty());
        const ThreeInARow::PrizeTiers oneD{{}, {}, {}, {4, 4}, 9};
        CHECK(ThreeInARow::pickPrizeItems(5, false, seeded(3), oneD).size() == 1); // a repeat is skipped, not looped on
    }
    if (fails)
        std::printf("%d check(s) failed\n", fails);
    else
        std::printf("prize_pool_test: all passed\n");
    return fails ? 1 : 0;
}
