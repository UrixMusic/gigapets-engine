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
    // Trick teachers (AttemptTrickLevel1..3 and TickTrickLearnedSuccessSequence1..3). Names are per species 0, 1, 2.
    {"trick_1_0", 0xd678}, {"trick_1_1", 0xd682}, {"trick_1_2", 0xd68e}, // Back Flip, Tail Spring, Somersault
    {"trick_2_0", 0xd6c8}, {"trick_2_1", 0xd6d2}, {"trick_2_2", 0xd6dd}, // Play Dead, Hand Stand, Belly Flop
    {"trick_3_0", 0xd71a}, {"trick_3_1", 0xd724}, {"trick_3_2", 0xd72f}, // Head Spin, Helicopter, Robot Dance
    {"trick_learn_prompt",    0xd73b},   // "Would you like to learn this trick?"
    {"trick_failed_1",        0xd699},   // "Good try! Come back another time to try again."
    {"trick_failed_2",        0xd6e8},   // "Not too bad! Come back another time to try again."
    {"trick_failed_3",        0xd699},
    {"trick_learned_1",       0xd75f},   // "Excellent! You should be able to do this trick on your own now."
    {"trick_learned_2",       0xd79f},   // "Great! ..."
    {"trick_learned_3",       0xd7db},   // "Wow! ..."
    // Using a furnishing from the item menu inside your own home (ItemMenuScreen 0x02c610).
    {"room_display_prompt",   0xb0ac},   // "Display this in your room?"
    {"room_already_shown",    0xb0c7},   // "This item is already displayed in your room." (furniture and beds can't be removed)
    {"room_remove_prompt",    0xb0f4},   // "Remove this item from your room?"
    {"give_prompt",           0xb25e},   // "How many do you want to give?" (the item screen's give mode)
    {"give_no_bed",           0xb1e2},   // "You can't give away the bed that's currently in your room."
    {"give_no_dish",          0xb21d},   // "You can't give away the food dish that's currently in your room."
    {"sell_offer",            0xd622},   // Jeremy: "I like to buy all sorts of things. Do you have something that you would like to sell?"
    {"sell_prompt",           0xb1c4},   // "How many do you want to sell?"
    {"sell_no_bed",           0xb152},   // "You can't sell the bed that's currently in your room."
    {"sell_no_dish",          0xb188},   // "You can't sell the food dish that's currently in your room."
    {"room_removed",          0xb692},   // "This item has been removed from your room." (after a Yes to Remove)
    {"quit_prompt",           0xf660},   // "Are you sure you want to quit the game?" (the house bar's last button)
};

inline void importWideStrings(ContentDB& db, const Rom& rom) {
    for (const WideStr& w : kWideStrings)
        db.put(TextDef{std::string("base:str/") + w.key, readWideText(rom, w.addr)});
}

} // namespace gp