// Dump a contact sheet for every animation record.
// Usage: anim_all <rom> <outdir> [paletteId] [maxId]
// The output folder must already exist.
#include "png.hpp"
#include "render.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Frame
{
    gp::DecodedSprite s;
    std::vector<uint8_t> rgba;
};

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::printf("usage: anim_all <rom> <outdir> [paletteId] [maxId]\n");
        return 1;
    }
    try
    {
        gp::Rom rom(argv[1]);
        const std::string outDir = argv[2];
        const int palId = argc > 3 ? int(std::strtol(argv[3], nullptr, 0)) : 0x1e1;
        const int maxId = argc > 4 ? int(std::strtol(argv[4], nullptr, 0)) : 400;

        gp::Palette pal;
        const bool havePal = palId && gp::readPalette(rom, palId, pal);

        int written = 0;
        for (int id = 0; id < maxId; ++id)
        {
            try
            {
                const uint32_t rec = 0x8ad1 + uint32_t(id) * 5;
                const int start = rom.u16(rec), end = rom.u16(rec + 1);
                const int count = end - start;
                if (count <= 0 || count > 64)
                    continue;

                std::vector<Frame> frames;
                int totalW = 0, maxH = 1;
                for (int k = 0; k < count; ++k)
                {
                    Frame f;
                    if (!gp::decodeSprite(rom, start + k, f.s))
                        continue;
                    gp::toRGBA(f.s, havePal ? &pal : nullptr, false, f.rgba);
                    totalW += f.s.w + 2;
                    maxH = std::max(maxH, f.s.h);
                    frames.push_back(std::move(f));
                }
                if (frames.empty())
                    continue;

                gp::Image sheet(totalW, maxH);
                int x = 0;
                for (const Frame &f : frames)
                {
                    gp::blit(sheet, f.rgba.data(), f.s.w, f.s.h, x, 0);
                    x += f.s.w + 2;
                }

                char path[512];
                std::snprintf(path, sizeof path, "%s/anim_%03d.png", outDir.c_str(), id);
                if (!writePng(path, sheet.w, sheet.h, sheet.rgba))
                {
                    std::printf("cannot write %s\n", path);
                    continue;
                }
                std::printf("anim %3d (0x%02x): sprites %d-%d, %d frames, first %dx%d, duration %d, rate %d\n",
                            id, id, start, end - 1, int(frames.size()),
                            frames[0].s.w, frames[0].s.h, rom.u16(rec + 2), rom.u16(rec + 3));
                ++written;
            }
            catch (const std::exception &)
            {
                // a bad record or out-of-range read: skip this id
            }
        }
        std::printf("wrote %d sheets\n", written);
    }
    catch (const std::exception &e)
    {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}