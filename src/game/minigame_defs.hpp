// game/minigame_defs.hpp - the base minigames' numbers as records. Nothing here reads the ROM.
// Every game: personality Lightbulbs > fastAbove switches to the "Fast" numbers (the ROM's GetPersonalityStat(3) > 0xc7).
#pragma once
#include <vector>
#include "../content/records.hpp"

namespace gp
{
    inline std::vector<MinigameDef> baseMinigames()
    {
        std::vector<MinigameDef> v;
        { // Swift Pick (area 0, slot 98)
            MinigameDef m;
            m.id = "base:minigame/swift_pick";
            m.handler = "swift_pick";
            m.num = {{"fastAbove", 199}, {"speed", 12}, {"speedFast", 10},
                     {"petX", 0x628}, {"petXJitter", 10}, {"petY", 0x4c2}, {"petYJitter", 15}};
            m.lists["pool"] = {0xf5, 0xf4, 0xf3, 0xf2, 0xf0, 0xef, 0xee, 0, 0xf1, 0xed, 0, 0x105}; // 0 = "Got Away!"
            v.push_back(std::move(m));
        }
        { // Three in a Row (the wishing wells). The cost is per well (an InteractionDef).
            MinigameDef m;
            m.id = "base:minigame/three_in_a_row";
            m.handler = "three_in_a_row";
            m.num = {{"fastAbove", 199}, {"itemsLo", 6}, {"itemsHi", 9}}; // item count = RandRange(itemsLo, itemsHi)
            m.lists["periods"] = {14, 12, 10};
            m.lists["periodsFast"] = {16, 14, 12};
            v.push_back(std::move(m));
        }
        { // Memory Match (the game tents)
            MinigameDef m;
            m.id = "base:minigame/memory_match";
            m.handler = "memory_match";
            m.num = {{"fastAbove", 199}, {"cost", 20}, {"maxWrong", 4}, {"maxWrongFast", 5}};
            v.push_back(std::move(m));
        }
        { // Trick Teacher's code game (the three teachers, NPC records firstTeacher..firstTeacher+2)
            MinigameDef m;
            m.id = "base:minigame/trick_guess";
            m.handler = "trick_guess";
            // cooldownSeconds: a failed lesson locks every teacher for this long (TryBefriendNPC: elapsed / 59.94 > 600)
            m.num = {{"fastAbove", 199}, {"tries", 3}, {"triesFast", 4}, {"firstTeacher", 0x30}, {"cooldownSeconds", 600}};
            m.lists["codeLengths"] = {3, 4, 5}; // per level: how many of the pet's 5 traits form the code
            v.push_back(std::move(m));
        }
        return v;
    }
}