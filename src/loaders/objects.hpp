// Object overlay collision for the Gigapets Explorer engine (C++17, header-only).
// Port of CullVisibleNPCs (0x1c7e4, builds the on-screen list at RAM 0xbbb) and
// CheckObjectCollisionAtPoint (0x1c2xx). Overlay pixels come from decodeSprite().
//
//   type record  word 0xe9bd + type*5: [0]=hasMarker [1]=sprite id [2]=palette id [3]=marker tile [4]=overlay sprite id (0xffff=none)
//   object flags: RAM words from 0x540; object index i is present when bit (i & 15) of word 0x540 + (i >> 4) is set
#pragma once
#include "room.hpp"
#include "sprite.hpp"
#include <cstdlib>
#include <unordered_map>

namespace gp {

constexpr uint32_t kTypeTable = 0xe9bd;
constexpr int kScreenW = 320, kScreenH = 240;

struct TypeRec { int hasMarker, spriteId, paletteId, markerTile, overlayId; };

inline TypeRec readTypeRec(const Rom& rom, int type) {
    const uint32_t a = kTypeTable + uint32_t(type) * 5;
    return {rom.u16(a), rom.u16(a + 1), rom.u16(a + 2), rom.u16(a + 3), rom.u16(a + 4)};
}

// Generic bit array over RAM words, bit i = word i>>4, bit i&15.
// Two of these exist in the game (both 15 words = 240 bits), reset together by ResetSpriteSlots (0x1c100):
//   0x540..0x54e  "object present" bits, reset to all ONES. A cleared bit hides an object and stops it blocking.
//   0x531..0x53f  "object used/done" bits (GetProgressBit), reset to all ZEROS. A set bit stops it being interactable.
// ResetSpriteSlots is called from TryTransitionToArea, LoadScriptedQuestData and HandleMainMenuInput,
// NOT from LoadArea itself.
struct ProgressFlags {
    std::vector<uint16_t> words;
    static ProgressFlags allSet(size_t nWords = 15) { return {std::vector<uint16_t>(nWords, 0xFFFF)}; }
    static ProgressFlags allClear(size_t nWords = 15) { return {std::vector<uint16_t>(nWords, 0)}; }
    bool isActive(int idx) const {
        if (idx < 0) return false;
        const size_t w = size_t(idx >> 4);
        return w < words.size() && ((words[w] >> (idx & 15)) & 1);
    }
    void clear(int idx) { if (idx >= 0 && size_t(idx >> 4) < words.size()) words[size_t(idx >> 4)] &= uint16_t(~(1u << (idx & 15))); }
    void set(int idx)   { if (idx >= 0 && size_t(idx >> 4) < words.size()) words[size_t(idx >> 4)] |= uint16_t(1u << (idx & 15)); }
};

// Solid mask of one overlay sprite, in the header box (w x h), origin = box top-left.
struct OverlayMask {
    int w = 0, h = 0, ax = 0, ay = 0;
    std::vector<uint8_t> solid;
    bool at(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && solid[size_t(y) * w + x]; }
};

class ObjectCollision {
public:
    // yOffset = RAM[0x1afa], applied here only (0 so far).
    ObjectCollision(const Rom& rom, const Area& area, const ProgressFlags& flags, int yOffset = 0)
        : rom_(rom), area_(area), flags_(flags), yOffset_(yOffset) {}

    // CullVisibleNPCs: rebuild the visible list for a camera position.
    // Full scan instead of the original's incremental y-window; the set should match except possibly
    // at the window edges. Clip-window mode (g_clipWindowEnabled) is NOT implemented.
    void updateVisible(int camX, int camY) {
        visible_.clear();
        for (size_t i = 0; i < area_.objects.size(); ++i) {
            const ObjectRec& o = area_.objects[i];
            if (!flags_.isActive(int(i))) continue;
            if (o.y + 45 < camY) continue;                       // y window: anchor above the screen by > 45
            if (o.y - 200 >= camY + kScreenH) continue;          // anchor below the screen by >= 200
            if (o.xRight < camX || o.xLeft > camX + kScreenW) continue;
            if (o.yBottom < camY || o.yTop > camY + kScreenH) continue;
            visible_.push_back(int(i));
        }
    }
    const std::vector<int>& visible() const { return visible_; }
    // Inject the game's real list (RAM 0xbbb) to test collision separately from culling.
    void setVisible(std::vector<int> v) { visible_ = std::move(v); }

    // CheckObjectCollisionAtPoint, inverted: true when the point is BLOCKED by an object overlay.
    bool blocks(int x, int y) const {
        const int yy = y + yOffset_;
        for (int idx : visible_) {
            const ObjectRec& o = area_.objects[size_t(idx)];
            const TypeRec t = readTypeRec(rom_, o.type);
            if (t.overlayId == 0xffff) continue;
            const OverlayMask* m = mask(t.overlayId);
            if (!m) continue;
            const int left = o.x - m->ax, top = o.y - m->ay;
            if (m->at(x - left, yy - top)) return true;         // pixel index > 0
        }
        return false;
    }

private:
    const OverlayMask* mask(int spriteId) const {
        auto it = cache_.find(spriteId);
        if (it != cache_.end()) return it->second.w ? &it->second : nullptr;
        OverlayMask m;
        SpriteHeader h; DecodedSprite d;
        if (readSpriteHeader(rom_, spriteId, h) && decodeSprite(rom_, spriteId, d)) {
            m.w = h.w; m.h = h.h; m.ax = h.anchorX; m.ay = h.anchorY;
            m.solid.assign(size_t(m.w) * m.h, 0);
            for (int y = 0; y < m.h; ++y)
                for (int x = 0; x < m.w; ++x)
                    m.solid[size_t(y) * m.w + x] = d.idx[size_t(y + d.originY) * d.w + (x + d.originX)] != 0;
        }
        OverlayMask& slot = cache_[spriteId];
        slot = std::move(m);
        return slot.w ? &slot : nullptr;
    }

    const Rom& rom_; const Area& area_; const ProgressFlags& flags_;
    int yOffset_;
    std::vector<int> visible_;
    mutable std::unordered_map<int, OverlayMask> cache_;
};

// FindNearestInteractableObject (0x1c117). Result goes to RAM 0x1aa5 (g_activeStoryObjectId): the index of the
// nearest interactable object within reach, or -1. `doneBits` is the 0x531 bit array (assumed to use the same
// bit layout as the 0x540 one; GetProgressBit itself has not been read). Distances use 16-bit arithmetic like the original.
inline int findNearestInteractable(const Area& area, const std::vector<int>& visible,
                                   const ProgressFlags& doneBits, int playerX, int playerY) {
    int best = -1; uint16_t bestD2 = 0xFFFF;
    for (int idx : visible) {
        const ObjectRec& o = area.objects[size_t(idx)];
        if (o.interactId == 0xffff) continue;                 // scenery
        if (doneBits.isActive(idx)) continue;                 // GetProgressBit(idx) == 1 -> skip
        const int dx = std::abs(playerX - o.x), dy = std::abs(playerY - o.y);
        if (dx >= o.reachX || dy >= o.reachY) continue;       // must be inside the reach box
        const uint16_t d2 = uint16_t(uint16_t(dx * dx) + uint16_t(dy * dy));
        if (d2 < bestD2) { best = idx; bestD2 = d2; }
    }
    return best;
}

} // namespace gp