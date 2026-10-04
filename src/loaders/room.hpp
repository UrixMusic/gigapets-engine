// Area / room loading for the Gigapets Explorer engine (C++17, header-only).
// Formats taken from the ROM editor (areas, room table, objects) and the decompile (collision).
//
//   tilemap offsets   word 0xf971 + area            -> map data at word 0x50000 + value (W*H words, row-major)
//   area record       word 0x6bfd + area*8:  [0]=height(tiles) [1]=width(tiles) [2]=object count
//                                            [4]=object table word address [5]=point count [7]=point list address
//   object record     10 words: x, y (feet anchor px), reachX, reachY, type, interactId (0xffff=scenery),
//                               yBottom, yTop, xLeft, xRight
//   room table        word 0xe06b, 73 records x 8 words: [1]=area id, [2]=tile frame, [3]=palette id, [4]=collision frame
//   frame table       word 0x91ab, 35 records x 4 words: [0]=?, [1]=?, [2]=format bits, [3]=data segment (x 0x40 words)
//   collision sheet   frame -> tiles stored linearly at seg*0x40 + tile*tileWords; pixel != 0 means blocked
#pragma once
#include "rom.hpp"
#include <array>
#include <vector>

namespace gp {

constexpr uint32_t kAreaTileOffsets = 0xf971;
constexpr uint32_t kAreaRecords = 0x6bfd;
constexpr uint32_t kTilemapBank = 0x50000;
constexpr uint32_t kRoomTable = 0xe06b;
constexpr int kRoomTableCount = 73;
constexpr uint32_t kFrameTable = 0x91ab;
constexpr int kFrameCount = 35;

// Room bounds margins: hardcoded constants in LoadArea (0x1c0e9..0x1c0f4), identical for every room.
// RAM 0x1aac..0x1aaf = left, right, top, bottom. Room size in px = tiles * 32
// (height at 0x1aaa from area record word 0, width at 0x1aab from word 1).
constexpr int kMarginLeft = 42, kMarginRight = 42, kMarginTop = 62, kMarginBottom = 20;
inline int areaWidthPx(int tilesW) { return tilesW * 32; }
inline int areaHeightPx(int tilesH) { return tilesH * 32; }

struct Frame { uint16_t w0, w1, attr, seg; };

inline Frame readFrame(const Rom& rom, int i) {
    const uint32_t a = kFrameTable + uint32_t(i) * 4;
    return {rom.u16(a), rom.u16(a + 1), rom.u16(a + 2), rom.u16(a + 3)};
}

struct ObjectRec {
    int x, y, reachX, reachY, type, interactId, yBottom, yTop, xLeft, xRight;
    bool interactive() const { return interactId != 0xffff; }
};

struct Area {
    int id = 0;
    int tilesW = 0, tilesH = 0;
    std::vector<uint16_t> tiles;  // tilesW*tilesH tile ids
    std::vector<ObjectRec> objects;
};

// Number of valid areas, using the same sanity rules as the editor.
inline int areaCount(const Rom& rom) {
    int n = 0; uint32_t prev = 0;
    for (int i = 0; i < 128; ++i) {
        const uint32_t v = rom.u16(kAreaTileOffsets + i);
        const uint32_t r = kAreaRecords + uint32_t(i) * 8;
        const int h = rom.u16(r), w = rom.u16(r + 1);
        if (!w || !h || w > 255 || h > 255 || (i && v <= prev) || kTilemapBank + v >= rom.sizeWords()) break;
        prev = v; ++n;
    }
    return n;
}

inline bool readArea(const Rom& rom, int id, Area& out) {
    if (id < 0 || id >= areaCount(rom)) return false;
    const uint32_t r = kAreaRecords + uint32_t(id) * 8;
    out.id = id;
    out.tilesH = rom.u16(r);
    out.tilesW = rom.u16(r + 1);
    const uint32_t mapAddr = kTilemapBank + rom.u16(kAreaTileOffsets + id);
    const size_t n = size_t(out.tilesW) * out.tilesH;
    if (mapAddr + n > rom.sizeWords()) return false;
    out.tiles.resize(n);
    for (size_t k = 0; k < n; ++k) out.tiles[k] = rom.u16(mapAddr + uint32_t(k));
    out.objects.clear();
    const int cnt = rom.u16(r + 2);
    const uint32_t ptr = rom.u16(r + 4);
    if (cnt <= 1000)
        for (int i = 0; i < cnt; ++i) {
            const uint32_t o = ptr + uint32_t(i) * 10;
            if (o + 10 > rom.sizeWords()) break;
            out.objects.push_back({rom.u16(o), rom.u16(o + 1), rom.u16(o + 2), rom.u16(o + 3), rom.u16(o + 4),
                                   rom.u16(o + 5), rom.u16(o + 6), rom.u16(o + 7), rom.u16(o + 8), rom.u16(o + 9)});
        }
    return true;
}

struct RoomLink { int tileFrame, paletteId, collisionFrame; };

// First room-table record whose target area is `id` (the editor keeps the first match too).
inline bool readRoomLink(const Rom& rom, int id, RoomLink& out) {
    for (int i = 0; i < kRoomTableCount; ++i) {
        const uint32_t a = kRoomTable + uint32_t(i) * 8;
        if (a + 8 > rom.sizeWords()) break;
        if (rom.u16(a + 1) == id) { out = {rom.u16(a + 2), rom.u16(a + 3), rom.u16(a + 4)}; return true; }
    }
    return false;
}

// Ground collision: IsTileWalkable. Walkable only when the collision-sheet pixel is 0.
class GroundCollision {
public:
    GroundCollision(const Rom& rom, const Area& area, int collisionFrame, int yOffset = 0)
        : rom_(rom), area_(area), yOffset_(yOffset) {
        const Frame f = readFrame(rom, collisionFrame);
        // depth rule of SampleTilePixelValue: 1 -> 4bpp, 3 -> 8bpp, else 2bpp
        const int d = f.attr & 3;
        bpp_ = d == 1 ? 4 : d == 3 ? 8 : 2;
        pw_ = 8 << ((f.attr >> 4) & 3);   // bits 4-5 width
        ph_ = 8 << ((f.attr >> 6) & 3);   // bits 6-7 height
        tileWords_ = uint32_t(pw_ * ph_ * bpp_ / 16);
        base_ = uint32_t(f.seg) * 0x40;
    }
    // y is raw; the 0x1afa offset is applied here and nowhere else.
    bool walkable(int x, int y) const {
        const int yy = y + yOffset_;
        if (x < 0 || yy < 0) return false;
        const int col = x >> 5, row = yy >> 5;
        if (col >= area_.tilesW || row >= area_.tilesH) return false;
        const uint32_t tile = area_.tiles[size_t(row) * area_.tilesW + col];
        const int px = (x & 31), py = (yy & 31);
        const uint32_t bitIdx = uint32_t(px + py * pw_) * uint32_t(bpp_);   // bits into the tile, file byte order
        const uint32_t byteIdx = bitIdx / 8;
        const uint32_t wordAddr = base_ + tile * tileWords_ + byteIdx / 2;
        if (wordAddr >= rom_.sizeWords()) return false;
        const uint16_t w = rom_.u16(wordAddr);
        const uint8_t byte = (byteIdx & 1) ? uint8_t(w >> 8) : uint8_t(w & 0xFF);
        const int shift = 8 - bpp_ - int(bitIdx % 8);
        return ((byte >> shift) & ((1 << bpp_) - 1)) == 0;
    }
private:
    const Rom& rom_; const Area& area_;
    int yOffset_, bpp_ = 2, pw_ = 32, ph_ = 32; uint32_t tileWords_ = 0, base_ = 0;
};

} // namespace gp