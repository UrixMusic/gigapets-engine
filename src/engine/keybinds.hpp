// keybinds.hpp - which key does what: Up, Down, Left, Right, Select, Cancel and Menu.
//
// The game's code reads those seven keys in a lot of places as SDL's arrow keys, Z, X and Enter. Rather than change every place,
// canonical() turns a rebound key into the default key of its action: a key bound to Select reads as Z, one bound to Menu as
// Enter, and so on, and a default key that no longer belongs to any action reads as nothing. Keys that aren't bound to an action
// (the debug letters, the function keys) pass through untouched. Polled input asks held() instead.
#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <string>

namespace gp
{

    enum class Action : int
    {
        Up,
        Down,
        Left,
        Right,
        Select,
        Cancel,
        Menu,
        kCount
    };
    constexpr int kActionCount = int(Action::kCount);
    constexpr SDL_Scancode kDefaultKeys[kActionCount] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
                                                         SDL_SCANCODE_Z,  SDL_SCANCODE_X,    SDL_SCANCODE_RETURN};
    constexpr SDL_Keycode kCanonicalKeys[kActionCount] = {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_z, SDLK_x, SDLK_RETURN};
    constexpr const char *kActionNames[kActionCount] = {"Up", "Down", "Left", "Right", "Select", "Cancel", "Menu"};

    class KeyBinds
    {
    public:
        std::array<int, kActionCount> key{}; // scancodes

        KeyBinds() { reset(); }

        void reset()
        {
            for (int a = 0; a < kActionCount; ++a)
                key[size_t(a)] = kDefaultKeys[a];
        }
        void reset(int action) { key[size_t(action)] = kDefaultKeys[action]; }

        // From saved values: 0 or a nonsense number means the default; keys bound twice put everything back to the defaults.
        void load(const std::array<int, kActionCount> &saved)
        {
            reset();
            for (int a = 0; a < kActionCount; ++a)
                if (saved[size_t(a)] > 0 && saved[size_t(a)] < SDL_NUM_SCANCODES)
                    key[size_t(a)] = saved[size_t(a)];
            for (int a = 0; a < kActionCount; ++a)
                for (int b = a + 1; b < kActionCount; ++b)
                    if (key[size_t(a)] == key[size_t(b)])
                    {
                        reset();
                        return;
                    }
        }

        bool held(const Uint8 *state, Action a) const { return state[key[size_t(a)]] != 0; }

        // The key that acts as this one: the default key of the action it is bound to, nothing if it is the default key of an
        // action it no longer belongs to, itself otherwise.
        SDL_Keycode canonical(int scancode, SDL_Keycode sym) const
        {
            for (int a = 0; a < kActionCount; ++a)
                if (key[size_t(a)] == scancode)
                    return kCanonicalKeys[a];
            for (int a = 0; a < kActionCount; ++a)
                if (int(kDefaultKeys[a]) == scancode)
                    return SDLK_UNKNOWN;
            return sym;
        }

        // Keys that can't be bound: Escape (it cancels the binding), and "no key".
        static bool allowed(int scancode) { return scancode > 0 && scancode < SDL_NUM_SCANCODES && scancode != SDL_SCANCODE_ESCAPE; }

        // Bind a key to an action; if another action had it, that one takes the key this one had (they swap). False if not allowed.
        bool bind(int action, int scancode)
        {
            if (!allowed(scancode))
                return false;
            for (int a = 0; a < kActionCount; ++a)
                if (a != action && key[size_t(a)] == scancode)
                    key[size_t(a)] = key[size_t(action)];
            key[size_t(action)] = scancode;
            return true;
        }

        std::string name(int action) const
        {
            const char *n = SDL_GetScancodeName(SDL_Scancode(key[size_t(action)]));
            return n && *n ? n : "None";
        }
    };

    // The one set of bindings for the whole program.
    inline KeyBinds &keys()
    {
        static KeyBinds k;
        return k;
    }

} // namespace gp
