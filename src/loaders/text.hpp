// loaders/text.hpp
#pragma once
#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "../content/db.hpp"
#include "rom.hpp"

namespace gp {

// Where the packed text lives (all WORD addresses).
struct TextPool {
    uint32_t tableWord;      // word address of name-table entry 0
    uint32_t strideWords;    // words per table entry
    uint32_t poolWord;       // word address of chunk 0
    int      maxEntries = 1000;
};

// Port of GigaEditr's findNameTable(): look for the chunk numbers of names 95-97 (0xbd, 0xbe, 0xc0),
// accept only if names 286-288 are 0x23f, 0x241, 0x242.
inline std::optional<TextPool> findTextPool(const Rom& rom) {
    const uint32_t n = uint32_t(rom.sizeWords());
    for (uint32_t sw : {1u, 2u, 3u, 4u})
        for (uint32_t p = 95 * sw; p + 2 * sw < n; p++) {
            if (rom.u16(p) != 0xbd || rom.u16(p + sw) != 0xbe || rom.u16(p + 2 * sw) != 0xc0) continue;
            const uint32_t base = p - 95 * sw;
            if (base + 289 * sw + 1 > n) continue;
            if (rom.u16(base + 286 * sw) == 0x23f && rom.u16(base + 287 * sw) == 0x241 &&
                rom.u16(base + 288 * sw) == 0x242)
                return TextPool{base, sw, 0x10000};   // POOL0 = byte 0x20000 = word 0x10000
        }
    return std::nullopt;
}

// Packed text: 2 chars per word, low byte first, stops at a 0 byte or after spanBytes.
inline std::string readPackedText(const Rom& rom, uint32_t startWord, uint32_t spanBytes) {
    std::string s;
    if (startWord >= rom.sizeWords()) return s;
    spanBytes = std::min<uint32_t>(spanBytes, (rom.sizeWords() - startWord) * 2);
    for (uint32_t k = 0; k < spanBytes; k++) {
        const uint16_t w = rom.u16(startWord + k / 2);
        const uint8_t b = (k & 1) ? uint8_t(w >> 8) : uint8_t(w & 0xff);
        if (!b) break;
        s += char(b);
    }
    return s;
}

// Wide literal: one char per word (Ghidra's "u_..." strings). wordAddr = the Ghidra address.
inline std::string readWideText(const Rom& rom, uint32_t wordAddr, int maxLen = 200) {
    std::string s;
    for (int k = 0; k < maxLen && wordAddr + k < rom.sizeWords(); k++) {
        const uint16_t w = rom.u16(wordAddr + k);
        if (!w) break;
        s += char(w & 0xff);
    }
    return s;
}

// Each table entry points at an 8-byte chunk (4 words) and owns everything up to the next
// referenced chunk, at most 16 chunks.
inline void importText(ContentDB& db, const Rom& rom, const TextPool& t) {
    std::vector<uint16_t> chunkOf;
    uint16_t prev = 0;
    for (int i = 0; i < t.maxEntries; i++) {
        const uint32_t p = t.tableWord + uint32_t(i) * t.strideWords;
        if (p >= rom.sizeWords()) break;
        const uint16_t c = rom.u16(p);
        if (t.poolWord + uint32_t(c) * 4 + 4 > rom.sizeWords()) break; // points outside the ROM
        if (i > 0 && c < prev) break;                                  // the table is increasing; a drop means it ended
        chunkOf.push_back(c);
        prev = c;
    }
    std::printf("importText: %zu entries\n", chunkOf.size());
    std::set<uint16_t> sorted(chunkOf.begin(), chunkOf.end());
    for (size_t i = 0; i < chunkOf.size(); i++) {
        const uint32_t c = chunkOf[i];
        const auto nx = sorted.upper_bound(chunkOf[i]);
        const uint32_t endC = std::min<uint32_t>(nx == sorted.end() ? c + 16 : *nx, c + 16);
        db.put(TextDef{"base:text/" + std::to_string(i),
                       readPackedText(rom, t.poolWord + c * 4, (endC - c) * 8)});
    }
}

// Sentences the befriending code uses, read straight from the ROM by their Ghidra addresses.
struct WideStr { const char* key; uint32_t addr; };
constexpr WideStr kWideStrings[] = {
    {"stats_title",           0xd95d},   // "%s's Stats"
    {"hint_more_pitchforks",  0xd99c},
    {"hint_fewer_halos",      0xd96d},
    {"hint_fewer_pitchforks", 0xd9cf},
    {"hint_more_halos",       0xda03},
    {"greet_meet",            0xd894},   // "Nice to meet you, %s."
    {"greet_again",           0xd875},   // "Hi, %s. Nice to see you again."
    {"gift_meet",             0xd8e0},   // "I have a gift for you!"
    {"gift_again",            0xd8aa},
    {"game_meet",             0xd935},   // "Would you like to play a game?"
    {"game_again",            0xd8f7},
    {"fmt_name_and_line",     0xd954},   // unnamed format string: print it and see
    {"fmt_stats_first",       0xd968},   // unnamed format string: print it and see
};

inline void importWideStrings(ContentDB& db, const Rom& rom) {
    for (const WideStr& w : kWideStrings)
        db.put(TextDef{std::string("base:str/") + w.key, readWideText(rom, w.addr)});
}

} // namespace gp