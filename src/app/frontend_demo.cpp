// tools/frontend_demo.cpp
// Standalone viewer for the front-end screens. Needs only rom.hpp, sprite.hpp, menu_draw.hpp, name_entry.hpp.
//   frontend_demo rom.u7 [--frames N]
//
// Keys (all screens):  F1 main menu   F2 name entry   F3 background browser
//                      F5 cycle sprite placement mode   F6 toggle red/blue swap   F7 species 0/1/2/3 (name screen)
//                      Esc  back to the main menu
// Main menu:  Up/Down move, Z (release) activates. Q/W, E/R, T/Y change slide offsets a, b, c.
//             New -> name entry. Load/Options print their target state.
// Name entry: arrows move, Enter presses the key. The typed name is shown in the window title (no font here).
// Browser:    Left/Right = frame-table index a, Up/Down = palette +-1 (Shift = +-16).
#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>
#include <exception>
#include <functional>
#include <tuple>
#include <array>

#include "../game/menu_draw.hpp"
#include "../game/name_entry.hpp"
#include "rom.hpp"
#include "sprite.hpp"
#include "../game/player.hpp"
#include "../game/save.hpp"
#include "../game/save_player.hpp" // whatever the savePlayer header is called
#include "../game/dialogue.hpp"
#include "app_context.hpp"
#include "../loaders/import.hpp" // ContentDB + importRom: that is where the fonts live
#include "text_draw.hpp"
#include <memory>

using namespace gp;

// ---- ROM helpers ------------------------------------------------------------------------------

// RollStarterColorFromTable: common table (mode 0) = 18 entries, rare (mode != 0) = 14 entries,
// 0x20 words apart per species. Move this into frontend.hpp once it works.
inline int rollStarterColor(const Rom &rom, int species, int mode, const std::function<int(int)> &randBelow)
{
    if (species > 2)
        return mode;
    const bool rare = mode != 0;
    const int n = rare ? 14 : 18;
    const uint32_t base = (rare ? 0xd180u : 0xd18eu) + 0x20u * uint32_t(species);
    return rom.u16(base + uint32_t(randBelow(n)));
}

// ---- draw queue -------------------------------------------------------------------------------

struct FrontGfx : Gfx
{
    struct Cmd
    {
        int layer, seq, sprite, pal, x, y;
    };
    std::vector<Cmd> q;
    std::string lastText;
    void drawTile(int s, int p, int x, int y, int layer, int) override
    {
        q.push_back({layer, int(q.size()), s, p, x, y});
    }
    struct Text
    {
        int style, pal, x, y, w, mode;
        std::string s;
    };
    std::vector<Text> texts;
    void drawTextBox(int style, int pal, int x, int y, const std::string &t, int w, int mode, int) override
    {
        lastText = t;
        texts.push_back({style, pal, x, y, w, mode, t});
    }
};

// ---- main menu (draw list from notes section 3) -----------------------------------------------

struct MenuView
{
    int a = 0, b = 0, c = 8; // final slide offsets (read from the game)
    int colour[3] = {0, 0, 0};
    int cursor = 1;
    bool pressed = false;
    bool ready = true; // false while sprites are still sliding in
};

inline void drawMainMenu(Gfx &g, const MenuView &v)
{
    constexpr int P = 0x4d5, L = 0x3000, BP = 0x4c7;
    g.drawTile(0x9f4, P, v.b + 0xb8, -1, L);
    g.drawTile(0x9f1, P, v.a, -1, L);
    g.drawTile(0x9f3, v.colour[2], v.a + 0x1b, 0x28, L);
    g.drawTile(0x9f0, v.colour[0], v.a, 0x7b, L);
    g.drawTile(0x9f2, v.colour[1], v.b + 0xcf, 0x44, L);
    g.drawTile(0x9e6, BP, 0x5f, v.c, L);
    if (!v.ready)
        return;
    struct Btn
    {
        int x, y, t[3];
    };
    static const Btn btns[3] = {
        {0x68, 0x85, {0x9e7, 0x9e8, 0x9e9}}, // New
        {0x65, 0xa0, {0x9e3, 0x9e4, 0x9e5}}, // Load
        {0x74, 0xbb, {0x9ea, 0x9eb, 0x9ec}}, // Options
    };
    for (int i = 0; i < 3; ++i)
    {
        const int st = (i == v.cursor) ? (v.pressed ? 2 : 1) : 0;
        g.drawTile(btns[i].t[st], BP, btns[i].x, btns[i].y, L);
    }
}

inline void drawIntro(Gfx &g, const MenuView &m)
{
    constexpr int L = 0x3000;
    g.drawTile(0x9f2, m.colour[1], 0xcf, 0x45, L);
    g.drawTextBox(4, 0x4c7, 0x14, 0x25, "Next you will:", 0x140, 0, L);
    g.drawTextBox(0, 0x28c, 0x14, 0x53, "* Choose a pet", 200, 0, L);
    g.drawTextBox(0, 0x28c, 0x14, 0x67, "* Name it", 200, 0, L);
    g.drawTextBox(0, 0x28c, 0x14, 0x7b, "* Pick a house", 200, 0, L);
    g.drawTextBox(2, 0x28c, 0x0a, 0xc0, "Press Select to continue", 200, 1, L);
}

inline void drawHouse(Gfx &g, int cur, const std::string (&names)[3], const bool (&full)[3])
{
    constexpr int L = 0x3000;
    static const int ys[4] = {0x7d, 0x96, 0xaf, 200};
    g.drawTextBox(4, 0x4c7, 0, 0x14, "Select your house.", 0x140, 1, L);
    if (cur != 3)
        g.drawTile(0x9c3 + cur, 0x28c, 0x108, 0xe6, L, 999);
    for (int i = 0; i < 4; ++i)
    {
        const std::string label = i == 3 ? "Back" : (full[i] ? names[i] : "Empty");
        g.drawTextBox(i == cur ? 5 : 4, 0x4c7, 8, ys[i], label, 0x140, 0, L);
    }
}
inline void drawLoad(Gfx &g, int cur, bool picked, const std::string (&names)[3], const bool (&full)[3],
                     const int (&sp)[3], const int (&col)[3])
{
    constexpr int L = 0x3000;
    static const int ys[4] = {0x80, 0x99, 0xb2, 0xcb};
    if (cur < 3 && full[cur])
    {
        if (sp[cur] == 0)
            g.drawTile(0x9f0, col[cur], 0, 0x7b, L, 999);
        if (sp[cur] == 1)
            g.drawTile(0x9f2, col[cur], 0xcf, 0x44, L, 999); // x assumed from the main menu
        if (sp[cur] == 2)
            g.drawTile(0x9f3, col[cur], 0x1b, 0x28, L, 999); // x assumed from the main menu
    }
    g.drawTile(0x9e6, 0x4c7, 0x5f, 8, L, 999);
    for (int i = 0; i < 4; ++i)
    {
        const std::string label = i == 3 ? "Back" : (full[i] ? names[i] : "Empty");
        g.drawTextBox(i == cur ? (picked ? 6 : 5) : 4, 0x4c7, 0, ys[i], label, 0x140, 1, L);
    }
}

// ---- app --------------------------------------------------------------------------------------

struct Tex
{
    SDL_Texture *t = nullptr;
    int w = 0, h = 0, ox = 0, oy = 0, ax = 0, ay = 0;
};

struct App
{
    const Rom &rom;
    SDL_Renderer *ren;
    std::map<std::pair<int, int>, Tex> sprites, bgs;
    std::map<int, bool> warned;
    int placeMode = 0; // 0: obj - anchor - origin   1: obj - origin   2: obj = canvas top-left
    bool swapRB = false;

    struct AnimTex
    {
        std::vector<Tex> frames;
        std::vector<int> dx, dy;
    };
    std::map<std::pair<int, int>, AnimTex> anims;
    std::map<std::tuple<int, int, int>, Tex> layers; // tile-map layers (starter screen)
    int transMode = 0;                               // F9: 0 palette bit15, 1 index 0, 2 both

    App(const Rom &r, SDL_Renderer *rn) : rom(r), ren(rn) {}

    void clearCaches()
    {
        for (auto *m : {&sprites, &bgs})
        {
            for (auto &kv : *m)
                if (kv.second.t)
                    SDL_DestroyTexture(kv.second.t);
            m->clear();
        }
        for (auto &kv : anims)
            for (auto &f : kv.second.frames)
                if (f.t)
                    SDL_DestroyTexture(f.t);
        anims.clear();
        for (auto &kv : layers)
            if (kv.second.t)
                SDL_DestroyTexture(kv.second.t);
        layers.clear();
    }

