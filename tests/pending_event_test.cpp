// pending_event_test.cpp - pending-event table (dig spots, ground items) and the spawn-point loader.
// Usage: pending_event_test [path/to/rom.u7]   (the real-ROM check is skipped if the ROM can't be opened)
#include "../src/game/pending_event.hpp"
#if defined(__has_include)
#  if __has_include("../src/loaders/import.hpp")
#    define HAVE_IMPORT 1
#    include "../src/loaders/import.hpp"   // importAreas, for the real-ROM check
#  endif
#endif
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>

using namespace gp;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// Scripted RNG: hands out queued values in order and checks each one is inside [lo, hi).
static RandFn script(std::vector<int> v) {
    auto q = std::make_shared<std::pair<std::vector<int>, size_t>>(std::move(v), 0);
    return [q](int lo, int hi) {
        const int x = q->first.at(q->second++);
        CHECK(x >= lo && x < hi);
        return x;
    };
}
static RandFn constRng(int x) { return [x](int lo, int hi) { CHECK(x >= lo && x < hi); return x; }; }

static AreaDef gridArea(int n, int maxEvents = 12, int legacyId = 0) {
    AreaDef a;
    a.legacyId = legacyId;
    a.maxEvents = maxEvents;
    for (int i = 0; i < n; ++i) a.spawnPoints.push_back({(i % 5) * 100 + 50, (i / 5) * 100 + 50});
    return a;
}

static const int kNone = kEventNoTrigger;

static void testItemTable() {
    int total = 0;
    for (const auto& w : kEventItemTable) total += w.weight;
    CHECK(total == kEventItemTotalWeight);
    CHECK(pickRandomEventItemId(script({1})) == 0);
    CHECK(pickRandomEventItemId(script({15})) == 0);        // under 16 = dud
    CHECK(pickRandomEventItemId(script({16, 0})) == 2);
    CHECK(pickRandomEventItemId(script({16, 24})) == 2);    // last of the first 25
    CHECK(pickRandomEventItemId(script({16, 25})) == 6);
    CHECK(pickRandomEventItemId(script({16, 242})) == 267);

    std::mt19937 g(42);
    const RandFn rnd = makeRandFn(g);
    const int N = 100000;
    int duds = 0, i265 = 0;
    for (int i = 0; i < N; ++i) {
        const int it = pickRandomEventItemId(rnd);
        if (it == 0) ++duds; else if (it == 265) ++i265;
    }
    CHECK(duds > 14000 && duds < 16000);                    // 15%
    const double f = double(i265) / double(N - duds);
    CHECK(f > 0.40 && f < 0.425);                           // 100/243 = 41.2%
}

static void testSpawn() {
    std::mt19937 g(1234);
    const RandFn rnd = makeRandFn(g);

    for (int iter = 0; iter < 50; ++iter) {
        PendingEventTable t;
        t.enterArea(gridArea(20), rnd);
        CHECK(t.size() == 6);
        std::set<int> slots;
        for (const auto& e : t.events()) {
            CHECK(e.state == EventState::BuriedRandom);
            CHECK(!e.persistent);
            CHECK(e.area == 0);
            slots.insert(e.spawnSlot);
        }
        CHECK(slots.size() == 6);
    }

    {   // count clamps to the number of spawn points
        PendingEventTable t;
        t.enterArea(gridArea(3), rnd);
        CHECK(t.size() == 3);
        t.enterArea(gridArea(3), rnd);                   // re-entry clears non-persistent, respawns
        CHECK(t.size() == 3);
    }
    {   // persistent events survive entry and count against the clamp
        PendingEventTable t;
        t.spawn(0, 0xffff, 50, 10, 10, EventState::Visible, true, false, 0xffff);
        t.enterArea(gridArea(20), rnd);
        CHECK(t.size() == 7);
        CHECK(t.events()[0].item == 50 && t.events()[0].persistent);
        t.enterArea(gridArea(20, 12, 1), rnd);                  // other area: persistent one stays, area 0 spots go
        CHECK(t.countForArea(0) == 1 && t.countForArea(1) == 6);
    }
    {   // holes within 30 px block a point; the one free point is always picked
        PendingEventTable t;
        const AreaDef sp = gridArea(4);
        t.addHole({50, 50, 0, 900});
        t.addHole({160, 40, 0, 901});
        t.addHole({250, 50, 0, 902});
        CHECK(t.spawnRandom(sp, 1, rnd) == 1);
        CHECK(t.events()[0].spawnSlot == 3);
        t.addHole({350, 50, 0, 903});                       // now every point is blocked
        PendingEventTable u;
        u.addHole({50, 50, 0, 1}); u.addHole({150, 50, 0, 2}); u.addHole({250, 50, 0, 3}); u.addHole({350, 50, 0, 4});
        CHECK(u.spawnRandom(sp, 1, rnd) == 0);           // gives up after 200 tries, no hang
        CHECK(u.size() == 0);
    }
    {   // refill gate
        PendingEventTable t;
        const AreaDef sp = gridArea(10, 2);
        CHECK(!t.refillOne(sp, 1, rnd));                 // quest active
        CHECK(t.refillOne(sp, 0, rnd));
        CHECK(t.refillOne(sp, 0, rnd));
        CHECK(!t.refillOne(sp, 0, rnd));                 // at max
        CHECK(t.countForArea(0) == 2);
    }
}

