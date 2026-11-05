// indexed_anim_test.cpp [rom] - a mod's animation drawn with a game palette is recoloured with the palette the pet wears: the game's own frames, exported in a
// species' colours and used as a mod anim, come out the same as the game's recolouring of them (the colour machine, potions).
#include "../src/loaders/import.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                                       \
    do                                                                 \
    {                                                                  \
        if (!(c))                                                      \
        {                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);           \
            ++fails;                                                   \
        }                                                              \
    } while (0)

// What --export-pet-frames writes for an animation: the frames in the palette's reference colours, as a mod's anim would hold them.
static bool exportedFrames(const gp::Rom &rom, int anim, int pal, gp::Anim &out)
{
    std::vector<gp::DecodedSprite> sprites;
    std::vector<std::pair<int, int>> offsets;
    gp::Palette palette;
    if (!gp::decodeAnimIndexed(rom, anim, sprites, offsets) || !gp::readPalette(rom, pal, palette))
        return false;
    const std::vector<gp::RefColor> ref = gp::referenceColors(palette);
    for (size_t i = 0; i < sprites.size(); ++i)
    {
        gp::SpriteFrame f;
        f.w = sprites[i].w;
        f.h = sprites[i].h;
        f.anchorX = sprites[i].anchorX;
        f.anchorY = sprites[i].anchorY;
        f.dx = offsets[i].first;
        f.dy = offsets[i].second;
        f.rgba.assign(sprites[i].idx.size() * 4, 0);
        for (size_t p = 0; p < sprites[i].idx.size(); ++p)
        {
            const size_t v = sprites[i].idx[p];
            if (v && v < ref.size() && ref[v].ok)
            {
                f.rgba[p * 4] = ref[v].r;
                f.rgba[p * 4 + 1] = ref[v].g;
                f.rgba[p * 4 + 2] = ref[v].b;
                f.rgba[p * 4 + 3] = 255;
            }
        }
        out.frames.push_back(std::move(f));
    }
    return true;
}

static double sameShare(const gp::Anim &a, const gp::Anim &b)
{
    size_t same = 0, total = 0;
    if (a.frames.size() != b.frames.size())
        return 0;
    for (size_t f = 0; f < a.frames.size(); ++f)
    {
        if (a.frames[f].rgba.size() != b.frames[f].rgba.size())
            return 0;
        for (size_t i = 0; i < a.frames[f].rgba.size(); i += 4)
        {
            ++total;
            const bool ta = a.frames[f].rgba[i + 3] == 0, tb = b.frames[f].rgba[i + 3] == 0;
            same += (ta && tb) || (!ta && !tb && std::equal(&a.frames[f].rgba[i], &a.frames[f].rgba[i] + 3, &b.frames[f].rgba[i])) ? 1 : 0;
        }
    }
    return total ? double(same) / double(total) : 0;
}

int main(int argc, char **argv)
{
    using namespace gp;
    Rom rom(argc > 1 ? argv[1] : "rom.u7");
    ContentDB db;
    for (int sp = 0; sp < 3; ++sp)
    {
        const SpeciesArt &art = artOf(sp);
        const int base = art.colorBase;
        std::vector<int> anims = {art.sitDown, art.eat, art.dig, art.walkSide, art.idleBase, art.posDown};
        for (int n : anims)
        {
            if (n < 0)
                continue;
            const Id ref = importAnim(rom, db, n, base, 0x1e1);
            const Anim *r = db.anim(ref);
            CHECK(r && !r->frames.empty());
            if (!r)
                continue;
            // the exported frames as a mod's anim (what --export-pet-frames writes), drawn with the species' colours
            Anim mod;
            mod.id = "t:anim/s" + std::to_string(sp) + "_" + std::to_string(n);
            CHECK(exportedFrames(rom, n, base, mod));
            mod.refPalette = base;
            db.put(mod);
            const int number = modAnimNumber(mod.id);
            CHECK(importAnim(rom, db, number, base) == mod.id);     // the palette it was drawn with: as it is
            CHECK(importAnim(rom, db, number, -1) == mod.id);       // no palette asked for: as it is
            for (int pal : {art.potions[0], art.potions[1], art.potions[2], art.potions[3], art.potions[4], base + 1, base + 7, base + 20})
            {
                if (pal < 0 || pal == base)
                    continue;
                const Id want = importAnim(rom, db, n, pal, 0x1e1), got = importAnim(rom, db, number, pal);
                CHECK(got == mod.id + "@" + [&] { char b[16]; std::snprintf(b, sizeof b, "%x", pal); return std::string(b); }());
                const double s = sameShare(*db.anim(want), *db.anim(got));
                if (s < 0.995)
                    std::printf("species %d anim %d palette 0x%x: %.4f of the pixels agree\n", sp, n, pal, s);
                CHECK(s >= 0.995);
            }
        }
    }
    { // a soft edge: colours that are not quite the palette's still take the nearest one
        const SpeciesArt &art = artOf(1);
        const Anim *r = db.anim(importAnim(rom, db, art.sitDown, art.colorBase, 0x1e1));
        Anim fuzzy;
        fuzzy.id = "t:anim/fuzzy";
        CHECK(exportedFrames(rom, art.sitDown, art.colorBase, fuzzy));
        fuzzy.refPalette = art.colorBase;
        (void)r;
        for (SpriteFrame &f : fuzzy.frames)
            for (size_t i = 0; i + 3 < f.rgba.size(); i += 4)
                if (f.rgba[i + 3])
                    f.rgba[i] = uint8_t(std::min(255, f.rgba[i] + 2)); // a little off
        db.put(fuzzy);
        const int number = modAnimNumber(fuzzy.id);
        const int pal = art.potions[2];
        const double s = sameShare(*db.anim(importAnim(rom, db, art.sitDown, pal, 0x1e1)), *db.anim(importAnim(rom, db, number, pal)));
        CHECK(s >= 0.98);
    }
    { // an anim that was not drawn with a palette is left alone, whatever the pet wears
        Anim plain;
        plain.id = "t:anim/plain";
        plain.frames = db.anim(importAnim(rom, db, artOf(0).sitDown, artOf(0).colorBase, 0x1e1))->frames;
        db.put(plain);
        const int number = modAnimNumber(plain.id);
        CHECK(importAnim(rom, db, number, artOf(0).potions[1]) == plain.id);
    }
    std::printf(fails ? "indexed_anim_test: %d failure(s)\n" : "indexed_anim_test: all passed\n", fails);
    return fails ? 1 : 0;
}
