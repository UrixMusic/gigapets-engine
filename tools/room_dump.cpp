// Dump an area: size, links, collision frame, objects, and a walkability PNG.
// Usage: room_dump <rom> <area> [out.png]    (PNG samples every 4th pixel: blocked = red, free = dark)
#include "png.hpp"
#include "room.hpp"
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: room_dump <rom> <area> [out.png]\n"); return 1; }
    try {
        gp::Rom rom(argv[1]);
        if (std::string(argv[2]) == "all") {          // overview of every area
            for (int i = 0; i < gp::areaCount(rom); ++i) {
                gp::Area a; if (!gp::readArea(rom, i, a)) { std::printf("%3d  (invalid)\n", i); continue; }
                gp::RoomLink l; const bool has = gp::readRoomLink(rom, i, l);
                std::printf("%3d  %3dx%-3d tiles  %3zu objects%s  collision frame %s\n", i, a.tilesW, a.tilesH, a.objects.size(),
                            a.objects.size() > 240 ? " (>240: beyond the flag words!)" : "", has ? std::to_string(l.collisionFrame).c_str() : "n/a (not in room table)");
            }
            return 0;
        }
        const int id = std::atoi(argv[2]);
        std::printf("areas in ROM: %d\n", gp::areaCount(rom));
        gp::Area a;
        if (!gp::readArea(rom, id, a)) { std::printf("area %d is invalid\n", id); return 1; }
        std::printf("area %d: %dx%d tiles (%dx%d px), %zu objects\n", id, a.tilesW, a.tilesH, a.tilesW * 32, a.tilesH * 32, a.objects.size());
        int interactive = 0; for (auto& o : a.objects) interactive += o.interactive();
        std::printf("  interactive objects: %d, scenery: %zu\n", interactive, a.objects.size() - interactive);
        gp::RoomLink link;
        if (!gp::readRoomLink(rom, id, link)) { std::printf("  not in room table 0xe06b\n"); return 0; }
        std::printf("  room table: tile frame %d, palette 0x%x, collision frame %d\n", link.tileFrame, link.paletteId, link.collisionFrame);
        const gp::Frame f = gp::readFrame(rom, link.collisionFrame);
        std::printf("  collision frame: words %04x %04x attr %04x seg %04x\n", f.w0, f.w1, f.attr, f.seg);
        if (argc > 3) {
            gp::GroundCollision gc(rom, a, link.collisionFrame);
            const int step = 4, w = a.tilesW * 32 / step, h = a.tilesH * 32 / step;
            std::vector<uint8_t> rgba(size_t(w) * h * 4); long blocked = 0;
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const bool free = gc.walkable(x * step, y * step); blocked += !free;
                uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
                p[0] = free ? 30 : 255; p[1] = free ? 30 : 40; p[2] = free ? 30 : 40; p[3] = 255;
            }
            writePng(argv[3], w, h, rgba);
            std::printf("  wrote %s (%dx%d), blocked samples: %ld of %d\n", argv[3], w, h, blocked, w * h);
        }
    } catch (const std::exception& e) { std::printf("error: %s\n", e.what()); return 1; }
    return 0;
}