static void testDig() {
    {
        PendingEventTable t;
        uint32_t flags = 0;
        t.spawn(0, 0xffff, 13, 200, 200, EventState::HiddenQuest, false, false, 5);
        t.spawn(0, 7, 0, 500, 500, EventState::BuriedRandom, false, false, kNone);   // dud
        t.spawn(1, 2, 2, 205, 205, EventState::BuriedRandom, false, false, kNone);   // other area

        CHECK(t.dig(0, 400, 400, flags, constRng(0)).kind == DigResult::Kind::Miss);
        CHECK(t.dig(0, 230, 200, flags, constRng(0)).kind == DigResult::Kind::Miss); // |dx| = 30 is outside
        DigResult r = t.dig(0, 229, 171, flags, constRng(0));                        // |dx|,|dy| = 29, 29
        CHECK(r.kind == DigResult::Kind::Item);
        CHECK(r.item == 13 && r.triggerSet && flags == (1u << 5));
        const PendingEvent& e = t.events()[size_t(r.index)];
        CHECK(e.state == EventState::Visible && e.x == 220 && e.y == 207);           // roll 0 -> +20, y +7
        CHECK(t.holes().size() == 1 && t.holes()[0].x == 200 && t.holes()[0].y == 200);
        CHECK(t.dig(0, 200, 200, flags, constRng(0)).kind == DigResult::Kind::Miss); // already visible

        r = t.dig(0, 500, 500, flags, constRng(0));
        CHECK(r.kind == DigResult::Kind::Dud);
        CHECK(t.size() == 2);                                                        // dud slot deleted
        CHECK(flags == (1u << 5));
        CHECK(t.holes().size() == 2);
        for (const auto& ev : t.events()) if (ev.area == 1) CHECK(ev.state == EventState::BuriedRandom);
    }
    {   // roll != 0 -> -20; trigger 0xffff sets no flag
        PendingEventTable t;
        uint32_t flags = 0;
        t.spawn(0, 3, 2, 300, 300, EventState::BuriedRandom, false, false, 0xffff);
        const DigResult r = t.dig(0, 300, 300, flags, constRng(1));
        CHECK(r.kind == DigResult::Kind::Item && !r.triggerSet && flags == 0);
        CHECK(t.events()[0].x == 280 && t.events()[0].y == 307);
    }
}

static void testHoleRing() {
    PendingEventTable t;
    uint32_t flags = 0;
    for (int i = 0; i < 6; ++i) t.spawn(0, i, 2, 100 + 100 * i, 100, EventState::BuriedRandom, false, false, kNone);
    for (int i = 0; i < 5; ++i) {
        const DigResult r = t.dig(0, 100 + 100 * i, 100, flags, constRng(1));
        CHECK(r.kind == DigResult::Kind::Item && !r.filledHole);
    }
    CHECK(t.holes().size() == 5);
    CHECK(t.events()[0].state == EventState::Visible);

    const DigResult r = t.dig(0, 600, 100, flags, constRng(1));                      // sixth hole
    CHECK(r.filledHole && r.filledHole->x == 100 && r.filledHole->y == 100);
    CHECK(t.holes().size() == 5);
    CHECK(t.events()[0].state == EventState::BuriedRandom);                          // unclaimed item re-buried
    CHECK(t.events()[0].x == 100 && t.events()[0].y == 100);                         // at its spawn point
    CHECK(t.events()[5].state == EventState::Visible);
}

static void testTableLimits() {
    {
        PendingEventTable t;
        t.spawn(0, 0xffff, 1, 0, 0, EventState::Visible, true, false, 0xffff);
        for (int i = 1; i < kEventTableMax; ++i) t.spawn(0, 0xffff, 100 + i, i, 0, EventState::Visible, false, false, 0xffff);
        CHECK(t.size() == kEventTableMax);
        CHECK(t.spawn(0, 0xffff, 999, 0, 0, EventState::Visible, false, false, 0xffff) >= 0);
        CHECK(t.size() == kEventTableMax);
        CHECK(t.events()[0].persistent);                                             // persistent one kept
        CHECK(t.events()[1].item == 102);                                            // first non-persistent evicted
        CHECK(t.events().back().item == 999);
    }
    {
        PendingEventTable t;
        for (int i = 0; i < kEventTableMax; ++i) t.spawn(0, 0xffff, 1, i, 0, EventState::Visible, true, false, 0xffff);
        CHECK(t.spawn(0, 0xffff, 2, 0, 0, EventState::Visible, false, false, 0xffff) == -1);
        CHECK(t.size() == kEventTableMax);
    }
}

