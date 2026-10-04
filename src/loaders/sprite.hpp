// Sprite decoding for the Gigapets Explorer engine (C++17, header-only).
// Format as worked out from the ROM editor + decompile:
//   header  @ word 0x40000 + id*6          (see rom.hpp)
//   piece   @ word 0x30000 + (first+k)*4:  tile, x, y, attr
//       x,y   signed, units of 4 px, measured to the piece CENTRE, y is up
//       attr  bits0-1 depth (0=2bpp 1=4bpp 2=6bpp 3=8bpp), bits4-5 width (8<<v), bits6-7 height (8<<v)
//   pixels  @ word 0x60000 + tile * (pw*ph*bpp/16), continuous bitstream in file byte order (low byte of each word first), MSB first within a byte
//   palette @ word 0x9237 + (palId-0x101)*4: [0]>>8 = colour count, [3] = word offset into bank 0x20000
#pragma once
#include "rom.hpp"
#include <algorithm>
#include <vector>

namespace gp {

constexpr uint32_t kPieceBase = 0x30000;
constexpr uint32_t kPixelBase = 0x60000;
constexpr uint32_t kPaletteTable = 0x9237;
constexpr uint32_t kPaletteBank = 0x20000;

struct Piece { int tile, pw, ph, bpp, px, py; };  // px,py = top-left relative to sprite origin

inline Piece readPiece(const Rom& rom, int index) {
    const uint32_t a = kPieceBase + uint32_t(index) * 4;
    const int attr = rom.u16(a + 3);
    Piece p;
    p.tile = rom.u16(a);
    p.bpp = 2 * ((attr & 3) + 1);
    p.pw = 8 << ((attr >> 4) & 3);
    p.ph = 8 << ((attr >> 6) & 3);
    p.px = 4 * rom.s16(a + 1) - p.pw / 2;
    p.py = -4 * rom.s16(a + 2) - p.ph / 2;
    return p;
}

// Reads n palette indices of `bpp` bits starting at a word address.
// The stream is in FILE BYTE ORDER: low byte of each word first, bits MSB first
// within each byte (the game byte-swaps each loaded word, which gives this order).
inline std::vector<uint8_t> unpackPixels(const Rom& rom, uint32_t wordAddr, int n, int bpp) {
    std::vector<uint8_t> out(n);
    uint32_t acc = 0; int nb = 0; int p = 0;
    const uint32_t mask = (1u << bpp) - 1;
    while (p < n) {
        const uint16_t w = rom.u16(wordAddr++);
        for (int half = 0; half < 2 && p < n; ++half) {
            const uint8_t byte = half == 0 ? uint8_t(w & 0xFF) : uint8_t(w >> 8);
            acc = (acc << 8) | byte; nb += 8;
            while (nb >= bpp && p < n) { out[p++] = uint8_t((acc >> (nb - bpp)) & mask); nb -= bpp; }
            acc &= (1u << nb) - 1;
        }
    }
    return out;
}

struct Palette { std::vector<uint16_t> colors; };

inline bool readPalette(const Rom& rom, int id, Palette& out) {
    if (id <= 0x100) return false;
    const uint32_t e = kPaletteTable + uint32_t(id - 0x101) * 4;
    if (e + 4 > rom.sizeWords()) return false;
    const int cnt = rom.u16(e) >> 8;
    const uint32_t off = kPaletteBank + rom.u16(e + 3);
    if (!cnt || off + cnt > rom.sizeWords()) return false;
    out.colors.resize(cnt);
    for (int k = 0; k < cnt; ++k) out.colors[k] = rom.u16(off + k);
    return true;
}

struct DecodedSprite {
    int w = 0, h = 0;          // canvas size (header box united with all pieces)
    int originX = 0, originY = 0;  // canvas position of the sprite origin (x0,y0 negated)
    int anchorX = 0, anchorY = 0;
    int maxBpp = 2;
    std::vector<uint8_t> idx;  // w*h palette indices, 0 = transparent
    // Draw the canvas at (objX - anchorX - originX, objY - anchorY - originY).
};

inline bool decodeSprite(const Rom& rom, int id, DecodedSprite& out) {
    SpriteHeader h;
    if (!readSpriteHeader(rom, id, h)) return false;
    std::vector<Piece> ps;
    int x0 = 0, y0 = 0, x1 = h.w, y1 = h.h;
    for (int k = 0; k < h.pieceCount; ++k) {
        const uint32_t pa = kPieceBase + uint32_t(h.firstPiece + k) * 4;
        if (pa + 4 > rom.sizeWords()) return false;
        Piece p = readPiece(rom, h.firstPiece + k);
        ps.push_back(p);
        x0 = std::min(x0, p.px); y0 = std::min(y0, p.py);
        x1 = std::max(x1, p.px + p.pw); y1 = std::max(y1, p.py + p.ph);
    }
    out.w = x1 - x0; out.h = y1 - y0;
    out.originX = -x0; out.originY = -y0;
    out.anchorX = h.anchorX; out.anchorY = h.anchorY;
    out.idx.assign(size_t(out.w) * out.h, 0);
    for (const Piece& p : ps) {
        const uint32_t a = kPixelBase + uint32_t(p.tile) * uint32_t(p.pw * p.ph * p.bpp / 16);
        if (a + uint32_t(p.pw * p.ph * p.bpp / 16) > rom.sizeWords()) continue;
        out.maxBpp = std::max(out.maxBpp, p.bpp);
        const auto px = unpackPixels(rom, a, p.pw * p.ph, p.bpp);
        for (int y = 0; y < p.ph; ++y)
            for (int x = 0; x < p.pw; ++x) {
                const uint8_t v = px[y * p.pw + x];
                if (v) out.idx[size_t(p.py - y0 + y) * out.w + (p.px - x0 + x)] = v;  // later pieces draw on top
            }
    }
    return true;
}

// Index -> RGBA. No palette = greyscale (debug only). swapRB flips the 5-5-5 channel order.
inline void toRGBA(const DecodedSprite& s, const Palette* pal, bool swapRB, std::vector<uint8_t>& rgba) {
    rgba.assign(s.idx.size() * 4, 0);
    for (size_t i = 0; i < s.idx.size(); ++i) {
        const int v = s.idx[i];
        if (!v) continue;
        uint8_t* o = &rgba[i * 4];
        if (!pal || v >= (int)pal->colors.size()) {
            const uint8_t g = uint8_t(v * 255 / ((1 << s.maxBpp) - 1));
            o[0] = o[1] = o[2] = g; o[3] = 255; continue;
        }
        const uint16_t w = pal->colors[v];
        if (w & 0x8000) continue;  // transparent colour
        int r = (w >> 10) & 31, g = (w >> 5) & 31, b = w & 31;
        if (swapRB) std::swap(r, b);
        o[0] = uint8_t(r * 255 / 31); o[1] = uint8_t(g * 255 / 31); o[2] = uint8_t(b * 255 / 31); o[3] = 255;
    }
}

} // namespace gp