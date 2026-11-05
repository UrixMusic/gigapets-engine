// game/minigame_defs.hpp - the base minigames' numbers as records. Nothing here reads the ROM.
// Every game: personality Lightbulbs > fastAbove switches to the "Fast" numbers (the ROM's GetPersonalityStat(3) > 0xc7).
#pragma once
#include <vector>
#include "../content/records.hpp"
#include "befriend.hpp"
#include "color_machine.hpp"
#include "item_pools.hpp"
#include "three_in_a_row.hpp"

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
            m.itemLists = {"pool"};
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
        { // The prize pools of Memory Match, Swift Pick's random game and the wishing wells, as lists of item NUMBERS. A repeat in a list
          // is a heavier weight. first / general: BuildUniqueRandomSet (the first slots, then the rest). prizeA..D + special: the 100 Bucks well.
            MinigameDef m;
            m.id = "base:minigame/prize_pools";
            m.handler = "pools";
            m.num = {{"special", ThreeInARow::kPrizeSpecial}}; // goes first unless held or already offered
            m.lists["first"] = {kPoolFirst.begin(), kPoolFirst.end()};
            m.lists["general"] = {kPoolGeneral.begin(), kPoolGeneral.end()};
            m.lists["prizeA"] = {std::begin(ThreeInARow::kPrizeA), std::end(ThreeInARow::kPrizeA)};
            m.lists["prizeB"] = {std::begin(ThreeInARow::kPrizeB), std::end(ThreeInARow::kPrizeB)};
            m.lists["prizeC"] = {std::begin(ThreeInARow::kPrizeC), std::end(ThreeInARow::kPrizeC)};
            m.lists["prizeD"] = {std::begin(ThreeInARow::kPrizeD), std::end(ThreeInARow::kPrizeD)};
            m.itemLists = {"first", "general", "prizeA", "prizeB", "prizeC", "prizeD"};
            v.push_back(std::move(m));
        }
        { // What a friend says and gives (befriend.hpp): gifts are item numbers, advice and defaultLines are text ids.
          // advice: ShowFriendAdviceText (a friend's "hello"); defaultLines: GetDefaultNPCDialogueLine (a wandering NPC's line).
            MinigameDef m;
            m.id = "base:minigame/friend_pools";
            m.handler = "pools";
            m.lists["gifts"] = {kFriendGifts.begin(), kFriendGifts.end()};
            m.itemLists = {"gifts"};
            for (int i = 0; i < kFriendAdviceCount; ++i)
                m.idLists["advice"].push_back("base:text/" + std::to_string(kFriendAdviceFirst + i));
            for (int i = 0; i < kDefaultLineCount; ++i)
                m.idLists["defaultLines"].push_back("base:text/" + std::to_string(kDefaultLineFirst + i));
            v.push_back(std::move(m));
        }
        { // The colour machine in the spaceship: presets = what each step adds to the species' base colour (the ROM's 36), in the order
          // the machine cycles; stepTicks = sim steps between two colours.
            MinigameDef m;
            m.id = "base:minigame/colour_machine";
            m.handler = "colour_machine";
            m.num = {{"stepTicks", kColorStepTicks}};
            m.lists["presets"] = {std::begin(kColorPresetTable), std::end(kColorPresetTable)};
            m.paletteLists = {"presets"};
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
        { // The tricks panel on the menu bar (trick_panel.hpp TrickLayout): its click sound, the question bubbles, the placeholder sprites and where the icons sit
            MinigameDef m;
            m.id = "base:minigame/trick_panel";
            m.handler = "trick_panel";
            m.num = {{"click", 0x59}, {"clickVolume", 0xbe}, {"questionBubble", 10}, {"answerBubble", 9}, {"questionWaitMs", 500},
                     {"unknownLeft", 0x5f}, {"unknownTop", 0x5e}, {"unknownRight", 0x5f},
                     {"knownXLeft", 0x73}, {"knownYLeft", 0xdc}, {"knownXTop", 0x9d}, {"knownYTop", 0xb4}, {"knownXRight", 199}, {"knownYRight", 0xdc},
                     {"unknownXLeft", 0x69}, {"unknownYLeft", 0xdc}, {"unknownXTop", 0x9d}, {"unknownYTop", 0xb4}, {"unknownXRight", 199}, {"unknownYRight", 0xdc}};
            v.push_back(std::move(m));
        }
        return v;
    }
}