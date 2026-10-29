// tests/keybinds_test.cpp - the key bindings (engine/keybinds.hpp).
#define SDL_MAIN_HANDLED
#include <cstdio>
#include "../src/engine/keybinds.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    KeyBinds k;
    check("the defaults are the arrows, Z, X and Enter", k.key[0] == SDL_SCANCODE_UP && k.key[3] == SDL_SCANCODE_RIGHT && k.key[4] == SDL_SCANCODE_Z &&
                                                          k.key[5] == SDL_SCANCODE_X && k.key[6] == SDL_SCANCODE_RETURN);
    check("a default key reads as itself", k.canonical(SDL_SCANCODE_Z, SDLK_z) == SDLK_z && k.canonical(SDL_SCANCODE_UP, SDLK_UP) == SDLK_UP);
    check("other keys pass through", k.canonical(SDL_SCANCODE_E, SDLK_e) == SDLK_e && k.canonical(SDL_SCANCODE_F4, SDLK_F4) == SDLK_F4);

    check("binding Select to C", k.bind(int(Action::Select), SDL_SCANCODE_C));
    check("C now reads as Z", k.canonical(SDL_SCANCODE_C, SDLK_c) == SDLK_z);
    check("and Z, which belongs to nothing now, does nothing", k.canonical(SDL_SCANCODE_Z, SDLK_z) == SDLK_UNKNOWN);

    check("binding Up to X swaps: Cancel gets Up's old key", k.bind(int(Action::Up), SDL_SCANCODE_X) && k.key[int(Action::Up)] == SDL_SCANCODE_X &&
                                                                  k.key[int(Action::Cancel)] == SDL_SCANCODE_UP);
    check("so X reads as Up and the Up arrow as Cancel", k.canonical(SDL_SCANCODE_X, SDLK_x) == SDLK_UP && k.canonical(SDL_SCANCODE_UP, SDLK_UP) == SDLK_x);

    check("Escape can't be bound", !k.bind(int(Action::Menu), SDL_SCANCODE_ESCAPE) && k.key[int(Action::Menu)] == SDL_SCANCODE_RETURN);
    check("no key can't either", !KeyBinds::allowed(0) && !KeyBinds::allowed(-3) && KeyBinds::allowed(SDL_SCANCODE_A));

    k.reset(int(Action::Select));
    check("one action resets to its default", k.key[int(Action::Select)] == SDL_SCANCODE_Z);

    std::array<int, kActionCount> saved{};
    saved[int(Action::Left)] = SDL_SCANCODE_A;
    saved[int(Action::Right)] = SDL_SCANCODE_D;
    KeyBinds l;
    l.load(saved);
    check("loading keeps the saved keys and defaults the zeros", l.key[int(Action::Left)] == SDL_SCANCODE_A && l.key[int(Action::Right)] == SDL_SCANCODE_D &&
                                                                    l.key[int(Action::Select)] == SDL_SCANCODE_Z);
    saved[int(Action::Up)] = SDL_SCANCODE_A; // twice
    l.load(saved);
    check("a file that binds a key twice falls back to the defaults", l.key[int(Action::Left)] == SDL_SCANCODE_LEFT && l.key[int(Action::Up)] == SDL_SCANCODE_UP);
    saved = {};
    saved[0] = 99999;
    l.load(saved);
    check("a nonsense scancode is ignored", l.key[0] == SDL_SCANCODE_UP);

    std::printf(fails ? "keybinds_test: %d failure(s)\n" : "keybinds_test: all passed\n", fails);
    return fails ? 1 : 0;
}