static void testPickup() {
    std::map<int, int> held;
    int granted = 0;
    PendingEventTable t;
    PickupEnv env;
    env.held = [&](int it) { auto f = held.find(it); return f == held.end() ? 0 : f->second; };
    env.capacity = [](int) { return 3; };
    env.grant = [&](int it) { granted = it; ++held[it]; };

    t.spawn(0, 0xffff, 2, 10, 10, EventState::Visible, false, false, 0xffff);
    CHECK(t.pickup(0, env).result == PickupResult::Granted);
    CHECK(granted == 2 && t.size() == 0);

    held[2] = 3;                                                                     // at capacity
    t.spawn(0, 0xffff, 2, 10, 10, EventState::Visible, false, false, 0xffff);
    CHECK(t.pickup(0, env).result == PickupResult::AtCapacity);
    CHECK(t.size() == 1);
    t.remove(0);

    t.spawn(0, 0xffff, 4, 10, 10, EventState::Shop, false, false, 0xffff);
    CHECK(t.pickup(0, env).result == PickupResult::ShopDialogue && t.size() == 1);
    t.remove(0);

    t.spawn(0, 0xffff, 0xe0, 10, 10, EventState::Visible, false, false, 0xffff);
    PickupOutcome o = t.pickup(0, env);
    CHECK(o.result == PickupResult::NeedItem && o.needItem == 0x107);
    held[0x107] = 1;
    CHECK(t.pickup(0, env).result == PickupResult::Granted && t.size() == 0);

    // grant spawning another event must not make the delete hit the wrong slot
    t.spawn(0, 0xffff, 196, 10, 10, EventState::Visible, false, false, 0xffff);
    env.grant = [&](int) { t.spawn(0, 0xffff, 194, 50, 50, EventState::HiddenQuest, false, false, 3); };
    CHECK(t.pickup(0, env).result == PickupResult::Granted);
    CHECK(t.size() == 1 && t.events()[0].item == 194);
}

static void testPrompt() {
    PendingEventTable t;
    t.spawn(0, 0xffff, 2, 100, 100, EventState::Visible, false, false, 0xffff);       // 0
    t.spawn(0, 0xffff, 2, 110, 100, EventState::Visible, false, false, 0xffff);       // 1
    t.spawn(0, 1, 2, 101, 100, EventState::BuriedRandom, false, false, 0xffff);       // buried: never a target
    t.spawn(0, 0xffff, 4, 112, 105, EventState::Shop, false, false, 0xffff);          // shop counts
    CHECK(t.nearestPrompt(0, 112, 100) == 1);
    CHECK(t.nearestPrompt(0, 112, 104) == 3);
    CHECK(t.nearestPrompt(1, 112, 100) == -1);                                        // wrong area
    CHECK(t.nearestPrompt(0, 200, 100) == -1);
    CHECK(markerTypeFor(EventState::BuriedRandom) == 5 && markerTypeFor(EventState::Visible) == 2 &&
          markerTypeFor(EventState::Shop) == 2 && markerTypeFor(EventState::HiddenQuest) == -1);
}

static void testRealRom(const std::string& path) {
#ifdef HAVE_IMPORT
    std::unique_ptr<Rom> rom;
    try { rom.reset(new Rom(path)); }
    catch (const std::exception& e) { std::printf("SKIP real-ROM checks (%s)\n", e.what()); return; }

    ContentDB db;
    importAreas(*rom, db);
    static const int kCount[10] = {52, 38, 31, 40, 0, 77, 36, 36, 0, 0};
    static const int kMax[10]   = {12, 12, 12, 12, 0, 12, 12, 12, 0, 0};
    for (int a = 0; a < 10; ++a) {
        const AreaDef* d = db.areaByLegacy(a);
        CHECK(d != nullptr);
        if (!d) continue;
        std::printf("area %d: %zu points, max %d", a, d->spawnPoints.size(), d->maxEvents);
        if (!d->spawnPoints.empty()) std::printf(", first (%d,%d)", d->spawnPoints[0].x, d->spawnPoints[0].y);
        std::printf("\n");
        CHECK(int(d->spawnPoints.size()) == kCount[a]);
        CHECK(d->maxEvents == kMax[a]);
        int outliers = 0;
        for (const IPoint& p : d->spawnPoints) if (p.x < 0 || p.x > 4096 || p.y < 0 || p.y > 4096) ++outliers;
        CHECK(outliers == 0);
    }
#else
    (void)path;
    std::printf("SKIP real-ROM checks (import.hpp not found)\n");
#endif
}

int main(int argc, char** argv) {
    try {
        testItemTable();
        testSpawn();
        testDig();
        testHoleRing();
        testTableLimits();
        testPickup();
        testPrompt();
        testRealRom(argc > 1 ? argv[1] : "rom.u7");
    } catch (const std::exception& e) {
        std::printf("FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (g_fail) { std::printf("pending_event_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("pending_event_test: all passed\n");
    return 0;
}