// fixed_spots.hpp - the base game's fixed placements (dig_notes.md "Fixed hidden items" and "Legacy visible
// items"). These live in Game_ProcessTick's code, not in a ROM table, so they are transcribed here once and
// imported into ContentDB like everything else:   for (const FixedSpotDef& s : baseFixedSpots()) db.put(s);
#pragma once
#include <string>
#include <vector>
#include "../content/records.hpp"

namespace gp
{

    inline std::vector<FixedSpotDef> baseFixedSpots()
    {
        enum : int
        {
            kNotHeld = 1,
            kNotOffered = 2,
            kNotLegacy = 4
        };
        struct Row
        {
            int area, item, x, y, prereq, trigger, flags, grant;
        };
        // Hidden quest items (state 1). trigger -1 + kNotOffered = lost-and-found (respawns until handed to the King).
        static const Row kHidden[] = {
            {0, 173, 436, 266, 188, -1, kNotHeld | kNotOffered, 0},
            {0, 5, 218, 1151, 193, 0, 0, 0},
            {0, 124, 1662, 898, 0, 4, 0, 0},
            {0, 101, 885, 898, 0, 16, 0, 0},
            {0, 102, 1269, 901, 0, 17, 0, 0},
            {1, 96, 840, 787, 0, 15, 0, 0},
            {1, 144, 535, 565, 195, 2, 0, 0},
            {1, 194, 2121, 1171, 196, 3, 0, 196}, // also spawned the moment 196 is granted
            {1, 199, 2416, 1064, 198, 8, kNotHeld, 0},
            {2, 162, 1173, 1251, 0, 12, 0, 0},
            {2, 180, 808, 248, 191, -1, kNotHeld | kNotOffered, 0},
            {3, 13, 1209, 787, 0, 5, 0, 0},
            {3, 157, 1570, 1273, 0, 14, 0, 0},
            {4, 181, 372, 567, 192, -1, kNotHeld | kNotOffered, 0},
            {5, 197, 286, 978, 0, 6, kNotHeld, 0},
            {6, 17, 331, 683, 0, 9, 0, 0},
            {6, 93, 271, 438, 0, 13, 0, 0},
            {6, 179, 1536, 537, 199, -1, kNotHeld | kNotOffered, 0},
            {7, 178, 589, 1776, 190, -1, kNotHeld | kNotOffered, 0},
            {7, 198, 1399, 319, 197, 7, kNotHeld, 0},
            {9, 9, 1206, 687, 0, 10, 0, 0},
            {9, 18, 1060, 1060, 0, 11, kNotHeld, 0},
            {9, 135, 426, 657, 194, 1, 0, 0},
        };
        // Legacy items lying on the ground (state 2): placed while not held and the legacy flag is clear.
        static const Row kLegacy[] = {
            {4, 224, 554, 288, 0, -1, kNotHeld | kNotLegacy, 0},
            {6, 225, 2252, 231, 0, -1, kNotHeld | kNotLegacy, 0},
            {2, 226, 748, 215, 0, -1, kNotHeld | kNotLegacy, 0},
            {6, 227, 246, 225, 0, -1, kNotHeld | kNotLegacy, 0},
            {5, 228, 1004, 1416, 0, -1, kNotHeld | kNotLegacy, 0},
            {9, 231, 1088, 961, 0, -1, kNotHeld | kNotLegacy, 0},
            {7, 233, 1399, 2201, 0, -1, kNotHeld | kNotLegacy, 0},
            {8, 234, 126, 451, 0, -1, kNotHeld | kNotLegacy, 0},
        };

        std::vector<FixedSpotDef> out;
        auto add = [&](const Row &r, int state)
        {
            FixedSpotDef s;
            s.id = "base:spot/" + std::to_string(r.item);
            s.area = r.area;
            s.item = legacyItemId(r.item);
            s.x = r.x;
            s.y = r.y;
            s.state = state;
            s.prereqItem = r.prereq ? legacyItemId(r.prereq) : Id();
            // baseFixedSpots(): replace `s.trigger = r.trigger;`
            s.trigger = r.trigger;
            s.notWhileHeld = (r.flags & kNotHeld) != 0;
            s.notOffered = (r.flags & kNotOffered) != 0;
            s.notLegacyFlag = (r.flags & kNotLegacy) != 0;
            s.spawnOnGrantItem = r.grant ? legacyItemId(r.grant) : Id();
            out.push_back(std::move(s));
        };
        for (const Row &r : kHidden)
            add(r, 1);
        for (const Row &r : kLegacy)
            add(r, 2);
        return out;
    }

} // namespace gp