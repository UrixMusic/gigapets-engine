// NPC waypoint wandering for the Gigapets Explorer engine (C++17, header-only).
// Port of the path-advance logic duplicated twice in TickAllPets (0x03cc8e): when an NPC has reached its
// current waypoint, the waypoint index advances, bounces or wraps. Movement itself (speed, facing, animation)
// is NOT here: that is TickActor_RunAction.
//
// Per-NPC data: path = list of (dx,dy) offsets from the NPC's spawn point (ROM list ends with x == -1),
// wanderType = npcInfoTable.wanderTypeFlags (1 = ping-pong, anything else = loop),
// state = g_npcWanderPathIndex plus the direction flag (g_petFlags & 0x4000: set = index increases).
// At spawn the index is 0 and the flag is SET (SpawnPoolEntryInstance ORs 0x4000 into the flags).
#pragma once
#include <vector>

namespace gp {

struct PathPoint { int dx, dy; };

struct WanderState {
    int index = 0;
    bool forward = false;   // g_petFlags bit 0x4000
};

// Called when the NPC stands exactly on spawn + path[index]. Mirrors the decompiled branches.
inline void advanceWaypoint(WanderState& s, int pathLen, bool pingPong) {
    if (pathLen <= 0) return;
    if (!s.forward) {
        s.index -= 1;
        if (s.index < 0) {
            if (pingPong) { s.index = 1; s.forward = true; }      // bounce at the start (sets index 1, not 0)
            else s.index = pathLen - 1;                            // loop: wrap to the last point
        }
    } else {
        const int old = s.index;
        s.index += 1;
        if (s.index >= pathLen) {                                  // ran into the -1 terminator
            if (pingPong) { s.index = old - 1; s.forward = false; }
            else s.index = 0;
        }
    }
    if (s.index < 0) s.index = 0;   // the original underflows on 1-point ping-pong paths; clamp instead
    if (s.index >= pathLen) s.index = pathLen - 1;
}

// One decision when the NPC is wandering and not mid-action: returns the world target.
// `atWaypoint` is (position == spawn + path[index]); if false the NPC keeps heading for the same point.
inline PathPoint wanderTarget(WanderState& s, const std::vector<PathPoint>& path, bool pingPong,
                              int spawnX, int spawnY, int posX, int posY) {
    const PathPoint cur = path.empty() ? PathPoint{0, 0} : path[size_t(s.index) < path.size() ? size_t(s.index) : 0];
    const bool atWaypoint = posX == spawnX + cur.dx && posY == spawnY + cur.dy;
    if (atWaypoint) advanceWaypoint(s, int(path.size()), pingPong);
    const PathPoint t = path.empty() ? PathPoint{0, 0} : path[size_t(s.index)];
    return {spawnX + t.dx, spawnY + t.dy};
}

} // namespace gp