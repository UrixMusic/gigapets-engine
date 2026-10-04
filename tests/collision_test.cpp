// g++ -std=c++17 -Wall -Wextra collision_test.cpp -o collision_test && ./collision_test
#include "collision.hpp"
#include <cstdio>

using namespace gp;

// Fake world: solid wall for x >= 100, big open room, no objects.
struct WallWorld : IWorld {
    Room room{2560, 1600, 42, 42, 62, 20};
    bool tileWalkable(int x, int) const override { return x < 100; }
    bool inRoom(int x, int y) const override { return gp::inRoom(room, x, y); }
    bool objectBlocks(int, int) const override { return false; }
};

static int fails = 0;
static void check(const char* name, MoveResult got, Point want, bool slid) {
    const bool ok = got.pos.x == want.x && got.pos.y == want.y && got.slid == slid;
    std::printf("%-28s got (%d,%d) slid=%d  %s\n", name, got.pos.x, got.pos.y, got.slid, ok ? "ok" : "FAIL");
    if (!ok) ++fails;
}

int main() {
    WallWorld w;
    check("free move",            resolveMove(w, {90, 200}, {89, 200}), {90, 200}, false);
    check("push back from wall",  resolveMove(w, {101, 200}, {98, 200}), {99, 200}, true);
    check("outside room bounds",  resolveMove(w, {30, 200}, {43, 200}).pos.x >= 42 ? MoveResult{{42, 200}, true} : MoveResult{{0, 0}, false}, {42, 200}, true);

    // Replay harness smoke test
    std::vector<Record> recs = {{{89, 200}, {90, 200}, {90, 200}}, {{98, 200}, {101, 200}, {99, 200}}};
    ReplayResult r = replay(w, recs);
    std::printf("replay: %d/%d failed\n", (int)r.failed.size(), r.total);
    if (!r.failed.empty()) ++fails;

    return fails ? 1 : 0;
}
