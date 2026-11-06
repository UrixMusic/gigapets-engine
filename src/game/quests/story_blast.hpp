// game/story_blast.hpp
// Two quests end with the pet standing by while something blows up (StartQuestRewardActivity 0x019c82, reward codes -8 and -10,
// which start activities 0x20 and 0x22 in Pet_RunCurrentAction; the decompile calls them the mini-pet found-item sequences).
// Both are three steps: (1) the dialogue closes and a wait begins; (2) the sound 0x5a at volume 200, SpawnMessageBubble(0x13,
// 0x28c, x, y, rate 0xe) at a fixed world spot, StartScriptedCameraPan(0) (the screen shake of the panda's belly flop), and two
// progress bits: the rock's object is cleared and the passage's object is set; (3) another wait, then the dialogue goes on.
// Progress bit n is object slot n of the current area; the engine shows those two objects by quest count (importQuestGates),
// so the blast brings the change forward by one quest.
//
//   code  quest id  area  explosion at   rock (cleared)  passage (set)
//   -8    41        0     (0x579, 0x196)  slot 0x1a       slot 0x19
//   -10   89        1     (0x8a6, 0x111)  slot 5          slot 4
//
// The two waits are soft floats (elapsed ticks / 59.94 against 2.5 and 1.5), read from the listing.
// Codes -9 (clears bit 0x3b) and -11 (sets 0xb8, clears 0xb9) only change objects and have no explosion.
#pragma once

namespace gp
{
    constexpr int kBlastAnim = 0x13, kBlastFps = 0xe;
    constexpr int kBlastLeadTicks = 75; // 2.5 s with the dialogue closed before the blast (the float 0x40200000 against elapsed / 59.94)
    constexpr int kBlastTailTicks = 45; // 1.5 s of aftermath before the dialogue goes on (0x3fc00000; read for -8, assumed the same for -10)

    struct StoryBlast
    {
        int code, area, x, y;
    };

    inline const StoryBlast *storyBlastFor(int code)
    {
        static constexpr StoryBlast kBlasts[2] = {{-8, 0, 0x579, 0x196}, {-10, 1, 0x8a6, 0x111}};
        for (const StoryBlast &b : kBlasts)
            if (b.code == code)
                return &b;
        return nullptr;
    }
}
