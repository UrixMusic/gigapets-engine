// npc.hpp - NPC records, text resources, sprites and path walking, ported from GigaEditr
#pragma once
#include "room.hpp"
#include "../content/records.hpp"   // IPoint
#include "sprite.hpp"
#include <string>
#include <vector>

namespace gp {

inline uint8_t romByte(const Rom& r, uint32_t byteOff) {
    return (r.u16(byteOff >> 1) >> ((byteOff & 1) * 8)) & 0xff;
}

constexpr uint32_t kNpcTable = 0xdae7;   // word address of record 0 (GigaEditr's 0xdaeb is word 4 of it)
constexpr int kNpcCount = 78, kNpcWords = 16;
constexpr uint32_t kAnimTable = 0x8ad1;  // 5 words per animation id

struct NpcRecord {
    int index;
    bool stationary;        // word 0
    int speciesOrAnim;      // 1: roaming 0 dog / 1 monkey / 2 panda, 0xfffe = player's species; stationary: animation id
    int portraitSprite;     // 2 (dialogue portrait)
    int appearancePalette;  // 3 (also the palette for the world sprite)
    int nameId;             // 4: text resource id
    int area;               // 5
    int x, y;               // 6, 7 (feet anchor, px)
    int halos, pitchforks, stars, lightbulbs;  // 8-11
    int befriendOdds;       // 12
    int wanderFlags;        // 13: 1 = ping-pong path, else loops
    int pathPtr;            // 14
    int behaviorFlags;      // 15: bit 0x800 = follows the path
};

inline NpcRecord readNpc(const Rom& rom, int i) {
    const uint32_t b = kNpcTable + uint32_t(i) * kNpcWords;
    auto w = [&](int k) { return int(rom.u16(b + k)); };
    return {i, w(0) != 0, w(1), w(2), w(3), w(4), w(5), w(6), w(7),
            w(8), w(9), w(10), w(11), w(12), w(13), w(14), w(15)};
}

inline std::vector<NpcRecord> npcsInArea(const Rom& rom, int area) {
    std::vector<NpcRecord> v;
    for (int i = 0; i < kNpcCount; ++i) {
        if (kNpcTable + uint32_t(i + 1) * kNpcWords > rom.sizeWords()) break;
        NpcRecord n = readNpc(rom, i);
        if (n.area == area) v.push_back(n);
    }
    return v;
}

inline std::vector<IPoint> npcPath(const Rom& rom, const NpcRecord& n) {
    std::vector<IPoint> pts;
    if (n.stationary || !(n.behaviorFlags & 0x800)) return pts;
    uint32_t p = n.pathPtr;
    for (int k = 0; k < 64 && p + 2 <= rom.sizeWords(); ++k, p += 2) {
        const int x = rom.u16(p);
        if (x == 0xffff) break;
        pts.push_back({int16_t(x), int16_t(rom.u16(p + 1))});
    }
    return pts;   // offsets from the NPC's spawn point
}

// ---- text resources ----
struct TextTable { uint32_t base; int stride; };   // word address of entry 0, stride in words

// GigaEditr's findNameTable(): entries 95-97 are chunks 0xbd,0xbe,0xc0 and entries 286-288 are 0x23f,0x241,0x242.
// Prefer reading base/stride straight from DecodeTextResource (0x3ff6d) once you have it.
inline bool findTextTable(const Rom& rom, TextTable& out) {
    const uint32_t N = rom.sizeWords();
    for (int s : {1, 2, 3, 4})
        for (uint32_t p = 95u * s; p + 2u * s + 1 < N; ++p) {
            if (rom.u16(p) != 0xbd || rom.u16(p + s) != 0xbe || rom.u16(p + 2 * s) != 0xc0) continue;
            const uint32_t base = p - 95u * s;
            if (base + 289u * s + 1 > N) continue;
            if (rom.u16(base + 286u * s) == 0x23f && rom.u16(base + 287u * s) == 0x241 && rom.u16(base + 288u * s) == 0x242) {
                out = {base, s};
                return true;
            }
        }
    return false;
}

inline std::string textResource(const Rom& rom, const TextTable& t, int id) {
    const int a = rom.u16(t.base + uint32_t(id) * t.stride);
    const int b = rom.u16(t.base + uint32_t(id + 1) * t.stride);
    if (b <= a) return {};                      // the game shows a fallback string here
    std::string s;
    const uint32_t start = 0x20000u * 2 + uint32_t(a) * 8;   // byte offset: word bank 0x20000, 8-byte chunks
    for (int i = 0; i < (b - a) * 8; ++i) {
        const uint8_t c = romByte(rom, start + i);
        if (!c) break;
        s += char(c);
    }
    return s;
}

// ---- animation choice ----
// Roaming NPCs idle with a per-species animation; stationary ones use speciesOrAnim as the animation id directly.
// Render with walk.cpp's loadAnim(); the NPC's palette is appearancePalette (overrides the animation's default).
inline int npcAnimId(const NpcRecord& n, int playerSpecies = 0) {
    static const int kIdle[3] = {0x22, 0x8b, 0xeb};
    int sp = n.speciesOrAnim;
    if (sp == 0xfffe) sp = playerSpecies;
    if (n.stationary) return sp;
    return (sp >= 0 && sp <= 2) ? kIdle[sp] : -1;
}

} // namespace gp