// Sprite animation state for the Gigapets Explorer engine (C++17, header-only).
// Port of PlaySpriteAnimation, IsAnimationBusy and AdvanceSpriteAnimFrame (listing at 0x04dcd8).
//
// Timing: AdvanceSpriteAnimFrame adds g_tickDelta (measured: 2 per game tick, i.e. video frames) to a 32-bit
// timer, multiplies by the frame rate and divides by the immediate 60 (Ghidra labels the helper
// "RemainderUnsigned32"; it must be a divide). So frame number = seconds * frameRate. Only the low 16 bits
// of the quotient are kept.
// Flags: bit 0 finished, bit 1 loop, bit 2 reverse.
#pragma once
#include <cstdint>

namespace gp {

constexpr uint32_t kAnimTimeUnit = 60;   // divisor in AdvanceSpriteAnimFrame
constexpr uint32_t kTickDelta = 2;       // g_tickDelta per game tick, as measured in MAME

struct SpriteAnimState {
    int animId = -1;
    uint32_t flags = 0;          // 1 finished, 2 loop, 4 reverse
    int frameRate = 0;           // frames per second (override or table default)
    int duration = 0;            // stored by PlaySpriteAnimation; the advance function never reads it
    int bank = 0;                // sprite word: 0x1000, or 0x1004 for the mirrored variant
    int ownerX = 0, ownerY = 0;
    int startFrame = 0, currentFrame = 0;
    uint32_t frameTimer = 0;     // 32-bit
};

// durationOverride / rateOverride < 0 mean "use the animation table default".
inline void playSpriteAnimation(SpriteAnimState& s, int animId, int durationOverride, int x, int y, int rateOverride,
                                uint32_t flags, int bank, int frameCount, int defaultDuration, int defaultRate) {
    s.animId = animId; s.ownerX = x; s.ownerY = y; s.flags = flags; s.bank = bank;
    s.duration = durationOverride < 0 ? defaultDuration : durationOverride;
    s.frameRate = rateOverride < 0 ? defaultRate : rateOverride;
    s.startFrame = (flags & 4) ? frameCount - 1 : 0;
    s.currentFrame = 0;
    s.frameTimer = 0;
}

inline bool isAnimationBusy(const SpriteAnimState* s) { return s && (s->flags & 1) == 0; }

inline uint32_t frameNumber(uint32_t timer, int frameRate) {
    const uint32_t product = timer * uint32_t(frameRate);   // 32-bit product
    return (product / kAnimTimeUnit) & 0xFFFFu;
}

// One AdvanceSpriteAnimFrame call. Returns true when the visible frame changed (the original sets g_needsRedraw).
inline bool advanceSpriteAnimFrame(SpriteAnimState& s, int frameCount, uint32_t tickDelta = kTickDelta) {
    if ((s.flags & 1) || frameCount <= 0) return false;
    s.frameTimer += tickDelta;
    const uint32_t n = frameNumber(s.frameTimer, s.frameRate);
    const uint32_t last = uint32_t(frameCount - 1);
    const int before = s.currentFrame;
    if (!(s.flags & 2) && n > last) {                      // one-shot ran past its last frame: finish
        s.currentFrame = (s.flags & 4) ? 0 : int(last);
        s.flags |= 1;
        return true;
    }
    const int rem = int(n % uint32_t(frameCount));
    s.currentFrame = (s.flags & 4) ? frameCount - (rem + 1) : rem;
    return s.currentFrame != before;
}

} // namespace gp