    Tex &sprite(int s, int p)
    {
        const auto key = std::make_pair(s, p);
        auto it = sprites.find(key);
        if (it != sprites.end())
            return it->second;
        Tex t;
        DecodedSprite d;
        if (decodeSprite(rom, s, d))
        {
            Palette pal;
            const bool has = readPalette(rom, p, pal);
            if (!has)
                std::printf("warning: palette 0x%x not found (sprite 0x%x), drawing greyscale\n", p, s);
            std::vector<uint8_t> rgba;
            toRGBA(d, has ? &pal : nullptr, swapRB, rgba);
            t.t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, d.w, d.h);
            SDL_UpdateTexture(t.t, nullptr, rgba.data(), d.w * 4);
            SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
            t.w = d.w;
            t.h = d.h;
            t.ox = d.originX;
            t.oy = d.originY;
            t.ax = d.anchorX;
            t.ay = d.anchorY;
        }
        else if (!warned[s])
        {
            warned[s] = true;
            std::printf("warning: sprite 0x%x failed to decode\n", s);
        }
        return sprites.emplace(key, t).first->second;
    }

    // Same decode as gp::importAnim (anim table 0x8ad1, 5 words/record), kept as SDL textures.
    AnimTex &anim(int id, int palId)
    {
        const auto key = std::make_pair(id, palId);
        auto it = anims.find(key);
        if (it != anims.end())
            return it->second;
        AnimTex a;
        const uint32_t rec = 0x8ad1u + uint32_t(id) * 5;
        if (rec + 5 <= rom.sizeWords())
        {
            const int start = rom.u16(rec), end = rom.u16(rec + 1);
            const uint32_t tbl = rom.u16(rec + 4);
            if (end > start && end - start <= 256)
            {
                Palette pal;
                const bool has = readPalette(rom, palId, pal);
                for (int sid = start; sid < end; ++sid)
                {
                    DecodedSprite d;
                    if (!decodeSprite(rom, sid, d))
                        continue;
                    std::vector<uint8_t> rgba;
                    toRGBA(d, has ? &pal : nullptr, swapRB, rgba);
                    Tex t;
                    t.t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, d.w, d.h);
                    SDL_UpdateTexture(t.t, nullptr, rgba.data(), d.w * 4);
                    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
                    t.w = d.w;
                    t.h = d.h;
                    t.ax = d.anchorX;
                    t.ay = d.anchorY;
                    const int k = sid - start;
                    a.frames.push_back(t);
                    a.dx.push_back(int16_t(rom.u16(tbl + k * 4)));
                    a.dy.push_back(int16_t(rom.u16(tbl + k * 4 + 1)));
                }
            }
        }
        return anims.emplace(key, std::move(a)).first->second;
    }

    void drawAnim(int id, int palId, int x, int y, Uint32 ms, int fps = 18)
    {
        AnimTex &a = anim(id, palId);
        if (a.frames.empty())
            return;
        const int f = int(uint64_t(ms) * fps / 1000) % int(a.frames.size());
        const Tex &t = a.frames[f];
        SDL_Rect r{x - t.ax + a.dx[f], y - t.ay + a.dy[f], t.w, t.h}; // same placement as your prompt arrow
        SDL_RenderCopy(ren, t.t, nullptr, &r);
    }

    // Screen background from the frame table: record a at 0x91ab + 4a = {cols, rows, attr, base};
    // tile n (1-based, row by row) at word base*0x40 + n*tileWords.
    Tex &background(int a, int palId)
    {
        const auto key = std::make_pair(a, palId);
        auto it = bgs.find(key);
        if (it != bgs.end())
            return it->second;
        Tex t;
        const uint32_t r0 = 0x91abu + uint32_t(a) * 4;
        if (r0 + 4 <= rom.sizeWords())
        {
            const int cols = rom.u16(r0), rows = rom.u16(r0 + 1), attr = rom.u16(r0 + 2);
            const uint32_t base = rom.u16(r0 + 3);
            const int tw = 8 << ((attr >> 4) & 3), th = 8 << ((attr >> 6) & 3), bpp = 2 * ((attr & 3) + 1);
            const uint32_t tileWords = uint32_t(tw * th * bpp / 16);
            if (cols > 0 && rows > 0 && cols <= 64 && rows <= 64)
            {
                const int W = cols * tw, H = rows * th;
                Palette pal;
                const bool has = readPalette(rom, palId, pal);
                if (!has)
                    std::printf("warning: palette 0x%x not found for background a=0x%x\n", palId, a);
                std::vector<uint8_t> rgba(size_t(W) * H * 4, 0);
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < cols; ++c)
                    {
                        const uint32_t addr = base * 0x40u + uint32_t(r * cols + c + 1) * tileWords;
                        if (addr + tileWords > rom.sizeWords())
                            continue;
                        const auto px = unpackPixels(rom, addr, tw * th, bpp);
                        for (int y = 0; y < th; ++y)
                            for (int x = 0; x < tw; ++x)
                            {
                                const int v = px[size_t(y) * tw + x];
                                uint8_t *o = &rgba[(size_t(r * th + y) * W + c * tw + x) * 4];
                                o[3] = 255;
                                if (has && v < int(pal.colors.size()))
                                {
                                    const uint16_t w = pal.colors[v];
                                    int rr = (w >> 10) & 31, gg = (w >> 5) & 31, bb = w & 31;
                                    if (swapRB)
                                        std::swap(rr, bb);
                                    o[0] = uint8_t(rr * 255 / 31);
                                    o[1] = uint8_t(gg * 255 / 31);
                                    o[2] = uint8_t(bb * 255 / 31);
                                }
                                else
                                    o[0] = o[1] = o[2] = uint8_t(v * 255 / ((1 << bpp) - 1));
                            }
                    }
                t.t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
                SDL_UpdateTexture(t.t, nullptr, rgba.data(), W * 4);
                t.w = W;
                t.h = H;
                std::printf("background a=0x%x: %dx%d tiles of %dx%d, %dbpp, base 0x%x\n", a, cols, rows, tw, th, bpp, unsigned(base));
            }
            else
                std::printf("background a=0x%x: implausible record {%d,%d,0x%x,0x%x}\n", a, cols, rows, attr, unsigned(base));
        }
        return bgs.emplace(key, t).first->second;
    }

    void drawBackground(int a, int pal)
    {
        Tex &t = background(a, pal);
        if (!t.t)
            return;
        SDL_Rect dst{0, 0, t.w, t.h};
        SDL_RenderCopy(ren, t.t, nullptr, &dst);
    }

    // Tile-map layer (starter screen): map of cols x rows cells at word address mapPtr, tiles from frame record `frame`.
    // Cell value n = tile number (0 = empty), tile data at base*0x40 + n*tileWords (same addressing as background()).
    // transMode 0: palette bit15 = transparent, 1: pixel index 0 = transparent, 2: both.  (F9 cycles it)
    Tex &tileLayer(uint32_t mapPtr, int cols, int rows, int frame, int palId)
    {
        const auto key = std::make_tuple(int(mapPtr), frame, palId);
        auto it = layers.find(key);
        if (it != layers.end())
            return it->second;
        Tex t;
        const uint32_t r0 = 0x91abu + uint32_t(frame) * 4;
        const int attr = rom.u16(r0 + 2);
        const uint32_t base = rom.u16(r0 + 3);
        const int tw = 8 << ((attr >> 4) & 3), th = 8 << ((attr >> 6) & 3), bpp = 2 * ((attr & 3) + 1);
        const uint32_t tileWords = uint32_t(tw * th * bpp / 16);
        Palette pal;
        const bool has = readPalette(rom, palId, pal);
        const int W = cols * tw, H = rows * th;
        std::vector<uint8_t> rgba(size_t(W) * H * 4, 0);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const uint32_t n = rom.u16(mapPtr + uint32_t(r * cols + c));
                if (!n)
                    continue;
                const uint32_t addr = base * 0x40u + n * tileWords;
                if (addr + tileWords > rom.sizeWords())
                    continue;
                const auto px = unpackPixels(rom, addr, tw * th, bpp);
                for (int y = 0; y < th; ++y)
                    for (int x = 0; x < tw; ++x)
                    {
                        const int v = px[size_t(y) * tw + x];
                        if (!has || v >= int(pal.colors.size()))
                            continue;
                        const uint16_t w = pal.colors[v];
                        const bool tr = transMode == 0 ? (w & 0x8000) != 0 : transMode == 1 ? v == 0
                                                                                            : (v == 0 || (w & 0x8000));
                        if (tr)
                            continue;
                        int rr = (w >> 10) & 31, gg = (w >> 5) & 31, bb = w & 31;
                        if (swapRB)
                            std::swap(rr, bb);
                        uint8_t *o = &rgba[(size_t(r * th + y) * W + c * tw + x) * 4];
                        o[0] = uint8_t(rr * 255 / 31);
                        o[1] = uint8_t(gg * 255 / 31);
                        o[2] = uint8_t(bb * 255 / 31);
                        o[3] = 255;
                    }
            }
        t.t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
        SDL_UpdateTexture(t.t, nullptr, rgba.data(), W * 4);
        SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
        t.w = W;
        t.h = H;
        return layers.emplace(key, t).first->second;
    }

    void drawLayer(const Tex &t, int scroll)
    {
        if (!t.t)
            return;
        SDL_Rect dst{-scroll, 0, t.w, t.h};
        SDL_RenderCopy(ren, t.t, nullptr, &dst);
    }

    // Immediate sprite draw (no queue / layer sort), same placement modes as flush().
    void blit(int s, int p, int x, int y)
    {
        Tex &t = sprite(s, p);
        if (!t.t)
            return;
        int dx = x, dy = y;
        if (placeMode == 0)
        {
            dx = x - t.ax - t.ox;
            dy = y - t.ay - t.oy;
        }
        else if (placeMode == 1)
        {
            dx = x - t.ox;
            dy = y - t.oy;
        }
        SDL_Rect dst{dx, dy, t.w, t.h};
        SDL_RenderCopy(ren, t.t, nullptr, &dst);
    }

    // One animation frame at a fixed frame index; flip mirrors around the anchor (assumption, check by eye).
    void drawFrame(const AnimTex &a, int f, int x, int y, bool flip)
    {
        const Tex &t = a.frames[f];
        SDL_Rect r{flip ? x - (t.w - t.ax) - a.dx[f] : x - t.ax + a.dx[f], y - t.ay + a.dy[f], t.w, t.h};
        SDL_RenderCopyEx(ren, t.t, nullptr, &r, 0.0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }

    void flush(FrontGfx &g)
    {
        std::stable_sort(g.q.begin(), g.q.end(), [](const FrontGfx::Cmd &a, const FrontGfx::Cmd &b)
                         { return a.layer < b.layer; });
        for (const auto &c : g.q)
        {
            Tex &t = sprite(c.sprite, c.pal);
            if (!t.t)
                continue;
            int dx = c.x, dy = c.y;
            if (placeMode == 0)
            {
                dx = c.x - t.ax - t.ox;
                dy = c.y - t.ay - t.oy;
            }
            else if (placeMode == 1)
            {
                dx = c.x - t.ox;
                dy = c.y - t.oy;
            }
            SDL_Rect dst{dx, dy, t.w, t.h};
            SDL_RenderCopy(ren, t.t, nullptr, &dst);
        }
        g.q.clear();
    }
};

