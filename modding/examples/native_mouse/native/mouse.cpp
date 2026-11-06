// A native mod that adds the mouse. The game reads held keys through one function, gp::KeyBinds::held(state, action); this mod replaces it:
// the answer is the keyboard's, or the mouse's if the mouse asks for that action.
//   left button   = Select (Z)         middle button = Cancel (X)
//   right button  = walk toward the cursor (the pet is near the middle of the window)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "mods/native_mod.hpp"
#include "engine/keybinds.hpp"

static bool (*origHeld)(const gp::KeyBinds *, const Uint8 *, gp::Action);

static bool mouseWants(gp::Action a)
{
    SDL_Window *w = SDL_GetMouseFocus();
    if (!w)
        return false;
    int mx = 0, my = 0, ww = 1, wh = 1;
    const Uint32 b = SDL_GetMouseState(&mx, &my);
    SDL_GetWindowSize(w, &ww, &wh);
    if (a == gp::Action::Select)
        return (b & SDL_BUTTON_LMASK) != 0;
    if (a == gp::Action::Cancel)
        return (b & SDL_BUTTON_MMASK) != 0;
    if (!(b & SDL_BUTTON_RMASK))
        return false;
    const int dx = mx - ww / 2, dy = my - wh * 145 / 240, dead = wh / 14; // the pet stands 145 of the 240 rows down
    switch (a)
    {
    case gp::Action::Left:  return dx < -dead;
    case gp::Action::Right: return dx > dead;
    case gp::Action::Up:    return dy < -dead;
    case gp::Action::Down:  return dy > dead;
    default:                return false;
    }
}

// a member function is a function whose first argument is the object
static bool myHeld(const gp::KeyBinds *self, const Uint8 *state, gp::Action a) { return origHeld(self, state, a) || mouseWants(a); }

GP_NATIVE_MOD_ABI
extern "C" __declspec(dllexport) void gp_mod_init(const GpNativeApi *api)
{
    SDL_ShowCursor(SDL_ENABLE);
    api->detour("_ZNK2gp8KeyBinds4heldEPKhNS_6ActionE", (void *)myHeld, (void **)&origHeld);
    api->log("mouse control on: left = Z, middle = X, hold right to walk toward the cursor");
}
