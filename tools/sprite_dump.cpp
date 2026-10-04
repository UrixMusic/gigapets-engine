// Prints sprite headers so you can compare against the ROM editor / Python script.
// Usage: sprite_dump <rom file> [count]
#include "rom.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: sprite_dump <rom file> [count]\n"); return 1; }
    const int count = argc > 2 ? std::atoi(argv[2]) : 12;
    try {
        gp::Rom rom(argv[1]);
        std::printf("ROM: %zu words\n", rom.sizeWords());
        for (int id = 0; id < count; ++id) {
            gp::SpriteHeader h;
            if (!gp::readSpriteHeader(rom, id, h)) { std::printf("%3d  (invalid)\n", id); continue; }
            std::printf("%3d  %3dx%-3d  pieces=%-2d  anchor=(%d,%d)  first=%d\n",
                        id, h.w, h.h, h.pieceCount, h.anchorX, h.anchorY, h.firstPiece);
        }
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}