// ---- starter select scene (ported from ShowStarterPetSelectScreen & friends) -----------------
struct StarterScene
{
    using RB = std::function<int(int)>;
    enum Act
    {
        A_NONE,
        A_IDLE,
        A_POS,
        A_SETTLED,
        A_SPAWN,
        A_WALK,
        A_SETTLE,
        A_REP,
        A_VOC,
        A_CELEB,
        A_SAD,
        A_SADIDLE
    };
    struct PetAnim
    {
        int id = -1;
        double t = 0;
        int fps = 14;
        bool loop = false, flip = false, rev = false; // rev: play the frames backwards
    };
    struct P
    {
        int x = 0, y = 0, slot = 0, slotX = 0, facing = 3, species = 0, colour = 0, stat[4] = {0, 0, 0, 0};
        bool settled = false, react = false, legs = false, sad = false;
        Act act = A_NONE;
        int sub = 0, counter = 0, tx = 0, reps = 0;
        double wait = 0;
        std::vector<Act> q;
        PetAnim a;
    };
    struct Decor
    {
        int spr, pal, x, y;
    };

    // anim id tables per species (from the PlayXxxAnim functions)
    static constexpr int kIdle[3] = {0x3d, 0xa5, 0x10c};
    static constexpr int kWalk[3] = {0x4e, 0xb5, 0x114};
    static constexpr int kDir[3] = {0x4c, 0xb3, 0x11c}; // turn to camera (PlayDirectionalIdleAnim)
    static constexpr int kSettled[3] = {0x45, 0xad, 0x115};
    static constexpr int kPos[3] = {0x44, 0xac, 0x113};
    static constexpr int kSpawn[3] = {0x47, 0xae, 0x116};
    static constexpr int kRep[3] = {0x3f, 0xa7, 0x10f};
    static constexpr int kPre[3] = {0x4b, -1, -1};
    static constexpr int kVoc[3] = {0x49, 0xaf, 0x117};
    // confirm reactions. Starter pets have actor word +3 = 1, which picks the second table in each function.
    static constexpr int kHappy[3] = {0x3c, 0xa4, 0x10d};   // PlayPositiveReactionAnimVariant
    static constexpr int kSad[3] = {0x40, 0xa9, 0x110};     // PlaySadReactionAnim
    static constexpr int kSadIdle[3] = {0x41, 0xa8, 0x111}; // PlayIdleAnim_FlagA, second table (action 0x1d, TickActor_NegativeReaction)
    static constexpr int kCelPre[3] = {-1, -1, 0x118};      // SelectAndPlayReactionAnim: intro/outro, third species only

    std::vector<P> pets;
    std::vector<Decor> decor;
    int cursor = 0, confirmed = -1, scroll = 0x50, camT = 0xa0, arrowX = 0, arrowY = 0x73;
    bool changed = false, finished = false;
    int bgCols = 0x48, maxScroll = 0x7c0;
    uint32_t mapA = 0, mapB = 0;
    int frameA = 0, frameB = 0, palA = 0, palB = 0;

    // ---- setup -----------------------------------------------------------------------------
    void start(const Rom &rom, const RB &rb)
    {
        // background table 0xc43d, stride 9, entry 3
        const uint32_t e = 0xc43du + 3 * 9;
        bgCols = rom.u16(e);
        mapA = rom.u16(e + 2);
        frameA = rom.u16(e + 3);
        palA = rom.u16(e + 4);
        mapB = rom.u16(e + 5);
        frameB = rom.u16(e + 6);
        palB = rom.u16(e + 7);
        maxScroll = bgCols * 0x20 - 0x140;
        decor.clear();
        for (uint32_t k = 0; k < 14; ++k) // 0xf912: {itemId, x, y}; item entry = 0xa5df + 10*id, +5 sprite, +7 palette
        {
            const uint32_t rec = 0xf912u + k * 3, ent = 0xa5dfu + 10u * rom.u16(rec);
            decor.push_back({int(rom.u16(ent + 5)), int(rom.u16(ent + 7)), int(int16_t(rom.u16(rec + 1))), int(rom.u16(rec + 2))});
        }

        // species: each group of 3 is a permutation of {0,1,2}
        int sp[12];
        for (int i = 0; i < 12; ++i)
        {
            if (i % 3 == 0)
                sp[i] = rb(3);
            else if (i % 3 == 1)
            {
                int v = rb(3);
                if (v == sp[i - 1])
                {
                    if (rb(2) == 0)
                        v = v + 1 > 2 ? 0 : v + 1;
                    else
                        v = v - 1 < 0 ? 2 : v - 1;
                }
                sp[i] = v;
            }
            else
                sp[i] = 3 - sp[i - 1] - sp[i - 2];
        }
        pets.assign(12, P());
        int rareDone[3] = {0, 0, 0}, commonDone[3] = {0, 0, 0};
        bool special = false;
        for (int i = 0; i < 12; ++i)
        {
            P &p = pets[i];
            const int s = sp[i];
            bool rare;
            if (rareDone[s] < 2)
                rare = commonDone[s] < 2 ? rb(2) != 0 : true;
            else
                rare = false;
            ++(rare ? rareDone[s] : commonDone[s]);
            int colour = -1;
            if (s == 2 && rare && !special && (rareDone[2] == 2 || rb(2) == 0))
            {
                colour = 0x341;
                special = true;
            }
            else
            {
                for (int tries = 0; tries < 499; ++tries) // RollUniqueStarterColor
                {
                    colour = rollStarterColor(rom, s, rare ? 1 : 0, rb);
                    bool dup = false;
                    for (int j = 0; j < i; ++j)
                        dup = dup || (pets[j].species == s && pets[j].colour == colour);
                    if (!dup)
                        break;
                }
            }
            if (s == 2 && !special && colour == 0x341)
                special = true;
            p.species = s;
            p.colour = colour;
            for (int j = 0; j < 4; ++j)
                p.stat[j] = 3 + rb(13); // RandRange(3, 0x10)
            p.slot = i;
            p.slotX = (i / 2) * 0xa0 + 0xa0;
            p.x = p.slotX + rb(50) - 25; // RandRange(-0x19, 0x19)
            p.y = (i & 1) ? 0xa5 : 0x73;
            introRoll(p, rb);
        }
        cursor = 0;
        confirmed = -1;
        changed = false;
        finished = false;
        scroll = 0x50;
        camT = 0xa0;
        arrowX = pets[0].x;
        arrowY = 0x73;
    }

