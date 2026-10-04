// Decode one sprite from the ROM and write it as a PNG.
// Usage: sprite_png <rom> <id> <out.png> [paletteId] [--bgr]
//   paletteId: e.g. 0x28c (no palette = greyscale)
#include "png.hpp"
#include "sprite.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: sprite_png <rom> <id> <out.png> [paletteId] [--bgr]\n"); return 1; }
    const int id = std::atoi(argv[2]);
    int palId = 0; bool bgr = false;
    for (int i = 4; i < argc; ++i) { if (!std::strcmp(argv[i], "--bgr")) bgr = true; else palId = int(std::strtol(argv[i], nullptr, 0)); }
    try {
        gp::Rom rom(argv[1]);
        gp::DecodedSprite s;
        if (!gp::decodeSprite(rom, id, s)) { std::printf("sprite %d is invalid\n", id); return 1; }
        gp::Palette pal; const bool havePal = palId && gp::readPalette(rom, palId, pal);
        if (palId && !havePal) std::printf("palette 0x%x not found, using greyscale\n", palId);
        std::vector<uint8_t> rgba; gp::toRGBA(s, havePal ? &pal : nullptr, bgr, rgba);
        if (!writePng(argv[3], s.w, s.h, rgba)) { std::printf("cannot write %s\n", argv[3]); return 1; }
        std::printf("sprite %d: canvas %dx%d, origin (%d,%d), anchor (%d,%d), max %dbpp -> %s\n",
                    id, s.w, s.h, s.originX, s.originY, s.anchorX, s.anchorY, s.maxBpp, argv[3]);
    } catch (const std::exception& e) { std::printf("error: %s\n", e.what()); return 1; }
    return 0;
}
