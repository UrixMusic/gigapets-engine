// A native mod: two engine functions replaced by name.
//   build:  build.bat   (g++ -shared, with the game's src folder on the include path)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cmath>
#include "mods/native_mod.hpp"       // GpNativeApi
#include "game/pet/thought_bubble.hpp"   // gp::ThoughtTimer

// the pointers the old versions are reached by
static bool (*origSick)(gp::ThoughtTimer &, bool, bool, bool);

// gp::sickBubbleStep: count 30 steps for each one, so a sick pet's bubble comes 30 times as often
static bool mySick(gp::ThoughtTimer &t, bool sick, bool walking, bool bubbleUp)
{
    t.accum += 29;
    return origSick(t, sick, walking, bubbleUp);
}

// gp::clearFrame: the border (the bars either side of the picture on a wide window) slowly changes colour
static void myClear(SDL_Renderer *ren)
{
    const double t = SDL_GetTicks() / 1000.0;
    SDL_SetRenderDrawColor(ren, Uint8(128 + 127 * std::sin(t)), Uint8(128 + 127 * std::sin(t + 2.1)), Uint8(128 + 127 * std::sin(t + 4.2)), 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderFillRect(ren, nullptr);
}

GP_NATIVE_MOD_ABI // says which version of the game this was built against (the game refuses a dll built for another)
extern "C" __declspec(dllexport) void gp_mod_init(const GpNativeApi *api)
{
    api->detour("_ZN2gp14sickBubbleStepERNS_12ThoughtTimerEbbb", (void *)mySick, (void **)&origSick);
    void *unused = nullptr;
    api->detour("_ZN2gp10clearFrameEP12SDL_Renderer", (void *)myClear, &unused);
    api->log("loaded: border colour and sick bubble replaced");
}