    // ---- actions ---------------------------------------------------------------------------
    static void begin(P &p, Act a)
    {
        p.act = a;
        p.sub = 0;
    }
    static void play(P &p, const int *tbl, int fps, bool loop = false, bool flip = false, bool rev = false)
    {
        if (tbl[p.species] < 0)
            return;
        p.a = PetAnim{tbl[p.species], 0.0, fps, loop, flip, rev};
    }
    static int walkTarget(const P &p, bool fin, const RB &rb)
    {
        const int r = fin ? rb(0x1e) : 0;
        return p.x < p.slotX ? p.slotX + 0x1e - r : p.slotX + r - 0x1e;
    }
    void introRoll(P &p, const RB &rb)
    {
        const int r = 1 + rb(20); // RandRange(1, 0x15)
        if (r < 3)                // spawn effect, settle, idle
        {
            p.facing = 0;
            p.q = {A_SETTLE, A_IDLE};
            begin(p, A_SPAWN);
        }
        else if (r < 7 || (r >= 11 && r < 15)) // walk in, settle
        {
            p.facing = p.x <= p.slotX ? 1 : 0;
            p.legs = true;
            p.counter = 1 + rb(3);
            p.tx = p.facing == 1 ? p.x + 0x1e : p.x - 0x1e;
            p.q = {A_SETTLE};
            begin(p, A_WALK);
        }
        else if (r < 11) // excited anim 2..4 times (vanilla uses action 0x1c then 8; 8 not ported)
        {
            p.facing = 3;
            for (int n = 1 + rb(3); n > 0; --n)
                p.q.push_back(A_SETTLED);
            begin(p, A_SETTLED);
        }
        else if (r < 20)
        {
            p.facing = 3;
            begin(p, A_IDLE);
        }
        else
        {
            p.facing = 3;
            p.react = true;
            begin(p, A_POS);
        }
    }

    bool busy(App &app, P &p)
    {
        if (p.a.id < 0)
            return false;
        App::AnimTex &A = app.anim(p.a.id, p.colour);
        if (A.frames.empty())
            return false;
        return p.a.loop || p.a.t * p.a.fps < double(A.frames.size());
    }
    bool once(App &app, P &p, const int *tbl, int fps)
    {
        if (p.sub == 0)
        {
            play(p, tbl, fps);
            p.sub = 1;
            return false;
        }
        return !busy(app, p);
    }

    bool tickAct(App &app, P &p, const RB &rb)
    {
        const bool s0 = p.species == 0;
        switch (p.act)
        {
        case A_IDLE:
            return once(app, p, kIdle, 14);
        case A_POS:
            return once(app, p, kPos, s0 ? 14 : 7);
        case A_SETTLED:
            return once(app, p, kSettled, s0 ? 7 : 5);
        case A_SPAWN:
            return once(app, p, kSpawn, s0 ? 5 : 7);
        case A_WALK:
            if (p.sub == 0)
            {
                p.facing = (p.tx != p.x && p.x <= p.tx) ? 1 : 0;
                play(p, kWalk, 14, true, p.facing == 0);
                p.sub = 1;
                return false;
            }
            if (p.facing == 1)
            {
                p.x += 2; // 3 if g_miniPetPromptState != 0
                if (p.x < p.tx)
                    return false;
            }
            else
            {
                p.x -= 2;
                if (p.x > p.tx)
                    return false;
            }
            p.x = p.tx;
            return true;
        case A_SETTLE:
            if (p.facing == 3)
                return true;
            if (p.sub == 0)
            {
                play(p, kDir, 14, false, p.facing == 1);
                p.sub = 1;
                return false;
            }
            if (!busy(app, p))
            {
                p.facing = 3;
                return true;
            }
            return false;
        case A_REP:
        {
            const bool pre = kPre[p.species] >= 0;
            if (p.sub == 0)
            {
                if (pre)
                {
                    play(p, kPre, 14);
                    p.sub = 1;
                }
                else
                {
                    play(p, kRep, 14);
                    p.sub = 2;
                }
            }
            else if (p.sub == 1)
            {
                if (!busy(app, p))
                {
                    play(p, kRep, 14);
                    p.sub = 2;
                }
            }
            else if (p.sub == 2)
            {
                if (!busy(app, p))
                {
                    if (--p.reps > 0)
                        play(p, kRep, 14);
                    else if (pre)
                    {
                        play(p, kPre, 14);
                        p.sub = 3;
                    }
                    else
                        return true;
                }
            }
            else
                return !busy(app, p);
            return false;
        }
        case A_VOC: // gap between barks is a guess (vanilla: RandRange(2,6) / something)
            if (p.sub == 0)
            {
                play(p, kVoc, 14);
                p.sub = 1;
            }
            else if (p.sub == 1)
            {
                if (!busy(app, p))
                {
                    if (--p.reps <= 0)
                        return true;
                    p.wait = 0.5 + rb(10) * 0.1;
                    p.sub = 2;
                }
            }
            else
            {
                p.wait -= 1.0 / 30;
                if (p.wait <= 0)
                    p.sub = 0;
            }
            return false;
        case A_CELEB: // PlayPositiveReactionAnim(actor, 3, 2): [intro], happy anim x3, [outro]
        {
            const bool pre = kCelPre[p.species] >= 0;
            if (p.sub == 0)
            {
                p.reps = 3;
                if (pre)
                {
                    play(p, kCelPre, 14);
                    p.sub = 1;
                }
                else
                {
                    play(p, kHappy, 14);
                    p.sub = 2;
                }
            }
            else if (p.sub == 1) // intro playing
            {
                if (!busy(app, p))
                {
                    play(p, kHappy, 14);
                    p.sub = 2;
                }
            }
            else if (p.sub == 2) // happy anim playing
            {
                if (!busy(app, p))
                {
                    if (--p.reps > 0)
                        play(p, kHappy, 14);
                    else if (pre) // vanilla plays the outro with flag 4 (maybe reversed, unchecked)
                    {
                        play(p, kCelPre, 14);
                        p.sub = 3;
                    }
                    else
                        return true;
                }
            }
            else
                return !busy(app, p);
            return false;
        }
        case A_SADIDLE: // action 0x1d, TickActor_NegativeReaction: the sad idle, picked again and again
            return once(app, p, kSadIdle, 14);
        case A_SAD: // TickNPCReactionAnimationSequence: sad anim, pause (float 0x3f00 = 0.5, unchecked), sad anim again
                    // The second PlaySadReactionAnim is followed by "actor+0xb = 4" (the same flag-4 the celebration
                    // outro uses), which is the reverse play: the pet un-does the sad pose on the way out.
            if (p.sub == 0)
            {
                play(p, kSad, 14);
                p.sub = 1;
            }
            else if (p.sub == 1)
            {
                if (!busy(app, p))
                {
                    p.wait = 0.5;
                    p.sub = 2;
                }
            }
            else if (p.sub == 2)
            {
                p.wait -= 1.0 / 30;
                if (p.wait <= 0)
                {
                    play(p, kSad, 14, false, false, true); // reversed
                    p.sub = 3;
                }
            }
            else
                return !busy(app, p);
            return false;
        default:
            return true;
        }
    }

