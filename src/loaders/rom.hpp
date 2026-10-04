// ROM access for the Gigapets Explorer engine (C++17, header-only).
// The ROM is a flat dump of 16-bit little-endian words. All game addresses are
// WORD addresses, so file byte offset = wordAddr * 2. Keep that rule in this file only.
#pragma once
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gp {

class Rom {
public:
    explicit Rom(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open ROM: " + path);
        data_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    // For tests: build a ROM from raw bytes.
    explicit Rom(std::vector<uint8_t> bytes) : data_(std::move(bytes)) {}

    size_t sizeWords() const { return data_.size() / 2; }

    uint16_t u16(uint32_t wordAddr) const {
        const size_t o = size_t(wordAddr) * 2;
        if (o + 2 > data_.size()) throw std::out_of_range("ROM read past end");
        return uint16_t(data_[o] | (data_[o + 1] << 8));
    }
    int16_t s16(uint32_t wordAddr) const { return int16_t(u16(wordAddr)); }

private:
    std::vector<uint8_t> data_;
};

// Sprite header table: 6 words per sprite at word 0x40000 (see format notes).
struct SpriteHeader {
    int w, h, pieceCount;
    int anchorX, anchorY;   // signed
    int firstPiece;         // index into the piece table at word 0x30000
};

constexpr uint32_t kSpriteHeaderBase = 0x40000;

// Returns false for ids that are out of range or fail the sanity limits
// the ROM editor uses (w,h <= 512, 1..64 pieces).
inline bool readSpriteHeader(const Rom& rom, int id, SpriteHeader& out) {
    if (id < 0) return false;
    const uint32_t a = kSpriteHeaderBase + uint32_t(id) * 6;
    if (a + 6 > rom.sizeWords()) return false;
    out.w = rom.u16(a);
    out.h = rom.u16(a + 1);
    out.pieceCount = rom.u16(a + 2);
    out.anchorX = rom.s16(a + 3);
    out.anchorY = rom.s16(a + 4);
    out.firstPiece = rom.u16(a + 5);
    return out.w && out.h && out.w <= 512 && out.h <= 512 &&
           out.pieceCount && out.pieceCount <= 64;
}

} // namespace gp
