// NPC/pet animation selection and idle state machines for the Gigapets Explorer engine (C++17, header-only).
// Ports of UpdateActorSpriteFrame, PlayDirectionalIdleAnim, PlayTurnAroundIdleAnim (animation id tables)
// and TickActor_SettleFacing (activity 9) / TickActor_WalkInThenIdle (activity 0x19).
//
// Actor fields used (word offsets): [2] facing (0 left, 1 right, 2 up, 3 down), [3] style flag
// (non-zero = side-view-only walker), [4] kind (index 0..2 into every table; index 0 matches the dog ids
// 0x34..0x38 your notes found, so 1 and 2 are the other two species), [5] colourway/palette argument.
//
// "altBank" is the original's sprite-bank/attribute word argument: default 0x1000, or g_npcSpawnParam2.
// It is used to show the same art mirrored (left walk uses the alt bank, right walk does not; the idle and
// turn-around art is drawn the other way round, so they use it when facing right). That it is a flip is an
// inference from this pairing, not something the code states.
#pragma once
#include "npc_walk.hpp"

namespace gp {

enum AnimMode { AnimOnce = 0, AnimLoop = 2 };   // last-but-one argument of PlaySpriteAnimation

struct AnimChoice { int animId = -1; bool altBank = false; int mode = AnimOnce; };

namespace tables {
constexpr int kWalkA[3] = {0x38, 0x9f, 0xfa};     // side view (left/right)
constexpr int kWalkB[3] = {0x37, 0x9e, 0xec};     // walking down (toward the camera)
constexpr int kWalkC[3] = {0x36, 0x9d, 0xe6};     // walking up (away from the camera)
constexpr int kWalkD[3] = {0x4e, 0xb5, 0x114};    // side-view-only walkers (style flag set)
constexpr int kIdleDir[3]  = {0x35, 0x9c, 0x106};
constexpr int kIdleDirD[3] = {0x4c, 0xb3, 0x11c}; // style flag set
constexpr int kTurn[3]     = {0x34, 0x9b, 0x105};
}

inline int pick(const int (&t)[3], int kind) { return (kind >= 0 && kind < 3) ? t[kind] : -1; }

// UpdateActorSpriteFrame: called when a walk starts; the animation then loops for the whole walk.
inline AnimChoice walkAnim(int kind, int facing, int style) {
    AnimChoice c; c.mode = AnimLoop;
    if (style == 0) {
        if (facing == FaceLeft)       { c.animId = pick(tables::kWalkA, kind); c.altBank = true; }
        else if (facing == FaceRight) { c.animId = pick(tables::kWalkA, kind); }
        else if (facing == FaceUp)    { c.animId = pick(tables::kWalkC, kind); }
        else if (facing == FaceDown)  { c.animId = pick(tables::kWalkB, kind); }
    } else {
        if (facing == FaceLeft)       { c.animId = pick(tables::kWalkD, kind); c.altBank = true; }
        else if (facing == FaceRight) { c.animId = pick(tables::kWalkD, kind); }
    }
    return c;
}
inline AnimChoice directionalIdleAnim(int kind, int facing, int style) {          // PlayDirectionalIdleAnim
    return {pick(style == 0 ? tables::kIdleDir : tables::kIdleDirD, kind), facing == FaceRight, AnimOnce};
}
inline AnimChoice turnAroundAnim(int kind, int facing) {                          // PlayTurnAroundIdleAnim
    return {pick(tables::kTurn, kind), facing == FaceRight, AnimOnce};
}

// What the idle state machines need from the engine.
struct IdleHost {
    virtual ~IdleHost() = default;
    virtual void play(const AnimChoice& a) = 0;
    virtual void playScriptedIdle() = 0;             // PlayIdleAnim_Scripted(actor, 0): not yet read
    virtual bool animBusy() const = 0;               // IsAnimationBusy
    virtual int rand(int lo, int hi) = 0;            // [lo, hi), like RandRange
};

struct IdleActor { int facing = FaceDown, style = 0, kind = 0; };
struct IdleState { int actionId = 0, phase = 0; };

constexpr int kActSettleFacing = 9, kActWalkInThenIdle = 0x19;

// Activity 9. Returns true when finished. Ends with the actor facing down.
inline bool tickSettleFacing(IdleActor& a, IdleState& s, IdleHost& h) {
    if (a.facing == FaceDown) return true;
    if (s.actionId != kActSettleFacing) { s.actionId = kActSettleFacing; s.phase = 1; }
    switch (s.phase) {
        case 1:
            if (a.facing == FaceUp) {                                  // turn away-from-camera round to a random side
                a.facing = h.rand(1, 3) != 1 ? FaceRight : FaceLeft;
                h.play(turnAroundAnim(a.kind, a.facing)); s.phase = 2;
            } else s.phase = 3;
            break;
        case 2: if (!h.animBusy()) s.phase = 3; break;
        case 3: h.play(directionalIdleAnim(a.kind, a.facing, a.style)); s.phase = 4; break;
        case 4: if (!h.animBusy()) { a.facing = FaceDown; s.actionId = 0; return true; } break;
    }
    return false;
}

// Activity 0x19, the default idle. The random call's arguments were lost by the decompiler; (1,3) is assumed
// because it is a left/right coin flip exactly like the one in tickSettleFacing.
inline bool tickWalkInThenIdle(IdleActor& a, IdleState& s, IdleHost& h) {
    if (s.actionId != kActWalkInThenIdle) { s.actionId = kActWalkInThenIdle; s.phase = 1; }
    switch (s.phase) {
        case 1:
            if (a.facing == FaceUp) {
                a.facing = h.rand(1, 3) != 1 ? FaceRight : FaceLeft;
                h.play(turnAroundAnim(a.kind, a.facing)); s.phase = 2;
            } else { h.playScriptedIdle(); s.phase = 4; }
            break;
        case 2: if (!h.animBusy()) { h.play(directionalIdleAnim(a.kind, a.facing, a.style)); s.phase = 3; } break;
        case 3: if (!h.animBusy()) { h.playScriptedIdle(); s.phase = 4; } break;
        case 4: if (!h.animBusy()) { s.actionId = 0; return true; } break;
    }
    return false;
}

} // namespace gp