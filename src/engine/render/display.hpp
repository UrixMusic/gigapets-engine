// display.hpp - the window: its size, scaling and mode, from the options screen's settings.
//
// The game draws into a 320 x 240 logical screen that SDL scales to the window. Scale mode "perfect" lets SDL use whole-number
// scales only (pixels are never uneven; the leftover window is a border), "stretch" fills the window at any scale (still
// nearest neighbour). The border is not black but the menus' background colour, colour 24 (index 23) of palette 0x28c, so a border
// around a menu screen blends in; clearFrame() paints it.
#pragma once
#include <SDL2/SDL.h>
#include <algorithm>

namespace gp
{

    constexpr int kLogicalW = 320, kLogicalH = 240;
    constexpr int kWideW = 426; // 16:9 at 240 lines (426.7, rounded down)

    // The logical screen's width now: 320 (4:3) or kWideW (16:9). The world is drawn across all of it; the screens made for
    // 320 x 240 (menus, the item screen, minigames, dialogue) are drawn at a 320-wide logical size, which SDL centres (useUiScreen).
    inline int &logicalW()
    {
        static int w = kLogicalW;
        return w;
    }
    constexpr SDL_Color kBorderColor{0, 115, 189, 255}; // color used in menus mostly

    enum WindowMode : int
    {
        WindowWindowed = 0,
        WindowFullscreen = 1, // fills the desktop without changing its resolution
        WindowBorderless = 2, // a window with no frame, at the chosen scale
        kWindowModes
    };

    struct DisplaySettings
    {
        int scale = 2; // 1, 2, 4 or 8: the window is 320 x 240 times this (windowed and borderless)
        bool perfect = true;
        int windowMode = WindowWindowed;
        int aspect = 0; // 0 = 4:3, 1 = 16:9
    };

    // Everything the window needs, applied now. Safe to call again and again.
    inline void applyDisplay(SDL_Window *win, SDL_Renderer *ren, const DisplaySettings &d)
    {
        if (!win || !ren)
            return;
        const int logicalW_ = d.aspect == 1 ? kWideW : kLogicalW, logicalH = kLogicalH;
        logicalW() = logicalW_;
        SDL_RenderSetLogicalSize(ren, logicalW_, logicalH);
        SDL_RenderSetIntegerScale(ren, d.perfect ? SDL_TRUE : SDL_FALSE);
        if (d.windowMode == WindowFullscreen)
        {
            SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            return;
        }
        SDL_SetWindowFullscreen(win, 0);
        SDL_SetWindowBordered(win, d.windowMode == WindowBorderless ? SDL_FALSE : SDL_TRUE);
        int w = logicalW_ * d.scale, h = logicalH * d.scale;
        SDL_Rect usable{0, 0, w, h};
        if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win), &usable) == 0)
        {
            w = std::min(w, usable.w); // a scale too big for the desktop is cut down to fit it
            h = std::min(h, usable.h);
        }
        SDL_SetWindowSize(win, w, h);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    // In 16:9 the screens made for 320 x 240 are drawn in the middle by switching the logical size to 320 x 240 for them: SDL then
    // centres that 4:3 picture in the window at the same pixel size as the wide world (the window is height-limited either way),
    // and everything drawn already stays where it is. useWholeScreen() goes back to the wide size, for the world and for fades that
    // cover the whole window. Both do nothing in 4:3.
    inline void useUiScreen(SDL_Renderer *ren)
    {
        if (logicalW() > kLogicalW)
            SDL_RenderSetLogicalSize(ren, kLogicalW, kLogicalH);
    }
    inline void useWholeScreen(SDL_Renderer *ren)
    {
        if (logicalW() > kLogicalW)
            SDL_RenderSetLogicalSize(ren, logicalW(), kLogicalH);
    }

    // The start of a frame: the whole window in the border colour (SDL clears the whole target, bars included), then the
    // logical screen in black (with a 320-wide logical size, just its middle).
    inline void clearFrame(SDL_Renderer *ren)
    {
        SDL_SetRenderDrawColor(ren, kBorderColor.r, kBorderColor.g, kBorderColor.b, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderFillRect(ren, nullptr);
    }

} // namespace gp
