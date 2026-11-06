// Debug mode: the developer keys (sprint, animation pokes, F-key toggles) only work after this code is typed on the main menu.
// Not the ROM's test mode, and not saved: it is off again every launch.
#pragma once
#include <SDL2/SDL.h>

namespace gp
{
    inline bool &debugMode()
    {
        static bool on = false;
        return on;
    }

    // Up, Down, Up, Down, Menu, Cancel, Cancel, Left, Right, Menu (as the canonical key codes of KeyBinds::canonical).
    class DebugCode
    {
    public:
        static constexpr int kLength = 10;
        static constexpr SDL_Keycode kCode[kLength] = {SDLK_UP, SDLK_DOWN, SDLK_UP, SDLK_DOWN, SDLK_RETURN,
                                                       SDLK_x,  SDLK_x,    SDLK_LEFT, SDLK_RIGHT, SDLK_RETURN};
        // Feeds one key press. True when it completes the code (and then it starts over).
        bool press(SDL_Keycode k)
        {
            if (k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LALT || k == SDLK_RALT)
                return false;
            if (k != kCode[pos_])
                pos_ = (k == kCode[0]) ? 1 : 0;
            else if (++pos_ == kLength)
            {
                pos_ = 0;
                return true;
            }
            return false;
        }
        void reset() { pos_ = 0; }

    private:
        int pos_ = 0;
    };
}
