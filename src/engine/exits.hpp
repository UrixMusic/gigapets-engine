// Story exits for the Gigapets Explorer engine (C++17, header-only).
// Source: the tail of HandlePetStageEvents (0x14355..0x14580). After walking, the game reads
// GetActiveStoryObjectId() (RAM 0x1aa5: the nearest interactable object whose reach box contains the
// player) and, for specific (area, object) pairs, leaves the area loop: it sets
// g_storyInteractionSpawnReason, sets the loop state to 3 and calls SwitchGameState().
// No button press is needed; touching the object's reach box is enough.
// The meaning of each spawn reason and the destination are UNVERIFIED (SwitchGameState not read).
#pragma once
#include <cstddef>

namespace gp {

struct StoryExit {
    int area;
    int objectId;
    int spawnReason;       // g_storyInteractionSpawnReason
    int flag1a9d;          // value written to RAM 0x1a9d, or -1 if untouched
    bool consumeAndSpawn;  // ConsumeAllInactivePendingEvents + SpawnPendingEventItems(reason, 6, 0)
};

inline const StoryExit* findStoryExit(int area, int objectId) {
    static const StoryExit table[] = {
        {3, 0x3a, 2, -1, true},   {2, 0x1b, 3, -1, true},   {0, 0x68, 4, -1, true},
        {0, 0x32, 1, -1, true},   {5, 0x49, 1, -1, true},   {5, 0x4a, 1, -1, true},
        {9, 0xe6, 1, -1, true},   {3, 5, 1, -1, true},
        {1, 3, 5, -1, true},      {8, 2, 5, -1, true},
        {1, 0x26, 0, 1, true},    {6, 0x32, 0, 1, true},    {7, 0, 0, 1, true},    {4, 6, 0, 1, true},
        {0, 0x17, 6, 1, true},    {0, 6, 6, 0, true},       {6, 0x2b, 0, 0, true},
        {1, 0x5d, 3, -1, true},   {0x22, 0, 3, -1, true},
        {0, 0x76, 7, -1, true},   {1, 0x17, 9, -1, true},   {5, 4, 8, -1, true},
        {3, 0x1b, 0x22, -1, false},
    };
    for (const StoryExit& e : table) if (e.area == area && e.objectId == objectId) return &e;
    return nullptr;
}

} // namespace gp
