// Player input and stepping for the Gigapets Explorer engine (C++17, header-only).
// Sources: HandlePetStageEvents (event loops + walking code at 0x142d7..0x14355),
//          PollDirectionalInputAndTickIdle (initial held-flag poll).
//
// Input model: PollInputEvents yields abstract button events (0 left, 1 right, 2 up, 3 down,
// 4 action, 5 secondary, 6 busy-feedback, 0xb power). Presses set the 'held' flags
// (RAM 0x1c74 left, 0x1c75 right, 0x1c76 up, 0x1c77 down) and releases clear them.
// Each tick the game processes RELEASES first, then PRESSES, then walks.
//
// promptState = g_miniPetPromptState (RAM 0x1a99; the loop sets it to 1 on entry):
//   - up/down presses and releases are only processed when it is exactly 1
//   - when it is 0, horizontal steps are always 2 px
//   Name and meaning are UNVERIFIED; it may separate free 8-way walking from a side-view room.
//
// Walking rules (verified from the listing and against 2 recordings, 2,200+ requests):
//   x: LEFT is tested before RIGHT. step = 2 if promptState == 0 or up/down is held, else 3.
//   y: DOWN is tested before UP. step = 2.
// The CALLER must also check (see HandlePetStageEvents): game state == 6, and that
// g_petInteractionMode != 1 when a direction is held (otherwise the game starts pet activity 8 instead).
#pragma once
#include "collision.hpp"
#include <vector>

namespace gp {

enum Dir : uint8_t { DirUp = 1, DirDown = 2, DirLeft = 4, DirRight = 8 };
enum Button : int { BtnLeft = 0, BtnRight = 1, BtnUp = 2, BtnDown = 3, BtnAction = 4, BtnSecondary = 5, BtnBusy = 6, BtnPower = 11 };

inline uint8_t onPress(uint8_t held, int button, int promptState) {
    switch (button) {
        case BtnLeft:  return held | DirLeft;
        case BtnRight: return held | DirRight;
        case BtnUp:    return promptState == 1 ? (held | DirUp) : held;
        case BtnDown:  return promptState == 1 ? (held | DirDown) : held;
        default:       return held;
    }
}
inline uint8_t onRelease(uint8_t held, int button, int promptState) {
    switch (button) {
        case BtnLeft:  return held & ~DirLeft;
        case BtnRight: return held & ~DirRight;
        case BtnUp:    return promptState == 1 ? (held & ~DirUp) : held;     // quirk: ignored in other states
        case BtnDown:  return promptState == 1 ? (held & ~DirDown) : held;
        default:       return held;
    }
}

struct InputTick {
    uint8_t held;
    bool actionPressed = false;      // button 4 sets g_pendingStoryTransition
    bool secondaryPressed = false;   // button 5
};

// One tick of event processing: releases first, then presses.
inline InputTick applyInputEvents(uint8_t held, const std::vector<int>& releases, const std::vector<int>& presses, int promptState) {
    InputTick r{held};
    for (int b : releases) r.held = onRelease(r.held, b, promptState);
    for (int b : presses) {
        r.held = onPress(r.held, b, promptState);
        if (b == BtnAction) r.actionPressed = true;
        if (b == BtnSecondary) r.secondaryPressed = true;
    }
    return r;
}

// PollDirectionalInputAndTickIdle: direct poll of the four buttons, no gating (used before the loop starts).
inline uint8_t pollHeld(bool left, bool right, bool up, bool down) {
    return uint8_t((left ? DirLeft : 0) | (right ? DirRight : 0) | (up ? DirUp : 0) | (down ? DirDown : 0));
}

inline Point stepFor(uint8_t dirs, int promptState = 1) {
    const int h = (dirs & DirLeft) ? -1 : (dirs & DirRight) ? 1 : 0;   // left tested first
    const int v = (dirs & DirDown) ? 1 : (dirs & DirUp) ? -1 : 0;      // down tested first
    const int hStep = (promptState == 0 || v != 0) ? 2 : 3;
    return {h * hStep, v * 2};
}

// One movement tick: request pos + step, let the resolver accept, slide or refuse it.
inline MoveResult stepPlayer(const IWorld& world, Point pos, uint8_t dirs, int promptState = 1) {
    const Point s = stepFor(dirs, promptState);
    return resolveMove(world, {pos.x + s.x, pos.y + s.y}, pos);
}

} // namespace gp