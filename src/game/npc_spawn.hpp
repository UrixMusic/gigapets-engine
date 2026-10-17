// NPC spawning rules for the Gigapets Explorer engine (C++17, header-only).
// Port of SpawnPoolEntriesForStage (area filter) and SpawnPoolEntryInstance (initial instance state).
// Pool entries are numbered 0..0x4d. Info fields come from the NPC info table (stride 16 words).
#pragma once
#include "wander.hpp"

namespace gp {

constexpr int kPoolEntryCount = 0x4e;
constexpr int kQuestGatedPoolEntry = 0x36;   // spawns in area 3 even before the quest gate

struct NpcSpawnInfo {
    int areaId = 0, spawnX = 0, spawnY = 0;
    int behaviorFlags = 0;       // bitfield for moving NPCs; for stationary NPCs the original reuses it as a bubble anim id
    int speciesId = 0;           // -2 = use the player's species; for stationary NPCs it holds the looping anim id
    bool stationary = false;
};

struct NpcInstanceInit {
    int x = 0, y = 0;
    int facing = 3;              // g_npcApproachSide: starts facing down
    int style = 0;               // g_npcSpawnParam2 (the spawn call's second argument; 0 for pool NPCs)
    int kind = 0;                // species id
    int flags = 0;               // g_petFlags
    int pendingActivity = 0;
    WanderState wander;          // index 0; direction flag starts SET (forward) for moving NPCs
    bool stationary = false;
};

// SpawnPoolEntriesForStage filter.
inline bool npcSpawnsInArea(int entry, const NpcSpawnInfo& info, int area, int questsCompleted) {
    if (info.areaId != area) return false;
    return area != 3 || questsCompleted > 0x10 || entry == kQuestGatedPoolEntry;
}

// SpawnPoolEntryInstance.
inline NpcInstanceInit initNpcInstance(const NpcSpawnInfo& info, int playerSpecies, int spawnParam2 = 0) {
    NpcInstanceInit n;
    n.x = info.spawnX; n.y = info.spawnY; n.style = spawnParam2;
    n.kind = info.speciesId == -2 ? playerSpecies : info.speciesId;
    n.stationary = info.stationary;
    if (info.stationary) { n.flags = 0; n.pendingActivity = 0x19; return n; }
    n.flags = info.behaviorFlags | 0x4000;
    n.wander = WanderState{0, true};
    n.pendingActivity = (info.behaviorFlags & 1) ? 0x1b : (info.behaviorFlags & 2) ? 0x1d : 0x19;
    return n;
}

} // namespace gp