// fixed_spawn_test.cpp - fixed hidden items, legacy ground items, the 196->194 hook, quest-count spawns and
// the Player adapter. No ROM needed.
#include "../src/game/fixed_spawn.hpp"
#include "../src/loaders/fixed_spots.hpp"
#include <cstdio>
#include <map>
#include <set>

using namespace gp;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static RandFn constRng(int x) { return [x](int, int) { return x; }; }

struct World {
    std::map<int, int> held;
    std::set<int> offered, legacy;
    uint32_t trig = 0;
    FixedSpawnEnv env() {
        FixedSpawnEnv e;
        e.held = [this](int it) { auto f = held.find(it); return f == held.end() ? 0 : f->second; };
        e.offered = [this](int it) { return offered.count(it) != 0; };
        e.legacy = [this](int it) { return legacy.count(it) != 0; };
        e.triggered = [this](int n) { return n >= 0 && n < 32 && ((trig >> n) & 1u) != 0; };
        e.itemNumber = [](const Id &id) { return legacyItemNumber(id); };
        return e;
    }
};

static std::vector<const FixedSpotDef*> spotsIn(const std::vector<FixedSpotDef>& all, int area) {
    std::vector<const FixedSpotDef*> v;
    for (const FixedSpotDef& s : all) if (s.area == area) v.push_back(&s);
    return v;
}
static std::set<int> items(const WorldItems& t) {
    std::set<int> s;
    for (const WorldItem& e : t.events()) s.insert(e.item);
    return s;
}
static const WorldItem* find(const WorldItems& t, int item) {
    for (const WorldItem& e : t.events()) if (e.item == item) return &e;
    return nullptr;
}

static void testTable() {
    const auto all = baseFixedSpots();
    CHECK(all.size() == 31);
    int hidden = 0, legacy = 0;
    std::set<std::string> ids;
    std::set<int> triggers, offeredItems;
    std::map<int, int> perArea;
    for (const FixedSpotDef& s : all) {
        ids.insert(s.id);
        if (s.state == 1) { ++hidden; ++perArea[s.area]; }
        if (s.state == 2) ++legacy;
        if (s.trigger >= 0) triggers.insert(s.trigger);
        if (s.notOffered) offeredItems.insert(legacyItemNumber(s.item));
    }
    CHECK(hidden == 23 && legacy == 8);
    CHECK(ids.size() == all.size());
    CHECK(triggers.size() == 18 && *triggers.begin() == 0 && *triggers.rbegin() == 17);
    CHECK((offeredItems == std::set<int>{173, 178, 179, 180, 181}));
    const std::map<int, int> expect = {{0, 5}, {1, 4}, {2, 2}, {3, 2}, {4, 1}, {5, 1}, {6, 3}, {7, 2}, {9, 3}};
    CHECK(perArea == expect);
}

static void testGates() {
    const auto all = baseFixedSpots();
    World w;
    {   // area 0, nothing held: only the trigger-only spots
        WorldItems t;
        CHECK(spawnFixedSpots(t, 0, spotsIn(all, 0), w.env()) == 3);
        CHECK((items(t) == std::set<int>{124, 101, 102}));
        const WorldItem* e = find(t, 124);
        CHECK(e && e->state == EventState::HiddenQuest && !e->persistent && !e->category);
        CHECK(e && e->x == 1662 && e->y == 898 && e->trigger == 4 && e->spawnSlot == kEventNoSlot);
        CHECK(spawnFixedSpots(t, 0, spotsIn(all, 0), w.env()) == 0);   // calling twice never duplicates
        CHECK(t.size() == 3);
    }
    {   // map 188 opens the lost-and-found spot; 193 opens item 5
        w.held[188] = 1; w.held[193] = 1;
        WorldItems t;
        spawnFixedSpots(t, 0, spotsIn(all, 0), w.env());
        CHECK((items(t) == std::set<int>{173, 5, 124, 101, 102}));
        CHECK(find(t, 173)->trigger == kEventNoTrigger);               // lost-and-found: no trigger
        w.held[173] = 1;                                               // already holding it
        WorldItems u;
        spawnFixedSpots(u, 0, spotsIn(all, 0), w.env());
        CHECK(!find(u, 173));
        w.held[173] = 0; w.offered.insert(173);                        // already handed to the King
        WorldItems v;
        spawnFixedSpots(v, 0, spotsIn(all, 0), w.env());
        CHECK(!find(v, 173));
    }
    {   // a set trigger bit hides a spot for good
        World x; x.trig = 1u << 4;
        WorldItems t;
        spawnFixedSpots(t, 0, spotsIn(all, 0), x.env());
        CHECK(!find(t, 124) && find(t, 101) && find(t, 102));
    }
    {   // "not while held": 199 needs 198 held and no 199 in the bag; 197 needs none held
        World x; x.held[198] = 1;
        WorldItems t;
        spawnFixedSpots(t, 1, spotsIn(all, 1), x.env());
        CHECK(find(t, 199) && find(t, 96) && !find(t, 194) && !find(t, 144));
        x.held[199] = 1;
        WorldItems u;
        spawnFixedSpots(u, 1, spotsIn(all, 1), x.env());
        CHECK(!find(u, 199));
        World y;
        WorldItems v;
        spawnFixedSpots(v, 5, spotsIn(all, 5), y.env());
        CHECK(find(v, 197));
        y.held[197] = 1;
        WorldItems z;
        spawnFixedSpots(z, 5, spotsIn(all, 5), y.env());
        CHECK(!find(z, 197));
    }
    {   // spots for another area are ignored
        WorldItems t;
        CHECK(spawnFixedSpots(t, 3, spotsIn(all, 0), w.env()) == 0);
    }
}

