// npc_brain.hpp - NPC activity selection for the Gigapets Explorer engine (C++17, header-only).
// Port of the decision part of TickAllPets (0x03cc8e), as far as the notes cover it. Movement is npc_walk.hpp.
//
// RandRange(lo, hi) has an EXCLUSIVE hi: (1,101) -> 1..100, (1,4) -> 1..3, (1,100) -> 1..99.
// Original draws seen in the MAME log (return address):
//   ce64 (1,101) activity roll        cd5d (1,4) == 1 -> stop wandering at a waypoint
//   d153 (1,100) == 50 -> reset action   d210 (1,50) == 1 -> start bubble (not ported)
//
// Activity ids: 1, 2, 7 are the rare reaction activities (the call, the happy hop, the yawn: ReactionSeq, run by the
// game) and 0x13 is a trick, performed by TrickShowcase (see NpcActor::trickLevel). 9 = settle after being talked to / after stopping at a waypoint.
// 0x17 = wander along the path. 0x19 / 0x1b / 0x1d = default idles (picked by flag bit 1 / bit 2 / neither).
#pragma once
#include <cstdint>

namespace gp {

constexpr int kActivityTalkedTo = 9;
constexpr int kActivityNoise = 1;    // TickActor_IdleRandomSound: the idle call, `count` times
constexpr int kActivityHappy = 2;    // PlayPositiveReactionAnim: the happy hop, `count` times, of a variant
constexpr int kActivityBored = 7;    // TickActor_PlayReactionSound: the yawn
constexpr int kActivityTrick = 0x13; // TickPetNPCBehavior: the NPC performs a trick of level 1..3 (trick_showcase.hpp)
constexpr int kActivityWander = 0x17;
constexpr int kActivityIdle = 0x19;
constexpr int kActivityIdleA = 0x1b;
constexpr int kActivityIdleB = 0x1d;

// PLACEHOLDER: real idle durations come from animation length (TickActor_SettleFacing etc.), not yet ported.
constexpr int kIdleMinTicks = 30;    // 1 s at 30 Hz
constexpr int kIdleMaxTicks = 91;    // exclusive

struct Rng {
    uint32_t state = 0x9e3779b9u;    // must be non-zero
    uint32_t next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    int range(int lo, int hi) { return hi <= lo ? lo : lo + int(next() % uint32_t(hi - lo)); }   // hi exclusive
};

struct ActivityPick { int id = -1; int param = 0; };   // id -1 = nothing chosen

// The roll table from TickAllPets. `roll` is RandRange(1,101); `flags` is the NPC's behaviorFlags (record word 15).
// Each row needs all of its flag bits set.
inline ActivityPick pickActivity(int roll, int flags) {
    auto has = [&](int m) { return (flags & m) == m; };
    if (roll >= 0x00 && roll <= 0x02 && has(0x08 | 0x10)) return {2, 0};      // param: caller rolls 1..3
    if (roll >= 0x03 && roll <= 0x04 && has(0x20 | 0x10)) return {0x13, 1};
    if (roll >= 0x05 && roll <= 0x06 && has(0x40 | 0x10)) return {0x13, 2};
    if (roll >= 0x07 && roll <= 0x08 && has(0x80 | 0x10)) return {0x13, 3};
    if (roll >= 0x0b && roll <= 0x0c && has(0x200 | 0x10)) return {1, 0};
    if (roll >= 0x0d && roll <= 0x0e && has(0x400 | 0x10)) return {7, 0};
    if (roll >= 0x0f && roll <= 0x18 && has(0x800)) return {kActivityWander, 0};
    return {};
}

// How many times an idle NPC repeats the trick (the g_petActionParam TickAllPets rolls, RandRange with an exclusive hi):
// level 1 once; level 2 the monkey 2..5 times, the others once; level 3 the panda 1..3 times, the others 2..5.
inline int trickRepeatCount(int level, int species, Rng& rng) {
    if (level == 2) return species == 1 ? rng.range(2, 6) : 1;
    if (level == 3) return species == 2 ? rng.range(1, 4) : rng.range(2, 6);
    return 1;
}

// Nothing chosen: 0x1b (flag 1), 0x1d (flag 2), else 0x19. Same rule as SpawnPoolEntryInstance.
inline int defaultActivity(int flags) {
    return (flags & 1) ? kActivityIdleA : (flags & 2) ? kActivityIdleB : kActivityIdle;
}

// The call and the happy hop repeat RandRange(2, 5) times (exclusive hi: 2..4); the hop's variant is RandRange(1, 3).
inline int reactionRepeatCount(Rng& rng) { return rng.range(2, 5); }
inline int happyVariant(Rng& rng) { return rng.range(1, 3); }

// A stationary NPC's own overhead animation (TickAllPets: its record's last word is the animation id, 0xffff = none): with
// none playing, each tick has a 1 in 49 chance (RandRange(1, 0x32) == 1) of starting it, once, at 14 fps over the NPC.
constexpr int kNoOverheadAnim = 0xffff, kOverheadFps = 14;
inline bool overheadStarts(Rng& rng) { return rng.range(1, 0x32) == 1; }
// Ticks the animation takes for `frames` frames at kOverheadFps (it is finished when the frame count has been used up).
constexpr int overheadTicks(int frames) { return frames <= 0 ? 0 : (frames * 30 + kOverheadFps - 1) / kOverheadFps; }

inline int idleTicks(Rng& rng) { return rng.range(kIdleMinTicks, kIdleMaxTicks); }

} // namespace gp
