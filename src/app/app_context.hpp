// app_context.hpp - what the front end and the gameplay share: one ROM, one window, one renderer.
#pragma once
#include <SDL2/SDL.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "rom.hpp"

namespace gp
{
    // runWalk() results
    constexpr int kRunQuit = 0;   // window closed: leave the program
    constexpr int kRunToMenu = 1; // F10: back to the front-end menu
    constexpr int kRunError = 2;

    struct AppContext
    {
        SDL_Window *win = nullptr;
        SDL_Renderer *ren = nullptr;
        Rom *rom = nullptr;
    };

    // Owns the ROM, the SDL window and the renderer. Create one in main(); throws on failure.
    class AppHost
    {
    public:
        AppContext ctx;

        AppHost(const char *romPath, const char *title)
        {
            rom_ = std::make_unique<Rom>(romPath);
            if (SDL_Init(SDL_INIT_VIDEO) != 0)
                throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
            ctx.win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_RESIZABLE);
            if (ctx.win)
                ctx.ren = SDL_CreateRenderer(ctx.win, -1, SDL_RENDERER_PRESENTVSYNC);
            if (!ctx.win || !ctx.ren)
            {
                const std::string err = SDL_GetError();
                cleanup();
                throw std::runtime_error("SDL window/renderer: " + err);
            }
            SDL_RenderSetLogicalSize(ctx.ren, 320, 240);
            ctx.rom = rom_.get();
        }
        ~AppHost() { cleanup(); }
        AppHost(const AppHost &) = delete;
        AppHost &operator=(const AppHost &) = delete;

    private:
        void cleanup()
        {
            if (ctx.ren)
                SDL_DestroyRenderer(ctx.ren);
            if (ctx.win)
                SDL_DestroyWindow(ctx.win);
            ctx.ren = nullptr;
            ctx.win = nullptr;
            SDL_Quit();
        }
        std::unique_ptr<Rom> rom_;
    };
} // namespace gp

// defined in tools/walk.cpp and tools/frontend_demo.cpp
int runWalk(gp::AppContext &ctx, int slot, int startArea, bool showCollision);
int runFrontEnd(gp::AppContext &ctx, bool launchGame, bool skipBoot, int maxFrames);