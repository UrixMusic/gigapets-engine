#define SDL_MAIN_HANDLED
#include "../src/game/debug_mode.hpp"
#include <cstdio>

static int fails = 0;
static void check(const char *n, bool ok)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", n);
    fails += !ok;
}

int main()
{
    gp::DebugCode c;
    bool done = false;
    for (SDL_Keycode k : gp::DebugCode::kCode)
        done = c.press(k);
    check("the full code unlocks", done);
    done = false;
    for (int i = 0; i < 9; ++i)
        done |= c.press(gp::DebugCode::kCode[i]);
    check("a partial code does not", !done);
    c.reset();
    c.press(SDLK_UP);
    c.press(SDLK_DOWN);
    c.press(SDLK_LEFT); // wrong key: starts over
    done = false;
    for (SDL_Keycode k : gp::DebugCode::kCode)
        done = c.press(k);
    check("a wrong key starts over, and the code still works", done);
    c.reset();
    c.press(SDLK_UP);
    c.press(SDLK_UP); // a second Up is also a fresh start
    done = false;
    for (int i = 1; i < gp::DebugCode::kLength; ++i)
        done = c.press(gp::DebugCode::kCode[i]);
    check("Up, Up, then the rest counts", done);
    c.reset();
    done = false;
    for (SDL_Keycode k : gp::DebugCode::kCode)
    {
        c.press(SDLK_LSHIFT);
        done = c.press(k);
    }
    check("modifier keys are ignored", done);
    std::printf(fails ? "FAILED\n" : "ALL PASS\n");
    return fails;
}
