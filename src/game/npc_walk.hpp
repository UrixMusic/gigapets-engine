// NPC walking for the Gigapets Explorer engine (C++17, header-only).
// Exact port of TickActor_WalkWanderPath (0x01f0xx..0x01f119+): one call = one game tick (30 Hz).
// Actor layout: [0]=x [1]=y [2]=facing [3]=style flag. State: +4 phase, +6 target x, +7 target y, +8 action id.
// Facing: 0 left, 1 right, 2 up, 3 down. The style flag slows horizontal walking to 2 px and (when
// non-zero) blocks the straight-vertical facing; its real meaning is unknown.
// Not ported: UpdateActorSpriteFrame (animation) and what the phases mean for the sprite.
#pragma once
#include <cstdlib>

namespace gp {

enum Facing { FaceLeft = 0, FaceRight = 1, FaceUp = 2, FaceDown = 3 };
constexpr int kActWanderPath = 0x17;

struct WalkActor { int x = 0, y = 0, facing = FaceDown, style = 0; };
struct WalkState { int actionId = 0, phase = 0, tx = 0, ty = 0; };

// Returns true on the tick the walk is reported finished (the call after the target was reached).
inline bool tickWalk(WalkActor& a, WalkState& s, int targetX, int targetY) {
    if (s.actionId != kActWanderPath) { s.actionId = kActWanderPath; s.phase = 1; }
    if (s.phase == 1) {
        s.tx = targetX; s.ty = targetY;
        if (targetX == a.x && a.style == 0) a.facing = (targetY != a.y && a.y <= targetY) ? FaceDown : FaceUp;
        else a.facing = (targetX != a.x && a.x <= targetX) ? FaceRight : FaceLeft;
        s.phase = 2;
        return false;
    }
    if (s.phase == 3) { s.actionId = 0; return true; }
    if (s.phase != 2) return false;

    const int dx = std::abs(s.tx - a.x), dy = std::abs(s.ty - a.y);
    const bool up = a.y != s.ty && s.ty <= a.y;                     // target is above the actor (y before stepping)
    int stepX = 0, stepY = 0;
    if (a.facing == FaceRight) {
        if (dy == 0) stepX = a.style ? 2 : 3;
        else if (dy < dx) { stepX = a.style ? 2 : 3; stepY = up ? -1 : 1; }
        else { stepX = (dx < dy) ? 1 : 2; stepY = up ? -2 : 2; }
        a.x += stepX; a.y += stepY;
        if (a.x >= s.tx) a.x = s.tx;
    } else if (a.facing == FaceLeft) {
        if (dy == 0) stepX = a.style ? -2 : -3;
        else if (dy < dx) { stepX = a.style ? -2 : -3; stepY = up ? -1 : 1; }
        else if (dx < dy) { stepX = -1; stepY = up ? -2 : 2; }
        else { stepX = -2; stepY = up ? -2 : 2; }
        a.x += stepX; a.y += stepY;
        if (!(a.x != s.tx && s.tx <= a.x)) a.x = s.tx;                // snap once x has reached or passed the target
    } else if (a.facing == FaceUp) {
        a.y -= 2;
        if (a.y != s.ty && s.ty <= a.y) return false;                  // still below the target
        a.y = s.ty; s.phase = 3; return false;
    } else if (a.facing == FaceDown) {
        a.y += 2;
        if (a.y < s.ty) return false;
        a.y = s.ty; s.phase = 3; return false;
    } else return false;

    if ((stepY > 0 && s.ty <= a.y) || (stepY < 0 && a.y <= s.ty)) a.y = s.ty;   // snap y when passed
    if (a.x == s.tx && a.y == s.ty) s.phase = 3;
    return false;
}

} // namespace gp
