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

struct InteractExit { int area, objectId, to; };

inline const InteractExit* findInteractExit(int area, int objectId) {
    static const InteractExit table[] = {
        // entrances: outdoor -> interior
        {0,61,11}, {0,76,10}, {1,56,12}, {1,59,15}, {1,60,14}, {1,62,17}, {1,63,13},
        {1,73,21}, {1,76,16}, {1,77,18}, {1,78,19}, {2,9,31}, {2,23,32}, {3,15,33},
        {4,2,36},  {4,3,37},  {5,52,35}, {6,5,26},  {6,29,25}, {7,19,23}, {7,69,24},
        {9,32,28}, {9,34,30}, {9,204,27}, {9,205,29},
        // exits: interior -> outdoor
        {12,2,1}, {13,2,1}, {14,2,1}, {15,5,1}, {16,1,1}, {17,2,1}, {18,2,1},
        {19,1,1}, {20,0,1}, {21,2,1}, {22,2,1}, {10,3,0}, {11,0,0}, {31,3,2},
        {32,4,2}, {33,4,3}, {36,4,4}, {37,3,4}, {35,3,5}, {25,3,6}, {26,1,6},
        {23,0,7}, {24,3,7}, {27,2,9}, {28,0,9}, {29,1,9}, {30,0,9},
    };
    for (const InteractExit& e : table) if (e.area == area && e.objectId == objectId) return &e;
    return nullptr;
}

} // namespace gp
