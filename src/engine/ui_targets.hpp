// ui_targets.hpp - what the game is showing that can be pointed at (the menu bar's buttons, the item grid, a Yes / No box ...), for scripts: ui.targets().
// No SDL here. walk/walk_ui_targets.hpp fills it from the game's own screens.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace gp
{
    struct UiRect
    {
        int x = 0, y = 0, w = 0, h = 0; // in the picture's pixels (320 x 240), like the mouse position
    };
    struct UiScreen
    {
        std::string kind;        // "bar", "items", "tabs", "dialogue"; empty when nothing can be pointed at
        int selected = -1;       // which of items is highlighted (0 based), -1 for none
        std::vector<UiRect> items;
        bool any() const { return !kind.empty() && !items.empty(); }
    };
    // The 0-based number of the item of `s` that the point (x, y) is over, or -1
    inline int uiUnder(const UiScreen &s, float x, float y)
    {
        for (size_t i = 0; i < s.items.size(); ++i)
        {
            const UiRect &r = s.items[i];
            if (x >= float(r.x) && x < float(r.x + r.w) && y >= float(r.y) && y < float(r.y + r.h))
                return int(i);
        }
        return -1;
    }
    // What the script host calls (set by the walk session)
    struct UiHooks
    {
        std::function<UiScreen()> screen;
        std::function<bool(int)> focus; // move the highlight to item i (0 based); false if there is no such item
        std::function<void()> activate; // press Select on the highlighted item
        std::function<bool(const std::string &)> block; // input.block(action): the mouse does not give "select" / "cancel" / "menu" this step; false for another name
    };
} // namespace gp
