// palette_test.cpp - a mod's palettes: numbers, resolving against the ROM's palettes, and the sprite reader's seam.
#include "../loaders/import.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                             \
    do                                                       \
    {                                                        \
        if (!(c))                                            \
        {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while (0)

static void put16(std::vector<uint8_t> &b, uint32_t wordAddr, uint16_t v)
{
    b[size_t(wordAddr) * 2] = uint8_t(v & 0xff);
    b[size_t(wordAddr) * 2 + 1] = uint8_t(v >> 8);
}

int main()
{
    using namespace gp;
    // a ROM with one palette, number 0x1e0: 4 colours (transparent-ish black, red, green, blue)
    std::vector<uint8_t> bytes(size_t(kPaletteBank + 64) * 2, 0);
    const uint32_t e = kPaletteTable + uint32_t(0x1e0 - 0x101) * 4;
    put16(bytes, e, 4 << 8);
    put16(bytes, e + 3, 0x10);
    for (int i = 0; i < 4; ++i)
        put16(bytes, kPaletteBank + 0x10 + uint32_t(i), uint16_t(i == 1 ? 31 << 10 : i == 2 ? 31 << 5 : i == 3 ? 31 : 0));
    const Rom rom(std::move(bytes));

    Palette base;
    CHECK(readPalette(rom, 0x1e0, base) && base.colors.size() == 4 && base.colors[1] == (31 << 10));
    CHECK(!readPalette(rom, kModPaletteBase + 1, base)); // nothing registered yet

    ContentDB db;
    PaletteDef a; // recolour: entry 1 becomes blue, a new entry 6 appears
    a.id = "m:pal/a";
    a.basedOn = 0x1e0;
    a.set = {{1, 31}, {6, 0x7fff}};
    db.put(a);
    PaletteDef b; // a full list
    b.id = "m:pal/b";
    b.colors = {0, 0x8000, 0x1234};
    db.put(b);
    PaletteDef c; // names a ROM palette that is not there
    c.id = "m:pal/c";
    c.basedOn = 0x2ff;
    db.put(c);
    PaletteDef a2 = a; // the same id again keeps its number
    a2.set = {{1, 0}};
    db.put(a2);

    CHECK(db.palette("m:pal/a")->number == kModPaletteBase + 1 && db.palette("m:pal/b")->number == kModPaletteBase + 2 && db.palette("m:pal/c")->number == kModPaletteBase + 3);
    CHECK(db.allPalettes().size() == 3 && isModPalette(db.palette("m:pal/a")->number) && !isModPalette(0x1e0));

    const std::vector<Id> failed = resolveModPalettes(rom, db);
    CHECK(failed.size() == 1 && failed[0] == "m:pal/c");
    Palette p;
    CHECK(readPalette(rom, db.palette("m:pal/a")->number, p) && p.colors.size() == 4 && p.colors[0] == 0 && p.colors[1] == 0 && p.colors[2] == (31 << 5));
    db.put(a); // (the second version, a2, was the stored one: entry 1 = 0)
    CHECK(resolveModPalettes(rom, db).size() == 1);
    CHECK(readPalette(rom, db.palette("m:pal/a")->number, p) && p.colors[1] == 31 && p.colors.size() == 7 && p.colors[4] == 0x8000 && p.colors[6] == 0x7fff); // grown, gaps see-through
    CHECK(readPalette(rom, db.palette("m:pal/b")->number, p) && p.colors.size() == 3 && p.colors[2] == 0x1234);
    CHECK(!readPalette(rom, db.palette("m:pal/c")->number, p)); // left out
    CHECK(readPalette(rom, 0x1e0, p) && p.colors[1] == (31 << 10)); // the ROM's own is untouched

    // a mod palette turns a sprite's indices into its colours (index 1 = pure blue here)
    DecodedSprite s;
    s.w = 2;
    s.h = 1;
    s.idx = {1, 0};
    Palette q;
    readPalette(rom, db.palette("m:pal/a")->number, q);
    std::vector<uint8_t> rgba;
    toRGBA(s, &q, false, rgba);
    CHECK(rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 255 && rgba[3] == 255 && rgba[7] == 0);

    if (fails)
        std::printf("%d check(s) failed\n", fails);
    else
        std::printf("palette_test: all passed\n");
    return fails ? 1 : 0;
}