    void pickNext(P &p, const RB &rb)
    {
        if (!p.q.empty())
        {
            begin(p, p.q.front());
            p.q.erase(p.q.begin());
            return;
        }
        p.settled = true;
        if (confirmed >= 0)
        {
            if (&p == &pets[confirmed])
            {
                p.act = A_NONE; // celebration is over, the scene is about to finish
                return;
            }
            if (p.sad)
                begin(p, A_SADIDLE); // action 0x1d: the sad idle, over and over
            else
            {
                p.sad = true; // face the camera (action 9), then action 0xf: sad sequence
                p.q = {A_SAD};
                begin(p, A_SETTLE);
            }
            return;
        }
        const int r = 1 + rb(100); // RandRange(1, 0x65)
        if (r <= 7)
        {
            p.reps = 2 + rb(3);
            begin(p, A_VOC);
        }
        else if (r <= 20)
        {
            p.reps = 2 + rb(3);
            begin(p, A_REP);
        }
        else if (r <= 30)
        {
            p.legs = true;
            p.counter = 1 + rb(3);
            p.tx = walkTarget(p, p.counter == 1, rb);
            p.q = {A_SETTLE};
            begin(p, A_WALK);
        }
        else
            begin(p, p.react ? A_POS : A_IDLE);
    }

    void step(App &app, P &p, const RB &rb)
    {
        if (p.act == A_NONE)
        {
            pickNext(p, rb);
            if (p.act == A_NONE)
                return;
        }
        if (!tickAct(app, p, rb))
            return;
        const Act d = p.act;
        if (d == A_CELEB)
        {
            finished = true;
            p.act = A_NONE;
            return;
        }
        if (d == A_WALK && p.legs)
        {
            if (--p.counter >= 1)
            {
                p.tx = walkTarget(p, p.counter == 1, rb);
                begin(p, A_WALK);
                return;
            }
            p.legs = false;
        }
        p.act = A_NONE;
        pickNext(p, rb);
    }

    // ---- per-frame helpers ---------------------------------------------------------------
    static int frameIndex(const App::AnimTex &A, const P &p)
    {
        const int n = int(A.frames.size()), k = int(p.a.t * p.a.fps);
        const int f = p.a.loop ? k % n : std::min(k, n - 1);
        return p.a.rev ? n - 1 - f : f;
    }
    bool visible(App &app, P &p)
    {
        if (p.a.id < 0)
            return true;
        App::AnimTex &A = app.anim(p.a.id, p.colour);
        if (A.frames.empty())
            return true;
        const Tex &t = A.frames[frameIndex(A, p)];
        const int left = p.x - scroll - t.ax;
        return left + t.w > 0 && left <= 0x13f;
    }

    void confirm()
    {
        if (changed || confirmed >= 0)
            return;
        confirmed = cursor; // vanilla also plays sound 0x59 here
        // Like the original: running actions finish first (no further walk legs), then everybody
        // faces the camera. The chosen pet celebrates, the others turn sad (see pickNext).
        for (P &o : pets)
        {
            o.q.clear();
            o.legs = false;
        }
        pets[cursor].q = {A_SETTLE, A_CELEB};
    }

    void tick(App &app, const RB &rb) // one 30 Hz game tick
    {
        for (P &p : pets)
        {
            if (!visible(app, p))
                continue; // off-screen pets are frozen, like the original
            step(app, p, rb);
            p.a.t += 1.0 / 30;
        }
        const int axisY = (cursor & 1) ? 0xa5 : 0x73, slotX = (cursor / 2) * 0xa0 + 0xa0;
        if (!changed)
        {
            arrowX = pets[cursor].x;
            arrowY = axisY;
            return;
        }
        auto mv = [](int &v, int tgt)
        {
            if (v < tgt)
                v = std::min(v + 4, tgt);
            else if (v > tgt)
                v = std::max(v - 4, tgt);
        };
        mv(arrowX, pets[cursor].x);
        mv(arrowY, axisY);
        mv(camT, slotX);
        const int vx = camT - scroll;
        if (vx > 0xf0)
            scroll = std::min(scroll + vx - 0xf0, maxScroll);
        else if (vx < 0x50)
            scroll = std::max(scroll - (0x50 - vx), 0);
        if (camT < scroll + 0x40)
            scroll = camT - 0x40;
        else if (scroll + 0x100 < camT)
            scroll = camT - 0x100;
        scroll = std::clamp(scroll, 0, maxScroll);
        if (camT == slotX && arrowX == pets[cursor].x && arrowY == axisY)
            changed = false;
    }

    void drawPet(App &app, P &p)
    {
        if (p.a.id < 0)
            return;
        App::AnimTex &A = app.anim(p.a.id, p.colour);
        if (A.frames.empty())
            return;
        const int f = frameIndex(A, p), vx = p.x - scroll, h = A.frames[f].h;
        int idx = (h - 16 >= 0) ? (h - 16) >> 3 : (h - 9) >> 3; // DrawShadow
        idx = std::clamp(idx, 0, 11);
        const bool s2 = p.species == 2;
        app.blit((s2 ? 0x9a0 : 0x994) + idx, 0x28c, vx - (idx * 8 + 8) / 2, p.y - (s2 ? 0x10 : 6));
        app.drawFrame(A, f, vx, p.y, p.a.flip);
    }

    void draw(App &app, FrontGfx &gfx, Uint32 now)
    {
        app.drawLayer(app.tileLayer(mapA, bgCols, 8, frameA, palA), scroll);
        app.drawLayer(app.tileLayer(mapB, bgCols, 8, frameB, palB), scroll);
        auto band = [&](int lo, int hi)
        {
            for (const Decor &d : decor)
                if (d.y >= lo && d.y <= hi)
                    app.blit(d.spr, d.pal, d.x - scroll, d.y);
        };
        int first = -1, last = -1;
        auto row = [&](int y)
        {
            for (int i = 0; i < int(pets.size()); ++i)
            {
                P &p = pets[i];
                if (p.y != y || !visible(app, p))
                    continue;
                if (first < 0)
                    first = i;
                last = i;
                drawPet(app, p);
            }
        };
        band(0, 0x73);
        row(0x73);
        band(0x74, 0xa5);
        row(0xa5);
        band(0xa6, 0xf0);

        if (confirmed < 0)
        {
            int h = 0;
            const P &c = pets[cursor];
            if (c.a.id >= 0)
            {
                App::AnimTex &A = app.anim(c.a.id, c.colour);
                if (!A.frames.empty())
                    h = A.frames[frameIndex(A, c)].h;
            }
            app.drawAnim(0x11, 0x28c, arrowX - scroll, arrowY - h, now);
            if (first > 0)
            {
                gfx.drawTextBox(2, 0x28c, 0x14, 0x14, "More Pets", 100, 0, 0x3000);
                app.drawAnim(0x14, 0x28c, 0x14, 0x23, now);
            }
            if (last >= 0 && last < 11)
            {
                gfx.drawTextBox(2, 0x28c, 200, 0x14, "More Pets", 100, 2, 0x3000);
                app.drawAnim(0x15, 0x28c, 0x12c, 0x23, now);
            }
        }
    }
};

enum class Screen
{
    Boot,
    Menu,
    Intro,
    Starter,
    House,
    Load,
    Name,
    Browse
};