static void testDigClosesSpot() {
    const auto all = baseFixedSpots();
    World w;
    WorldItems t;
    spawnFixedSpots(t, 0, spotsIn(all, 0), w.env());
    const DigResult r = t.dig(0, 1662, 898, w.trig, constRng(1));
    CHECK(r.kind == DigResult::Kind::Item && r.item == 124 && w.trig == (1u << 4));
    t.clearNonPersistent();                                            // leave the area and come back
    spawnFixedSpots(t, 0, spotsIn(all, 0), w.env());
    CHECK(!find(t, 124) && find(t, 101) && find(t, 102));              // 124 is gone for good (see notes)
}

static void testLegacy() {
    const auto all = baseFixedSpots();
    World w;
    WorldItems t;
    spawnFixedSpots(t, 4, spotsIn(all, 4), w.env());
    const WorldItem* e = find(t, 224);
    CHECK(e && e->state == EventState::Visible && e->x == 554 && e->y == 288 && !e->persistent);
    w.held[224] = 1;
    WorldItems u;
    spawnFixedSpots(u, 4, spotsIn(all, 4), w.env());
    CHECK(!find(u, 224));
    w.held[224] = 0; w.legacy.insert(224);
    WorldItems v;
    spawnFixedSpots(v, 4, spotsIn(all, 4), w.env());
    CHECK(!find(v, 224));
}

static void testGrantHook() {
    const auto all = baseFixedSpots();
    World w;
    WorldItems t;
    CHECK(onItemGranted(t, 1, 196, spotsIn(all, 1), w.env()) == 1);
    const WorldItem* e = find(t, 194);
    CHECK(e && e->state == EventState::HiddenQuest && e->x == 2121 && e->y == 1171 && e->trigger == 3);
    CHECK(onItemGranted(t, 1, 196, spotsIn(all, 1), w.env()) == 0);    // no duplicate
    WorldItems u;
    CHECK(onItemGranted(u, 0, 196, spotsIn(all, 0), w.env()) == 0);    // wrong area
    CHECK(onItemGranted(u, 1, 195, spotsIn(all, 1), w.env()) == 0);    // wrong item
    w.trig = 1u << 3;
    CHECK(onItemGranted(u, 1, 196, spotsIn(all, 1), w.env()) == 0);    // already dug
}

static void testQuestCount() {
    WorldItems t;
    CHECK(spawnQuestCountItems(t, 15, 4) == 0);                        // only mode 5
    CHECK(spawnQuestCountItems(t, 16, 5) == 0);                        // exact equality
    CHECK(spawnQuestCountItems(t, 15, 5) == 1);
    const WorldItem* e = find(t, 262);
    CHECK(e && e->area == 3 && e->state == EventState::HiddenQuest && e->persistent && e->trigger == kEventNoTrigger);
    t.clearNonPersistent();
    CHECK(find(t, 262));                                               // survives area change
    CHECK(spawnQuestCountItems(t, 32, 5) == 1);
    CHECK(find(t, 168) && find(t, 168)->state == EventState::Visible && find(t, 168)->area == 9);
    CHECK(spawnQuestCountItems(t, 90, 5) == 1 && find(t, 183)->x == 1636);
    CHECK(spawnQuestCountItems(t, 100, 5) == 1 && find(t, 236)->y == 640);
}

static void testPlayerAdapter() {
    Player p;
    p.addItem(legacyItemId(188));
    FixedSpawnEnv e = playerSpawnEnv(p);
    CHECK(e.held(188) == 1 && e.held(5) == 0);
    CHECK(!e.offered(173) && !e.legacy(224));
    markOffered(p, 173); markLegacy(p, 224);
    CHECK(e.offered(173) && !e.offered(178) && e.legacy(224));
    p.setTrigger(4);
    CHECK(e.triggered(4) && !e.triggered(5) && !e.triggered(-1) && !e.triggered(40));

    // end to end: dig with the Player's trigger word, then the spot stays closed
    const auto all = baseFixedSpots();
    WorldItems t;
    spawnFixedSpots(t, 0, spotsIn(all, 0), e);
    CHECK(!find(t, 124) && find(t, 101));                              // 124's trigger already set
    uint32_t bits = 0; // player flags -> bits, like walk.cpp
    for (int n = 0; n < 32; ++n)
        if (p.hasTrigger(n))
            bits |= 1u << n;
    t.dig(0, 885, 898, bits, constRng(1));
    for (int n = 0; n < 32; ++n)
        if (bits >> n & 1u)
            p.setTrigger(n);
    CHECK(e.triggered(16));
}

int main() {
    testTable();
    testGates();
    testDigClosesSpot();
    testLegacy();
    testGrantHook();
    testQuestCount();
    testPlayerAdapter();
    if (g_fail) { std::printf("fixed_spawn_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("fixed_spawn_test: all passed\n");
    return 0;
}
