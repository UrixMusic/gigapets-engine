// ground_parity_test.cpp - proves the imported content matches the existing ROM-based collision, pixel for pixel.
// Usage: ground_parity_test <rom>
#include "../src/loaders/import.hpp"
#include "../src/engine/ground.hpp"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: ground_parity_test <rom>\n"); return 1; }
    gp::Rom rom(argv[1]);
    gp::ContentDB db;
    gp::importAreas(rom, db);

    long totalBad = 0;
    for (int id = 0; id < gp::areaCount(rom); ++id) {
        gp::Area area; gp::RoomLink link;
        const gp::AreaDef* a = db.areaByLegacy(id);
        if (!a || !gp::readArea(rom, id, area) || !gp::readRoomLink(rom, id, link)) { std::printf("area %2d: skipped\n", id); continue; }
        const gp::CollisionSet* cs = db.collision(a->collision);
        if (!cs) { std::printf("area %2d: no collision set\n", id); continue; }
        gp::GroundCollision old(rom, area, link.collisionFrame);
        gp::GroundField neu(*a, *cs);
        long bad = 0, firstX = -1, firstY = -1;
        for (int y = -8; y < a->heightPx() + 8; ++y)
            for (int x = -8; x < a->widthPx() + 8; ++x)
                if (old.walkable(x, y) != neu.walkable(x, y)) { if (!bad) { firstX = x; firstY = y; } ++bad; }
        std::printf("area %2d: %s", id, bad ? "MISMATCH" : "ok");
        if (bad) std::printf("  %ld px differ, first at (%ld,%ld)", bad, firstX, firstY);
        std::printf("\n");
        totalBad += bad;
    }
    std::printf(totalBad ? "FAILED\n" : "all areas match\n");
    return totalBad ? 1 : 0;
}
