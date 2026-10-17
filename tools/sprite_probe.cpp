#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../src/loaders/sprite.hpp"

// 32-bit BMP, no dependencies. Rows go bottom-up, pixels are BGRA.
static void writeBmp(const char *path, int w, int h, const std::vector<uint8_t> &rgba)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    const uint32_t size = 54 + w * h * 4;
    uint8_t hd[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v)
    { for (int i = 0; i < 4; ++i) hd[o + i] = uint8_t(v >> (8 * i)); };
    put32(2, size);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    hd[26] = 1;
    hd[28] = 32;
    fwrite(hd, 1, 54, f);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x)
        {
            const uint8_t *p = &rgba[(size_t(y) * w + x) * 4];
            // composite on mid-grey so transparent areas are visible
            const uint8_t a = p[3];
            uint8_t bgra[4] = {
                uint8_t((p[2] * a + 96 * (255 - a)) / 255),
                uint8_t((p[1] * a + 96 * (255 - a)) / 255),
                uint8_t((p[0] * a + 96 * (255 - a)) / 255), 255};
            fwrite(bgra, 1, 4, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: sprite_probe rom.u7 <spriteId> <paletteId> [swapRB]\n");
        return 1;
    }
    try
    {
        const gp::Rom rom(argv[1]);
        const int id = strtol(argv[2], nullptr, 0);
        const int pid = strtol(argv[3], nullptr, 0);
        const bool swap = argc > 4 && atoi(argv[4]);

        gp::DecodedSprite s;
        if (!gp::decodeSprite(rom, id, s))
        {
            fprintf(stderr, "decode failed for sprite 0x%x\n", id);
            return 2;
        }
        gp::Palette pal;
        const bool havePal = gp::readPalette(rom, pid, pal);
        printf("sprite 0x%x: %dx%d, maxBpp %d, palette 0x%x %s\n",
               id, s.w, s.h, s.maxBpp, pid, havePal ? "ok" : "MISSING (greyscale)");

        std::vector<uint8_t> rgba;
        gp::toRGBA(s, havePal ? &pal : nullptr, swap, rgba);
        char out[64];
        snprintf(out, sizeof out, "probe_%x_%x.bmp", id, pid);
        writeBmp(out, s.w, s.h, rgba);
        printf("wrote %s\n", out);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "error: %s\n", e.what());
        return 3;
    }
}