// Returns the save slot (0..2) to play, or -1 if the window was closed.
// launchGame = false keeps the old demo behaviour (after New/Load it just returns to the menu).
// skipBoot   = true starts at the main menu (used when coming back from gameplay).
int runFrontEnd(gp::AppContext &ctx, bool launchGame, bool skipBoot, int maxFrames)
{
    int chosenSlot = -1;
    try
    {
        const Rom &rom = *ctx.rom; // owned by the AppHost
        SDL_Window *win = ctx.win;
        SDL_Renderer *ren = ctx.ren;
        SDL_RenderSetLogicalSize(ren, 320, 240);

        // Fonts come from the ContentDB. Built once (static) and reused when we come back from gameplay.
        // Species 0 is arbitrary: nothing the front end needs depends on it.
        static std::unique_ptr<gp::ContentDB> uiDb;
        if (!uiDb)
        {
            std::printf("front end: loading fonts (importRom)...\n");
            uiDb = std::make_unique<gp::ContentDB>();
            gp::importRom(rom, *uiDb, 0);
            // Every (font style, palette) pair the front end draws with, imported up front so the db never
            // changes once a TextDrawer holds a pointer into it. drawTextBox's first two arguments are
            // exactly (fontIndex, palette): the palette is baked into the glyphs by importFont.
            static const int kFrontFonts[][2] = {{0, 0x28c}, {2, 0x28c}, {3, 0x104}, {4, 0x4c7}, {5, 0x4c7}, {6, 0x4c7}};
            for (const auto &f : kFrontFonts)
                if (!uiDb->font(gp::fontId(f[0], f[1])))
                    gp::importFont(*uiDb, rom, f[0], f[1]);
        }
        std::map<std::pair<int, int>, std::unique_ptr<TextDrawer>> fonts; // one drawer per (style, palette)
        auto fontFor = [&](int style, int pal) -> TextDrawer &
        {
            auto &slot = fonts[{style, pal}];
            if (!slot)
            {
                const gp::Id fid = gp::fontId(style, pal);
                if (!uiDb->font(fid)) // not in the pre-import list above
                {
                    std::printf("front end: importing font style %d palette 0x%x on demand\n", style, pal);
                    gp::importFont(*uiDb, rom, style, pal);
                }
                slot = std::make_unique<TextDrawer>(ren, uiDb->font(fid));
            }
            return *slot;
        };
        bool showOutlines = false; // F8: the old white debug boxes

        App app(rom, ren);
        FrontGfx gfx;
        std::mt19937 rng(std::random_device{}());
        auto randBelow = [&](int n)
        { return int(rng() % unsigned(n)); };

        Screen screen = Screen::Boot; // moved up

        std::string slotName[3];
        int slotSpecies[3] = {-1, -1, -1}, slotColour[3] = {0, 0, 0};
        bool slotFull[3] = {false, false, false};

        MenuView menu;
        auto rollColours = [&]
        {
            for (int s = 0; s < 3; ++s)
                menu.colour[s] = rollStarterColor(rom, s, randBelow(3), randBelow);
            std::printf("pet colours: 0x%x 0x%x 0x%x\n", menu.colour[0], menu.colour[1], menu.colour[2]);
        };
        rollColours();
        auto refreshSlots = [&]
        {
            for (int i = 0; i < 3; ++i)
            {
                const SlotInfo si = peekSlot(i);
                slotFull[i] = si.full;
                slotName[i] = si.name;
                slotSpecies[i] = si.full ? si.species : -1;
                const int sp = std::clamp(si.species, 0, 2);
                slotColour[i] = si.appearance >= 0 ? si.appearance : menu.colour[sp]; // old saves: random menu colour
            }
        };
        refreshSlots();

        struct Pet
        {
            int species, colour, stat[4];
            bool sick; // candidate +0x1d = DAT_0021f9 = GetDefaultStatValue(4): CreateNewPet rolls an illness if set
        };
        StarterScene starter;

        Pet chosen{};
        gp::Dialogue dlg;
        int houseCur = 0;
        int loadCur = 0;
        bool loadPicked = false;
        Uint32 loadPickedAt = 0;
        auto loadStart = [&]
        {
            loadCur = 3;
            for (int i = 2; i >= 0; --i)
                if (slotFull[i])
                    loadCur = i;
            loadPicked = false;
        };
        auto firstEmptySlot = [&]
        {
            for (int i = 0; i < 3; ++i)
                if (!slotFull[i])
                    return i;
            return 3;
        };
        NameEntry name;
        int species = 0;
        Uint32 pressedUntil = 0;
        int browseA = 2, browsePal = 0x5c5;
        constexpr Uint32 kLogoMs = 3500; // 3.5 s, decoded from the float compare
        Uint32 bootT0 = SDL_GetTicks(), lastMs = bootT0;
        double tickAcc = 0;
        int bootPhase = 0; // 0 logo hold, 1 fade out, 2 slide-in
        int outTick = 0;
        auto stepSlide = [&] // one 30 Hz tick, same order as the game
        {
            if (menu.a < 0)
                menu.a += 2;
            if (menu.b > 0 && menu.a > -0x87)
                menu.b -= 2;
            if (menu.c < 8 && menu.b < 10)
                menu.c += 2;
            else if (menu.c == 8)
                menu.ready = true;
        };
        std::string lastTitle;

        constexpr int kFade = 16; // 30 Hz ticks, the game's fade length
        int fadeTick = 0;         // fade-in progress; starts at 0 so the first screen fades in from black
        int fadeSkip = 2;         // frames ignored after a screen change: the first draw uploads textures and hitches,
                                  // which used to eat the whole fade-in in one go
        auto startFade = [&]
        { fadeTick = 0; fadeSkip = 2; };

        // Screen changes fade to black, swap, and fade back in (the swap happens in the tick section below).
        Screen pendingScreen = Screen::Menu;
        bool hasPending = false, pendingSlide = false, leaving = false;
        int fadeOutTick = 0;
        auto resetSlide = [&] // same start values as the boot slide-in
        {
            menu.a = -0xac;
            menu.b = 0x88;
            menu.c = -0x8c;
            menu.ready = false;
            menu.pressed = false;
        };
        auto setScreen = [&](Screen s)
        {
            if (s == screen || hasPending || leaving)
                return;
            pendingScreen = s;
            pendingSlide = false;
            hasPending = true;
            fadeOutTick = 0;
        };
        auto setScreenNoFade = [&](Screen s) // plain swap, no fade (Load screen)
        { if (!hasPending && !leaving) screen = s; };
        auto goMenu = [&] // back to the main menu; coming back from the pet flow replays the slide-in
        {
            if (screen == Screen::Menu || hasPending || leaving)
                return;
            const bool slide = screen == Screen::Starter || screen == Screen::Name || screen == Screen::House;
            setScreen(Screen::Menu);
            pendingSlide = slide;
        };
        auto startGame = [&](int slot) // frontend -> walk: fade out first, the loop ends when it is black
        {
            if (hasPending || leaving)
                return;
            chosenSlot = slot;
            leaving = true;
            fadeOutTick = 0;
        };

        auto create = [&](int slot)
        {
            createNewSave(slot, name.name, chosen.species, chosen.colour);
            { // CreateNewPet's GetDefaultStatValue(0..4) = the four numbers the starter screen showed + the illness flag
                gp::SaveData sd;
                const std::string path = gp::slotPath(slot);
                if (gp::loadFromFile(path, sd))
                {
                    for (int j = 0; j < 4; ++j)
                        sd.setInt("startStat" + std::to_string(j), chosen.stat[j]);
                    sd.setInt("startStat4", chosen.sick ? 1 : 0); // walk rolls the actual illness (RollRandomIllnessId)
                    gp::saveToFile(path, sd);
                }
                else
                    std::printf("warning: could not reopen %s to store the starter stats\n", path.c_str());
            }
            std::printf("created slot %d: species %d colour 0x%x name \"%s\"%s\n",
                        slot, chosen.species, chosen.colour, name.name.c_str(), chosen.sick ? " (starts sick)" : "");
            refreshSlots();
            if (launchGame)
                startGame(slot); // new pet created: fade out, then straight into gameplay
            else
                goMenu(); // the demo stops here
        };

        // Yes/No window (BoxKind::Confirm). Layout numbers are the same ones walk.cpp uses for its dialogue boxes.
        auto drawConfirm = [&](const gp::DialogueBox &b)
        {
            struct Layout
            {
                int frame, x, y, textX, textY, textW, btnX, btnY;
            };
            static const Layout kLayouts[3] = {
                {0x48, 0x30, 0x30, 0x10, 0x12, 0xbd, 0x74 + 0x2b, 0x57},
                {0x49, 0x30, 0x14, 0x10, 0x12, 0xbd, 0x74 + 0x2b, 0x8c},
                {0x4a, 0x16, 0x14, 0x0c, 0x10, 0xf4, 0xb0 + 0x2b, 0x8c},
            };
            constexpr int L = 0x3000, P = 0x28c;
            const Layout &l = kLayouts[std::clamp(b.layout, 0, 2)];
            gfx.drawTile(l.frame, P, l.x, l.y, L, 999);
            const Tex &fr = app.sprite(l.frame, P); // texts are drawn after all tiles, so remove the ones under the frame
            gfx.texts.erase(std::remove_if(gfx.texts.begin(), gfx.texts.end(),
                                           [&](const FrontGfx::Text &t)
                                           { return t.y + 10 > l.y && t.y < l.y + fr.h; }),
                            gfx.texts.end());
            gfx.drawTextBox(0, P, l.x + l.textX, l.y + l.textY, b.body, l.textW, 1, L);
            gfx.drawTile(b.yesSelected ? 0x4f : 0x4e, P, l.x + l.btnX - 0x2b, l.y + l.btnY, L, 999); // Yes
            gfx.drawTile(b.yesSelected ? 0x4b : 0x4c, P, l.x + l.btnX, l.y + l.btnY, L, 999);        // No
        };

        if (skipBoot)
            screen = Screen::Menu; // menu fields default to the final slide position
        bootT0 = lastMs = SDL_GetTicks();
        bool run = true;
        for (int frame = 0; run && (maxFrames < 0 || frame < maxFrames); ++frame)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
            {
                if (e.type == SDL_QUIT)
                {
                    run = false;
                    break;
                }
                if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP)
                    continue;
                const SDL_Keycode k = e.key.keysym.sym;
                const bool down = e.type == SDL_KEYDOWN, rep = e.key.repeat != 0;
                const bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;

                if (down && !rep)
                {
                    if (k == SDLK_F1)
                        screen = Screen::Menu;
                    else if (k == SDLK_F2)
                        screen = Screen::Name;
                    else if (k == SDLK_F3)
                        screen = Screen::Browse;
                    else if (k == SDLK_x)
                    {
                        if (screen == Screen::Boot)
                            screen = Screen::Menu; // debug skip, as before
                        else if (screen == Screen::Name || screen == Screen::Browse)
                            goMenu();
                        // the other screens handle Escape themselves below
                    }
                    else if (k == SDLK_F5)
                    {
                        app.placeMode = (app.placeMode + 1) % 3;
                        std::printf("placement mode %d\n", app.placeMode);
                    }
                    else if (k == SDLK_F6)
                    {
                        app.swapRB = !app.swapRB;
                        app.clearCaches();
                        std::printf("swapRB %d\n", int(app.swapRB));
                    }
                    else if (k == SDLK_F7)
                    {
                        species = (species + 1) % 4;
                        std::printf("species %d\n", species);
                    }
                    else if (k == SDLK_F8)
                        showOutlines = !showOutlines;
                    else if (k == SDLK_F9)
                    {
                        app.transMode = (app.transMode + 1) % 3;
                        app.clearCaches();
                        std::printf("layer transparency mode %d\n", app.transMode);
                    }
                }
                if (screen == Screen::Boot && down && !rep && k != SDLK_F1 && k != SDLK_F2 && k != SDLK_F3)
                {
                    if (bootPhase == 0)
                        bootT0 = SDL_GetTicks() - kLogoMs; // skip the hold, fade out starts
                    else if (bootPhase == 2)
                    {
                        menu.a = menu.b = 0;
                        menu.c = 8;
                        menu.ready = true; // first press finishes the slide
                    }
                    continue; // phase 1: input ignored, like the game during fades
                }
                if (down && screen != Screen::Boot && (hasPending || leaving || fadeTick < kFade))
                    continue; // input is ignored during fades, like the game
                if (screen == Screen::Menu)
                {
                    if (!menu.ready)
                    {
                        if (down && !rep) // first press finishes the slide
                        {
                            menu.a = menu.b = 0;
                            menu.c = 8;
                            menu.ready = true;
                        }
                        continue;
                    }
                    if (down)
                    {
                        if (k == SDLK_UP && menu.cursor > 0)
                            --menu.cursor;
                        if (k == SDLK_DOWN && menu.cursor < 2)
                            ++menu.cursor;
                        if (k == SDLK_z && !rep)
                            menu.pressed = true;
                        const int d = shift ? 8 : 1;
                        bool changed = true;
                        if (k == SDLK_q)
                            menu.a -= d;
                        else if (k == SDLK_w)
                            menu.a += d;
                        else if (k == SDLK_e)
                            menu.b -= d;
                        else if (k == SDLK_r)
                            menu.b += d;
                        else if (k == SDLK_t)
                            menu.c -= d;
                        else if (k == SDLK_y)
                            menu.c += d;
                        else
                            changed = false;
                        if (changed)
                            std::printf("slide a=%d b=%d c=0x%x\n", menu.a, menu.b, menu.c);
                    }
                    else if (k == SDLK_z && menu.pressed)
                    {
                        menu.pressed = false;
                        static const char *names[3] = {"New -> state 6 (intro), demo jumps to name entry", "Load -> state 8", "Options -> state 5"};
                        std::printf("%s\n", names[menu.cursor]);
                        if (menu.cursor == 0)
                        {
                            name = NameEntry();
                            setScreen(Screen::Intro);
                        }
                        else if (menu.cursor == 1)
                        {
                            loadStart();
                            setScreenNoFade(Screen::Load);
                        }
                    }
                }
                else if (screen == Screen::Intro && down && !rep && fadeTick >= 16)
                {
                    if (k == SDLK_x)
                        goMenu(); // event 5 -> state 4
                    else
                    {
                        starter.start(rom, randBelow);
                        setScreen(Screen::Starter);
                    }
                }
                else if (screen == Screen::Load && down && !rep && !loadPicked)
                {
                    if (k == SDLK_UP)
                    {
                        for (int i = loadCur - 1; i >= 0; --i)
                            if (slotFull[i])
                            {
                                loadCur = i;
                                break;
                            }
                    }
                    else if (k == SDLK_DOWN)
                    {
                        for (int i = loadCur + 1; i < 3; ++i)
                            if (slotFull[i])
                            {
                                loadCur = i;
                                goto moved;
                            }
                        loadCur = 3;
                    moved:;
                    }
                    else if (k == SDLK_x)
                        setScreenNoFade(Screen::Menu); // event 5 -> state 4
                    else if (k == SDLK_z)
                    {
                        if (loadCur == 3)
                            setScreenNoFade(Screen::Menu);
                        else if (slotFull[loadCur])
                        {
                            loadPicked = true;
                            loadPickedAt = SDL_GetTicks();
                        }
                    }
                }
                else if (screen == Screen::House && down && !rep && fadeTick >= 16)
                {
                    if (dlg.open())
                    {
                        gp::DialogueBox &b = *dlg.box;
                        if (k == SDLK_LEFT)
                            b.yesSelected = true; // Yes is on the left
                        else if (k == SDLK_RIGHT)
                            b.yesSelected = false;
                        else if (k == SDLK_z)
                            dlg.choose(b.yesSelected ? 1 : 2); // closes first; don't use b after this
                        else if (k == SDLK_x)
                            dlg.choose(2); // Esc = No
                    }
                    else if (k == SDLK_UP && houseCur > 0)
                        --houseCur;
                    else if (k == SDLK_DOWN && houseCur < 3)
                        ++houseCur;
                    else if (k == SDLK_x)
                        goMenu(); // event 5 -> state 4
                    else if (k == SDLK_z)
                    {
                        if (houseCur == 3)
                            goMenu(); // Back
                        else if (!slotFull[houseCur])
                            create(houseCur);
                        else
                        {
                            gp::DialogueBox b = gp::messageBox("This will remove " + slotName[houseCur] +
                                                               " from the game.  Are you sure you want this?");
                            b.kind = gp::BoxKind::Confirm;
                            b.yesSelected = false; // the real default is a guess; No is the safe one for a delete
                            const int slot = houseCur;
                            b.onYes = [&create, slot]
                            { create(slot); };
                            dlg.show(std::move(b));
                        }
                    }
                }
                else if (screen == Screen::Name && down)
                {
                    using In = NameEntry::In;
                    NameEntry::Result r;
                    bool acted = true;
                    if (k == SDLK_LEFT)
                        r = name.step(In::Left);
                    else if (k == SDLK_RIGHT)
                        r = name.step(In::Right);
                    else if (k == SDLK_UP)
                        r = name.step(In::Up);
                    else if (k == SDLK_DOWN)
                        r = name.step(In::Down);
                    else if (k == SDLK_z && !rep)
                        r = name.step(In::Confirm);
                    else
                        acted = false;
                    if (acted && r.typed)
                        pressedUntil = SDL_GetTicks() + 200; // WaitForElapsedTime(0.2)
                    if (r.done)
                    {
                        std::printf("Done: name \"%s\" -> InitPetName, state 7 (house select)\n", name.name.c_str());
                        houseCur = firstEmptySlot();
                        dlg.box.reset();
                        setScreen(Screen::House);
                    }
                }
                else if (screen == Screen::Browse && down)
                {
                    const int step = shift ? 16 : 1;
                    bool changed = true;
                    if (k == SDLK_LEFT)
                        browseA = std::max(0, browseA - 1);
                    else if (k == SDLK_RIGHT)
                        ++browseA;
                    else if (k == SDLK_UP)
                        browsePal += step;
                    else if (k == SDLK_DOWN)
                        browsePal -= step;
                    else
                        changed = false;
                    if (changed)
                        std::printf("browse a=0x%x pal=0x%x\n", browseA, browsePal);
                }
                else if (screen == Screen::Starter && down && !rep && fadeTick >= 16)
                {
                    if (k == SDLK_x)
                        goMenu();
                    else if (starter.confirmed < 0)
                    {
                        int c = starter.cursor;
                        if (k == SDLK_LEFT && c >= 2)
                            c -= 2;
                        else if (k == SDLK_RIGHT && c < 10)
                            c += 2;
                        else if (k == SDLK_UP || k == SDLK_DOWN)
                            c ^= 1;
                        else if (k == SDLK_z)
                            starter.confirm();
                        if (c != starter.cursor)
                        {
                            starter.cursor = c;
                            starter.changed = true;
                        }
                    }
                }
            }

            // ---- draw
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            gfx.q.clear();
            gfx.texts.clear();
            gfx.lastText.clear();

            // 30 Hz game ticks (drives the fade and the boot slide)
            const Uint32 now = SDL_GetTicks();
            tickAcc += (now - lastMs) * 30.0 / 1000.0;
            lastMs = now;
            int ticks = 0;
            while (tickAcc >= 1.0)
            {
                ++ticks;
                tickAcc -= 1.0;
            }
            const bool hitch = fadeSkip > 0; // right after a screen change: ignore the texture-upload hitch
            if (hitch)
                --fadeSkip;
            const int gt = hitch ? 0 : std::min(ticks, 3); // ticks for fades and the menu slide
            if (hasPending)
            {
                fadeOutTick = std::min(kFade, fadeOutTick + gt);
                if (fadeOutTick >= kFade) // black: swap and fade the new screen in
                {
                    screen = pendingScreen;
                    hasPending = false;
                    if (screen == Screen::Menu && pendingSlide)
                        resetSlide();
                    pendingSlide = false;
                    startFade();
                }
            }
            else if (leaving)
            {
                fadeOutTick = std::min(kFade, fadeOutTick + gt);
                if (fadeOutTick >= kFade)
                    run = false; // black: chosenSlot is already set, walk takes over
            }
            else if (fadeTick < kFade)
                fadeTick = std::min(kFade, fadeTick + gt);

            switch (screen)
            {
            case Screen::Boot:
            {
                if (bootPhase == 0) // fade in (fadeTick), then hold the logos
                {
                    app.drawBackground(1, 0x5c7);
                    if (now - bootT0 >= kLogoMs)
                    {
                        bootPhase = 1;
                        outTick = 0;
                    }
                    break;
                }
                if (bootPhase == 1) // logos fade out to black
                {
                    app.drawBackground(1, 0x5c7);
                    outTick += ticks;
                    if (outTick >= 16)
                    {
                        bootPhase = 2;
                        startFade(); // plain background fades in
                        menu.a = -0xac;
                        menu.b = 0x88;
                        menu.c = -0x8c;
                        menu.ready = false;
                    }
                    break;
                }
                for (int i = 0; i < gt; ++i) // phase 2: slide in
                    stepSlide();
                app.drawBackground(2, 0x5c5);
                drawMainMenu(gfx, menu);
                if (menu.ready)
                    screen = Screen::Menu; // plain assignment: no new fade
                break;
            }
            case Screen::Menu:
                for (int i = 0; i < gt && !menu.ready; ++i) // replayed slide-in after the pet flow
                    stepSlide();
                app.drawBackground(2, 0x5c5);
                drawMainMenu(gfx, menu);
                break;
            case Screen::Intro:
                app.drawBackground(0, 0x5c3);
                drawIntro(gfx, menu);
                break;
            case Screen::Starter:
            {
                for (int i = 0; i < ticks; ++i)
                    starter.tick(app, randBelow);
                starter.draw(app, gfx, now);
                if (starter.finished)
                {
                    const auto &sp = starter.pets[starter.confirmed];
                    chosen.species = sp.species;
                    chosen.colour = sp.colour;
                    for (int j = 0; j < 4; ++j)
                        chosen.stat[j] = sp.stat[j];
                    chosen.sick = sp.react; // the flag the selector pet carries (the one that keeps reacting)
                    std::printf("picked species %d colour 0x%x -> state 0x12\n", chosen.species, chosen.colour);
                    starter.finished = false;
                    setScreen(Screen::Name); // fades out with the starter scene still on screen
                }
                gfx.drawTile(0x2fb, 0x28c, 0, 0xb0, 0x1000, 999);
                const auto &cp = starter.pets[starter.cursor];
                char buf[16];
                static const int sx[4] = {0x78, 0x78, 0xaf, 0xaf}, sy[4] = {0xbe, 0xd4, 0xbe, 0xd4};
                for (int j = 0; j < 4; ++j)
                {
                    std::snprintf(buf, sizeof buf, "%d", cp.stat[j]);
                    gfx.drawTextBox(2, 0x28c, sx[j], sy[j], buf, 0x1e, 2, 0x1000);
                }
                break;
            }
            case Screen::Load:
                app.drawBackground(3, 0x5c6);
                drawLoad(gfx, loadCur, loadPicked, slotName, slotFull, slotSpecies, slotColour);
                if (loadPicked && !leaving && !hasPending && now - loadPickedAt >= 200) // WaitForElapsedTime; the real length is unread
                {
                    std::printf("LoadSaveSlot(%d) \"%s\" -> state 0xa (gameplay)\n", loadCur, slotName[loadCur].c_str());
                    if (launchGame)
                        startGame(loadCur); // loadPicked stays set so the picked look holds through the fade
                    else
                    {
                        loadPicked = false;
                        goMenu(); // the demo stops here
                    }
                }
                break;
            case Screen::House:
                app.drawBackground(2, 0x5c5);
                drawHouse(gfx, houseCur, slotName, slotFull);
                if (dlg.open())
                    drawConfirm(*dlg.box);
                break;
            case Screen::Name:
                app.drawBackground(0x1f, 0x5dd);
                drawNameEntry(gfx, name, species, SDL_GetTicks() < pressedUntil);
                break;
            case Screen::Browse:
                app.drawBackground(browseA, browsePal);
                break;
            }

            app.flush(gfx);
            for (const auto &t : gfx.texts)
            {
                const gp::Align al = t.mode == 1   ? gp::Align::Center
                                     : t.mode == 2 ? gp::Align::Right
                                                   : gp::Align::Left;
                fontFor(t.style, t.pal).draw(t.s, t.x, t.y, t.w, al);
            }
            if (showOutlines)
            {
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                for (const auto &t : gfx.texts)
                {
                    SDL_Rect r{t.x, t.y, std::min(t.w, int(t.s.size()) * 6), 10};
                    SDL_RenderDrawRect(ren, &r);
                }
            }
            int alpha = 0;
            if (hasPending || leaving)
                alpha = 255 * std::min(fadeOutTick, kFade) / kFade;
            else if (fadeTick < kFade)
                alpha = 255 * (kFade - fadeTick) / kFade;
            if (screen == Screen::Boot && bootPhase == 1)
                alpha = 255 * std::min(outTick, 16) / 16;
            if (alpha > 0)
            {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, uint8_t(alpha));
                SDL_RenderFillRect(ren, nullptr);
            }
            SDL_RenderPresent(ren);

            std::string title = "front-end demo";
            if (screen == Screen::Name)
                title += "  name: [" + gfx.lastText + "]";
            if (title != lastTitle)
            {
                SDL_SetWindowTitle(win, title.c_str());
                lastTitle = title;
            }
        }
        app.clearCaches();
    }
    catch (const std::exception &ex)
    {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return -1;
    }
    return chosenSlot;
}

#ifndef GP_NO_MAIN
// Standalone mode, same command line as before: frontend_demo rom.u7 [--frames N]
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: frontend_demo rom.u7 [--frames N]\n");
        return 1;
    }
    int maxFrames = -1;
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--frames"))
            maxFrames = std::atoi(argv[i + 1]);
    try
    {
        gp::AppHost host(argv[1], "front-end demo");
        runFrontEnd(host.ctx, false, false, maxFrames);
    }
    catch (const std::exception &ex)
    {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 2;
    }
    return 0;
}
#endif