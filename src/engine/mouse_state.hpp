// mouse_state.hpp - what the mouse is doing, in the game's own coordinates, for scripts (input.mouse(), ctx.mouse). No SDL here: input_bridge.hpp fills it.
#pragma once

namespace gp
{
    struct MouseState
    {
        bool inside = false;                 // the cursor is over the game picture (not the border bars, not outside the window)
        float x = 0, y = 0;                  // where, in the picture's pixels: 0..320 across and 0..240 down (not clamped: outside the picture they go past that)
        bool left = false, right = false, middle = false;              // held down now
        bool pressedLeft = false, pressedRight = false, pressedMiddle = false; // went down since the last step
        int wheel = 0;                       // wheel notches since the last step (up is positive)
    };
} // namespace gp
