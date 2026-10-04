// Area rendering for the Gigapets Explorer engine (C++17, header-only).
// Ported from the ROM editor's drawMap / tileCanvas / buildSprite, which are verified for every area.
//
//   background: room-table tile frame (word 2) -> frame table entry -> sheet at seg*0x40 words,
//               tile n at base + n*tileWords; palette = room-table palette id (word 3).
//               Tile index 0 is NOT transparent (palette entry 0 is used); only colours with bit 15 set are.
//   frame attr: bits 0-1 depth (0=2bpp 1=4bpp 2=6bpp 3=8bpp), bits 4-5 width (8<<v), bits 6-7 height (8<<v).
//               (The editor reads width/height the other way round for tile sheets; invisible for square
//               32x32 sheets. This code uses the game's order, as SampleTilePixelValue does.)
//   world scale: one tile cell is always 32x32 world pixels.
//   objects:    type record sprite + palette, drawn at (x - originX - anchorX, y - originY - anchorY + kSpriteYNudge).
#pragma once
#include "objects.hpp"
#include <unordered_map>

namespace gp {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    Image() = default;
    Image(int w_, int h_) : w(w_), h(h_), rgba(size_t(w_) * h_ * 4, 0) {}
};

// The editor found sprites sat 1 px too high without it. Probably a real draw rule, not yet located in the game code.
constexpr int kSpriteYNudge = 1;

inline void indexToRGBA(const Palette* pal, int idx, int bpp, bool swapRB, uint8_t out[4]) {
    if (!pal || idx >= int(pal->colors.size())) {                  // no palette: greyscale (debug)
        const uint8_t g = uint8_t(idx * 255 / ((1 << bpp) - 1));
        out[0] = out[1] = out[2] = g; out[3] = 255; return;
    }
    const uint16_t w = pal->colors[idx];
    if (w & 0x8000) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    int r = (w >> 10) & 31, g = (w >> 5) & 31, b = w & 31;
    if (swapRB) std::swap(r, b);
    out[0] = uint8_t(r * 255 / 31); out[1] = uint8_t(g * 255 / 31); out[2] = uint8_t(b * 255 / 31); out[3] = 255;
}

struct TileSheet {
    int tw = 32, th = 32, bpp = 2;
    uint32_t base = 0, tileWords = 0;
    bool ok = false;
    static TileSheet fromFrame(const Rom& rom, int frameIndex) {
        TileSheet s;
        if (frameIndex < 0 || frameIndex >= kFrameCount) return s;
        const Frame f = readFrame(rom, frameIndex);
        s.bpp = 2 * ((f.attr & 3) + 1);
        s.tw = 8 << ((f.attr >> 4) & 3);
        s.th = 8 << ((f.attr >> 6) & 3);
        s.tileWords = uint32_t(s.tw * s.th * s.bpp / 16);
        s.base = uint32_t(f.seg) * 0x40;
        s.ok = s.tileWords > 0;
        return s;
    }
    bool decode(const Rom& rom, int tile, std::vector<uint8_t>& idx) const {
        if (!ok || tile < 0) return false;
        const uint32_t a = base + uint32_t(tile) * tileWords;
        if (a + tileWords > rom.sizeWords()) return false;
        idx = unpackPixels(rom, a, tw * th, bpp);
        return true;
    }
};

inline void blit(Image& dst, const uint8_t* src, int sw, int sh, int x0, int y0) {   // alpha 0/255, no blending
    for (int y = 0; y < sh; ++y) {
        const int dy = y0 + y; if (dy < 0 || dy >= dst.h) continue;
        for (int x = 0; x < sw; ++x) {
            const int dx = x0 + x; if (dx < 0 || dx >= dst.w) continue;
            const uint8_t* s = &src[(size_t(y) * sw + x) * 4];
            if (!s[3]) continue;
            uint8_t* d = &dst.rgba[(size_t(dy) * dst.w + dx) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
        }
    }
}

// Whole-area background. Tiles that cannot be decoded (id past the sheet / ROM) are magenta.
inline Image renderBackground(const Rom& rom, const Area& area, const TileSheet& sheet, const Palette* pal, bool swapRB = false) {
    Image img(area.tilesW * 32, area.tilesH * 32);
    std::unordered_map<int, std::vector<uint8_t>> cache;
    for (int r = 0; r < area.tilesH; ++r)
        for (int c = 0; c < area.tilesW; ++c) {
            const int id = area.tiles[size_t(r) * area.tilesW + c];
            auto it = cache.find(id);
            if (it == cache.end()) {
                std::vector<uint8_t> px, cell(32 * 32 * 4);
                if (sheet.decode(rom, id, px)) {
                    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)    // nearest scale to a 32x32 cell
                        indexToRGBA(pal, px[size_t(y * sheet.th / 32) * sheet.tw + (x * sheet.tw / 32)], sheet.bpp, swapRB, &cell[(size_t(y) * 32 + x) * 4]);
                } else for (size_t i = 0; i < cell.size(); i += 4) { cell[i] = 255; cell[i + 1] = 0; cell[i + 2] = 255; cell[i + 3] = 255; }
                it = cache.emplace(id, std::move(cell)).first;
            }
            blit(img, it->second.data(), 32, 32, c * 32, r * 32);
        }
    return img;
}

// Draws every object of the area with its type-table sprite and palette, in table order (the table is y-sorted).
inline int drawObjects(Image& img, const Rom& rom, const Area& area, bool swapRB = false) {
    struct Spr { DecodedSprite d; std::vector<uint8_t> rgba; bool ok = false; };
    std::unordered_map<int, Spr> cache;
    int drawn = 0;
    for (const ObjectRec& o : area.objects) {
        auto it = cache.find(o.type);
        if (it == cache.end()) {
            Spr s; const TypeRec t = readTypeRec(rom, o.type);
            if (decodeSprite(rom, t.spriteId, s.d)) {
                Palette pal; const bool has = readPalette(rom, t.paletteId, pal);
                toRGBA(s.d, has ? &pal : nullptr, swapRB, s.rgba); s.ok = true;
            }
            it = cache.emplace(o.type, std::move(s)).first;
        }
        const Spr& s = it->second; if (!s.ok) continue;
        blit(img, s.rgba.data(), s.d.w, s.d.h, o.x - s.d.originX - s.d.anchorX, o.y - s.d.originY - s.d.anchorY + kSpriteYNudge);
        ++drawn;
    }
    return drawn;
}

// Tints the pixels the ground collision sheet blocks (what the player's position cannot enter).
inline void overlayGroundCollision(Image& img, const GroundCollision& gc) {
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (!gc.walkable(x, y)) {
                uint8_t* p = &img.rgba[(size_t(y) * img.w + x) * 4];
                p[0] = uint8_t((p[0] * 6 + 255 * 4) / 10); p[1] = uint8_t(p[1] * 6 / 10); p[2] = uint8_t(p[2] * 6 / 10);
            }
}

} // namespace gp