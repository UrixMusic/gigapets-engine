// tools/frontend_demo.cpp
// Standalone viewer for the front-end screens. Needs only rom.hpp, sprite.hpp, menu_draw.hpp, name_entry.hpp.
//   frontend_demo rom.u7 [--frames N] [--smooth]
//
// Keys (all screens):  F1 main menu   F2 name entry   F3 background browser
//                      F5 cycle sprite placement mode   F6 toggle red/blue swap   F7 species 0/1/2/3 (name screen)
//                      F4 toggle the optional smooth (sub-pixel) mode - the same switch as walk's F5; --smooth starts with it on
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

#include "../game/script_draw.hpp"
#include "../engine/gl_shader.hpp"
#include "../game/menu_draw.hpp"
#include "../game/name_entry.hpp"
#include "rom.hpp"
#include "sprite.hpp"
#include "../game/player.hpp"
#include "../game/save.hpp"
#include "../game/save_player.hpp" // whatever the savePlayer header is called
#include "../game/debug_mode.hpp"
#include "../game/test_mode.hpp"
#include "../game/settings.hpp"
#include "../engine/audio.hpp"
#include "../engine/display.hpp"
#include "../engine/keybinds.hpp"
#include "../game/pet_voice.hpp"
#include "../game/potion.hpp"
#include "../game/thought_bubble.hpp"

static void feSound(int id, int volume) { gp::audio().playSfx(id, volume); } // PlaySoundEffect(id, volume, 0)
// PlaySoundFromCategory(species, category, volume)
static void feVoice(int species, int category, int volume = 0xbe)
{
    if (const int id = gp::petVoiceId(species, category); id >= 0)
        gp::audio().playSfx(id, volume);
}
#include "../game/dialogue.hpp"
#include "app_context.hpp"
#include "../loaders/import.hpp" // ContentDB + importRom: that is where the fonts live
#include "../mods/mod_loader.hpp" // the mods' palettes: the slot pets wear their saved colours
#include "../game/text_draw.hpp"
#include "../game/starter_pool.hpp" // who the starter room holds, and the colour rolls
#include "../game/pet_colour_save.hpp" // a mod colour is saved by its id
#include "../game/profile.hpp" // what a new game offers (a species can need a profile key)
#include <memory>

using namespace gp;

// Optional sub-pixel mode (F4 / --smooth). One switch shared with walk.cpp: in the unity build (GP_NO_MAIN) walk.cpp is
// included first and defines it; standalone, this file defines its own.
#ifndef GP_NO_MAIN
bool smoothCam = false;
#endif

// A value that moves in whole-pixel steps once per 30 Hz tick, shown part-way (alpha, 0..1) between its previous and its
// current value. Falls back to the plain value when smooth mode is off, or when the change was a jump rather than a step.
inline float feSmooth(int prev, int now, float alpha, int maxStep)
{
    if (!smoothCam || std::abs(now - prev) > maxStep)
        return float(now);
    return float(prev) + float(now - prev) * alpha;
}

// ---- ROM helpers ------------------------------------------------------------------------------

// ---- draw queue -------------------------------------------------------------------------------

struct FrontGfx : Gfx
{
    struct Cmd
    {
        int layer, seq, sprite, pal;
        float x, y;
    };
    std::vector<Cmd> q;
    std::string lastText;
    void drawTile(int s, int p, int x, int y, int layer, int) override
    {
        drawTileF(s, p, float(x), float(y), layer);
    }
    void drawTileF(int s, int p, float x, float y, int layer) // same, at a fractional position (smooth mode)
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
    int pa = 0, pb = 0, pc = 8;     // the same before the latest slide tick
    float fa = 0, fb = 0, fc = 8;   // what is drawn: a, b, c, or part-way between pa..a while sliding in smooth mode
    int colour[3] = {0, 0, 0};
    int cursor = 1;
    bool pressed = false;
    bool ready = true; // false while sprites are still sliding in
};

inline void drawMainMenu(FrontGfx &g, const MenuView &v)
{
    constexpr int P = 0x4d5, L = 0x3000, BP = 0x4c7;
    g.drawTileF(0x9f4, P, v.fb + 0xb8, 0, L);
    g.drawTileF(0x9f1, P, v.fa, 0, L);
    g.drawTileF(0x9f3, v.colour[2], v.fa + 0x1b, 0x29, L);
    g.drawTileF(0x9f0, v.colour[0], v.fa, 0x7c, L);
    g.drawTileF(0x9f2, v.colour[1], v.fb + 0xcf, 0x45, L);
    g.drawTileF(0x9e6, BP, 0x5f, v.fc, L);
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
        g.drawTileF(btns[i].t[st], BP, btns[i].x, btns[i].y, L);
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
    if (cur < 3 && full[cur] && sp[cur] >= 0 && gp::knownSpecies(sp[cur]))
    {
        const gp::SpeciesArt &art = gp::artOf(sp[cur]); // the ROM's big picture (the base species, and their clones); the others are drawn by App::drawLoadPicture
        if (art.loadPicture < 0 && art.loadTile >= 0)
            g.drawTile(art.loadTile, col[cur], art.loadX, art.loadY, L, 999);
    }
    g.drawTile(0x9e6, 0x4c7, 0x5f, 8, L, 999);
    for (int i = 0; i < 4; ++i)
    {
        const std::string label = i == 3 ? "Back" : (full[i] ? names[i] : "Empty");
        g.drawTextBox(i == cur ? (picked ? 6 : 5) : 4, 0x4c7, 0, ys[i], label, 0x140, 1, L);
    }
}

// The options screen (ShowOptionsMenuScreen 0x03da32) grown into a scrolling list: music volume, SFX volume, master volume,
// tutorials, interpolation, scale, scale mode, window mode, Done (settings.hpp). The logo stays put and kOptVisible rows show
// under it; the list scrolls as the selection goes past the ends. Music, master volume, tutorials and Done keep the ROM's
// 3-state button art (normal, highlighted, pressed); the others are drawn as text, since there is no art for them.
inline void drawOptions(Gfx &g, int sel, bool pressed, const gp::Settings &o, int scroll, int capture, int rowCount = gp::kOptRows)
{
    constexpr int L = 0x3000, BP = 0x4b8, kTop = 0x6f, kPitch = 24, kLabelX = 0x1e, kLabelW = 0x8c, kValueX = 0xaa, kValueW = 0x7c; // the value ends at x 0x126
    // The long words don't fit beside each other in the big font ("Window Mode" 190 px, "Borderless" 158 px of 320), so the rows
    // with no art use the small one (style 2: 94 px and 65 px). "SFX" keeps the big font and the position it was tuned to.
    struct Row
    {
        int s[3], x;
        const char *label;
    };
    static const Row rows[gp::kOptRowsDebug] = { // sprites and x from the ROM's table at 0xf47c
        {{0x9d8, 0x9de, 0x9de}, 0x1e, nullptr},
        {{-1, -1, -1}, 0x1e, "SFX"},
        {{0x9dc, 0x9e2, 0x9e2}, 0x1e, nullptr},
        {{0x9db, 0x9e1, 0x9e2}, 0x1e, nullptr},
        {{-1, -1, -1}, 0x1e, "Interpolate"},
        {{-1, -1, -1}, 0x1e, "Scale"},
        {{-1, -1, -1}, 0x1e, "Scale Mode"},
        {{-1, -1, -1}, 0x1e, "Window Mode"},
        {{-1, -1, -1}, 0x1e, "Aspect Ratio"},
        {{-1, -1, -1}, 0x1e, "Retro Audio"},
        {{-1, -1, -1}, 0x1e, "Music Playback"},
        {{-1, -1, -1}, 0x1e, ""}, {{-1, -1, -1}, 0x1e, ""}, {{-1, -1, -1}, 0x1e, ""}, {{-1, -1, -1}, 0x1e, ""}, // the key rows: named below
        {{-1, -1, -1}, 0x1e, ""}, {{-1, -1, -1}, 0x1e, ""}, {{-1, -1, -1}, 0x1e, ""},
        {{0x9d7, 0x9dd, 0x9ed}, 0x82, nullptr},
        {{-1, -1, -1}, 0x1e, "Test Mode"}, // debug mode only
    };
    static const char *const kWindowNames[gp::kWindowModeCount] = {"Windowed", "Fullscreen", "Borderless"};
    g.drawTile(0x9e6, 0x4c7, 0x5f, 8, L, 999);
    auto value = [&](int row, int y, const std::string &text, bool small = false)
    {
        if (small)
            g.drawTextBox(2, 0x28c, kValueX, y + 4, text, kValueW, 2, L); // (the small font has only this palette, 0x28c)
        else
            g.drawTextBox(sel == row ? 5 : 4, sel == row ? 0x106 : 0x105, kValueX, y, text, kValueW, 2, L);
    };
    auto onOff = [&](int row, int y, bool on)
    {
        const bool hot = sel == row;
        g.drawTile(on ? (hot ? 0x9e0 : 0x9da) : (hot ? 0x9df : 0x9d9), BP, 0xff, y, L, 999);
    };
    const int last = std::min(rowCount, scroll + gp::kOptVisible);
    for (int i = scroll; i < last; ++i)
    {
        const int y = kTop + (i - scroll) * kPitch;
        if (rows[i].s[0] >= 0)
            g.drawTile(rows[i].s[i == sel ? (pressed ? 2 : 1) : 0], BP, rows[i].x, y, L, 999);
        else if (i == gp::kOptSfx) // the big-font label, centred where it was tuned
            g.drawTextBox(sel == i ? 5 : 4, 0x4c7, rows[i].x - 11, y + 2, rows[i].label, 0x50, 1, L);
        else // the small-font names, left-aligned where the buttons' art starts; the value is right-aligned (below)
        {
            const std::string name = gp::isKeyRow(i) ? std::string("Key: ") + gp::kActionNames[gp::keyRowAction(i)] : std::string(rows[i].label);
            g.drawTextBox(2, 0x28c, kLabelX, y + 4, name, kLabelW, 0, L);
            if (sel == i) // the small font has no highlight colour, so the selected row gets a marker
                g.drawTextBox(2, 0x28c, kLabelX - 12, y + 4, ">", 0x10, 0, L);
        }
        char buf[16];
        switch (i)
        {
        case gp::kOptMusic:
            std::snprintf(buf, sizeof buf, "%3d", o.musicVolume);
            value(i, y, buf);
            break;
        case gp::kOptSfx:
            std::snprintf(buf, sizeof buf, "%3d", o.sfxVolume);
            value(i, y, buf);
            break;
        case gp::kOptVolume:
            std::snprintf(buf, sizeof buf, "%3d", o.volume);
            value(i, y, buf);
            break;
        case gp::kOptTutorials:
            onOff(i, y, o.tutorials);
            break;
        case gp::kOptInterpolate:
            value(i, y, o.interpolate ? "On" : "Off", true); // the new switches are small-font text, not the ROM's tiles
            break;
        case gp::kOptScale:
            std::snprintf(buf, sizeof buf, "%dx", o.scale);
            value(i, y, buf, true);
            break;
        case gp::kOptScaleMode:
            value(i, y, o.perfectScale ? "Perfect" : "Stretch", true);
            break;
        case gp::kOptAspect:
            value(i, y, o.aspect == 1 ? "16:9" : "4:3", true);
            break;
        case gp::kOptRetro:
            value(i, y, o.retroAudio ? "On" : "Off", true);
            break;
        case gp::kOptLive:
            value(i, y, o.liveMusic ? "Live" : "Baked", true);
            break;
        case gp::kOptWindowMode:
            value(i, y, kWindowNames[std::clamp(o.windowMode, 0, gp::kWindowModeCount - 1)], true);
            break;
        default:
            if (gp::isKeyRow(i)) // a key binding: the key's name, or the prompt while it waits for one
                value(i, y, capture == gp::keyRowAction(i) ? std::string("Press a key") : gp::keys().name(gp::keyRowAction(i)), true);
            break;
        }
    }
    if (scroll > 0) // more above / below
        g.drawTextBox(4, 0x105, 0x12c, kTop - 6, "^", 0x10, 0, L);
    if (last < rowCount)
        g.drawTextBox(4, 0x105, 0x12c, kTop + (gp::kOptVisible - 1) * kPitch + 10, "v", 0x10, 0, L);
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
    const gp::ContentDB *modDb = nullptr; // where a mod's animations (numbers from gp::kModAnimBase) are
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
        if (gp::isModAnim(id)) // a mod's own frames: already coloured
        {
            if (const gp::Anim *ma = modDb ? modDb->anim(gp::modAnimName(id)) : nullptr)
                for (const gp::SpriteFrame &f : ma->frames)
                {
                    Tex t;
                    t.t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, f.w, f.h);
                    SDL_UpdateTexture(t.t, nullptr, f.rgba.data(), f.w * 4);
                    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
                    t.w = f.w;
                    t.h = f.h;
                    t.ax = f.anchorX;
                    t.ay = f.anchorY;
                    a.frames.push_back(t);
                    a.dx.push_back(f.dx);
                    a.dy.push_back(f.dy);
                }
            return anims.emplace(key, std::move(a)).first->second;
        }
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

    // The load screen's big picture of a species that has no ROM tile (or a mod's own picture): the first frame of its loadPicture, else its idle art.
    // By default it is scaled to fit the place of one of the three pets drawn on the background (loadSpot) and stands in its bottom middle; with
    // loadX / loadY it goes at its own size with its top-left there. (gp::SpeciesArt says more.)
    void drawLoadPicture(const gp::SpeciesArt &art, int palId)
    {
        if (art.loadPicture < 0 && art.loadTile >= 0)
            return; // the ROM's picture: drawn with the rest of the screen
        AnimTex &a = anim(art.loadPicture >= 0 ? art.loadPicture : art.idleBase, palId);
        if (a.frames.empty() || !a.frames[0].t)
            return;
        const Tex &t = a.frames[0];
        struct Spot { float x, y, w, h; };
        static const Spot spots[3] = {{0, 124, 96, 116}, {207, 69, 113, 171}, {27, 41, 72, 88}};
        const Spot &sp = spots[std::clamp(art.loadSpot, 0, 2)];
        SDL_FRect r;
        if (art.loadX >= 0 || art.loadY >= 0)
        {
            const float s = std::min(1.f, std::min(120.f / float(t.w), 176.f / float(t.h)));
            r = {art.loadX >= 0 ? float(art.loadX) : 0.f, art.loadY >= 0 ? float(art.loadY) : 240.f - float(t.h) * s, float(t.w) * s, float(t.h) * s};
        }
        else
        {
            float s = std::min(sp.w / float(t.w), sp.h / float(t.h));
            if (s >= 1.f)
                s = std::floor(s);
            r = {sp.x + (sp.w - float(t.w) * s) / 2, sp.y + sp.h - float(t.h) * s, float(t.w) * s, float(t.h) * s};
        }
        SDL_RenderCopyF(ren, t.t, nullptr, &r);
    }

    void drawAnim(int id, int palId, float x, float y, Uint32 ms, int fps = 18)
    {
        AnimTex &a = anim(id, palId);
        if (a.frames.empty())
            return;
        const int f = int(uint64_t(ms) * fps / 1000) % int(a.frames.size());
        const Tex &t = a.frames[f];
        SDL_FRect r{x - t.ax + a.dx[f], y - t.ay + a.dy[f], float(t.w), float(t.h)}; // same placement as your prompt arrow
        SDL_RenderCopyF(ren, t.t, nullptr, &r);
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

    void drawLayer(const Tex &t, float scroll)
    {
        if (!t.t)
            return;
        SDL_FRect dst{-scroll, 0.f, float(t.w), float(t.h)};
        SDL_RenderCopyF(ren, t.t, nullptr, &dst);
    }

    // Immediate sprite draw (no queue / layer sort), same placement modes as flush().
    void blit(int s, int p, float x, float y)
    {
        Tex &t = sprite(s, p);
        if (!t.t)
            return;
        float dx = x, dy = y;
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
        SDL_FRect dst{dx, dy, float(t.w), float(t.h)};
        SDL_RenderCopyF(ren, t.t, nullptr, &dst);
    }

    // One animation frame at a fixed frame index; flip mirrors around the anchor (assumption, check by eye).
    void drawFrame(const AnimTex &a, int f, float x, float y, bool flip)
    {
        const Tex &t = a.frames[f];
        SDL_FRect r{flip ? x - float(t.w - t.ax) - a.dx[f] : x - t.ax + a.dx[f], y - t.ay + a.dy[f], float(t.w), float(t.h)};
        SDL_RenderCopyExF(ren, t.t, nullptr, &r, 0.0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
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
            float dx = c.x, dy = c.y;
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
            SDL_FRect dst{dx, dy, float(t.w), float(t.h)};
            SDL_RenderCopyF(ren, t.t, nullptr, &dst);
        }
        g.q.clear();
    }
};

// The room's animation for one role, per species: the ROM's table for the three base species, the record's art for the others.
struct RoomTbl
{
    const int (&base)[3];
    int gp::SpeciesArt::*role;
    int operator()(int sp) const { return sp >= 0 && sp < 3 ? base[sp] : gp::knownSpecies(sp) ? gp::artOf(sp).*role : -1; }
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
        A_EMOTE, // PlayEmoteReactionAnim: the surprise
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
        int x = 0, px = 0, y = 0, slot = 0, slotX = 0, facing = 3, species = 0, colour = 0, stat[4] = {0, 0, 0, 0}; // px: x before the latest tick
        bool settled = false, react = false, legs = false, sad = false;
        Act act = A_NONE;
        int sub = 0, counter = 0, tx = 0, reps = 0;
        int ex = -1; // ticks since the exclamation bubble began over this pet, -1 = none
        double wait = 0;
        std::vector<Act> q;
        PetAnim a;
    };
    struct Decor
    {
        int spr, pal, x, y;
    };

    // anim id tables per species (from the PlayXxxAnim functions)
    static constexpr int bIdle[3] = {0x3d, 0xa5, 0x10c};
    static constexpr int bWalk[3] = {0x4e, 0xb5, 0x114};
    static constexpr int bDir[3] = {0x4c, 0xb3, 0x11c}; // turn to camera (PlayDirectionalIdleAnim)
    static constexpr int bSettled[3] = {0x45, 0xad, 0x115};
    static constexpr int bPos[3] = {0x44, 0xac, 0x113};
    static constexpr int bSpawn[3] = {0x47, 0xae, 0x116};
    static constexpr int bRep[3] = {0x3f, 0xa7, 0x10f};
    static constexpr int bPre[3] = {0x4b, -1, -1};
    static constexpr int bVoc[3] = {0x49, 0xaf, 0x117};
    // confirm reactions. Starter pets have actor word +3 = 1, which picks the second table in each function.
    static constexpr int bHappy[3] = {0x3c, 0xa4, 0x10d};   // PlayPositiveReactionAnimVariant
    static constexpr int bSad[3] = {0x40, 0xa9, 0x110};     // PlaySadReactionAnim
    static constexpr int bSadIdle[3] = {0x41, 0xa8, 0x111}; // PlayIdleAnim_FlagA, second table (action 0x1d, TickActor_NegativeReaction)
    // PlayEmoteReactionAnim -> SelectAndPlayEmoteAnim / PlayShockedReactionAnim, second table (starter pets are style 1)
    static constexpr int bEmote[3] = {0x43, 0xaa, 0x112};
    static constexpr int bShock[3] = {0x42, 0xab, -1};
    static constexpr int bCelPre[3] = {-1, -1, 0x118};      // SelectAndPlayReactionAnim: intro/outro, third species only

    // the species a mod adds use their record's art for each role (idle, walk, ...)
    static inline const RoomTbl kIdle{bIdle, &gp::SpeciesArt::idleBase};
    static inline const RoomTbl kWalk{bWalk, &gp::SpeciesArt::walkSide};
    static inline const RoomTbl kDir{bDir, &gp::SpeciesArt::callFront};
    static inline const RoomTbl kSettled{bSettled, &gp::SpeciesArt::settled};
    static inline const RoomTbl kPos{bPos, &gp::SpeciesArt::posDown};
    static inline const RoomTbl kSpawn{bSpawn, &gp::SpeciesArt::idleBase};
    static inline const RoomTbl kRep{bRep, &gp::SpeciesArt::callFront};
    static inline const RoomTbl kPre{bPre, &gp::SpeciesArt::introDown};
    static inline const RoomTbl kVoc{bVoc, &gp::SpeciesArt::callFront};
    static inline const RoomTbl kHappy{bHappy, &gp::SpeciesArt::posDown};
    static inline const RoomTbl kSad{bSad, &gp::SpeciesArt::sad};
    static inline const RoomTbl kSadIdle{bSadIdle, &gp::SpeciesArt::idleFlagA};
    static inline const RoomTbl kEmote{bEmote, &gp::SpeciesArt::emoDown};
    static inline const RoomTbl kShock{bShock, &gp::SpeciesArt::shkDown};
    static inline const RoomTbl kCelPre{bCelPre, &gp::SpeciesArt::introDown};

    std::vector<P> pets;
    std::vector<Decor> decor;
    int cursor = 0, confirmed = -1, scroll = 0x50, camT = 0xa0, arrowX = 0, arrowY = 0x73;
    int pScroll = 0x50, pArrowX = 0, pArrowY = 0x73; // scroll / arrow before the latest tick (smooth mode)
    float alpha = 1.f;                               // 0..1 through the current 30 Hz tick; the caller sets it every frame
    float sm(int prev, int now) const { return feSmooth(prev, now, alpha, 12); }
    bool changed = false, finished = false;
    int bgCols = 0x48, maxScroll = 0x7c0;
    uint32_t mapA = 0, mapB = 0;
    int frameA = 0, frameB = 0, palA = 0, palB = 0;

    // ---- setup -----------------------------------------------------------------------------
    void start(const Rom &rom, const RB &rb, const StarterPool &pool)
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

        int sp[12];
        const bool rom3 = pool.romRoom();
        rollRoomSpecies(pool, rb, sp); // see starter_pool.hpp
        auto entryOf = [&](int species) -> const StarterPool::Entry &
        {
            for (const StarterPool::Entry &e : pool.entries)
                if (e.species == species)
                    return e;
            return pool.entries[0];
        };
        pets.assign(12, P());
        std::map<int, int> rareDone, commonDone;
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
                    colour = rom3 ? rollStarterColor(rom, s, rare ? 1 : 0, rb) : poolColour(entryOf(s), rare, rb, [&](int sp2, int mode) { return rollStarterColor(rom, sp2, mode, rb); });
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
        pScroll = scroll; // nothing glides in from a previous scene
        pArrowX = arrowX;
        pArrowY = arrowY;
        for (P &q : pets)
            q.px = q.x;
    }

    // ---- actions ---------------------------------------------------------------------------
    static void begin(P &p, Act a)
    {
        p.act = a;
        p.sub = 0;
    }
    static void play(P &p, const RoomTbl &tbl, int fps, bool loop = false, bool flip = false, bool rev = false)
    {
        if (tbl(p.species) < 0)
            return;
        p.a = PetAnim{tbl(p.species), 0.0, fps, loop, flip, rev};
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
        else if (r < 7 || (r >= 11 && r < 15)) // walk in, settle (the first kind then is surprised: action 5)
        {
            p.facing = p.x <= p.slotX ? 1 : 0;
            p.legs = true;
            p.counter = 1 + rb(3);
            p.tx = p.facing == 1 ? p.x + 0x1e : p.x - 0x1e;
            p.q = {A_SETTLE};
            if (r < 7)
                p.q.push_back(A_EMOTE);
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
    bool once(App &app, P &p, const RoomTbl &tbl, int fps)
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
            if (p.sub == 0)
                feVoice(p.species, 4); // PlayPositiveReactionAnim: category 4
            return once(app, p, kPos, s0 ? 14 : 7);
        case A_SETTLED:
            return once(app, p, kSettled, s0 ? 7 : 5);
        case A_SPAWN:
            if (p.sub == 0)
                feSound(6, 0xbe); // TickActor_PlaySoundAndWait: the spawn effect's sound
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
            const bool pre = kPre(p.species) >= 0;
            if (p.sub == 0)
            {
                if (pre)
                {
                    play(p, kPre, 14);
                    p.sub = 1;
                }
                else
                {
                    play(p, kRep, 14), feVoice(p.species, 4);
                    p.sub = 2;
                }
            }
            else if (p.sub == 1)
            {
                if (!busy(app, p))
                {
                    play(p, kRep, 14), feVoice(p.species, 4);
                    p.sub = 2;
                }
            }
            else if (p.sub == 2)
            {
                if (!busy(app, p))
                {
                    if (--p.reps > 0)
                        play(p, kRep, 14), feVoice(p.species, 4);
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
                const int r = 1 + rb(4); // TickActor_IdleRandomSound: 2 -> category 1 (quiet in the ROM), 3 -> 1, else 2
                feVoice(p.species, r == 2 || r == 3 ? 1 : 2);
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
            const bool pre = kCelPre(p.species) >= 0;
            if (p.sub == 0)
            {
                p.reps = 3;
                feVoice(p.species, 4);
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
                    {
                        play(p, kHappy, 14);
                        feVoice(p.species, 4);
                    }
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
        case A_EMOTE:
            if (p.sub == 0)
            {
                play(p, kEmote, 14);
                feVoice(p.species, 9, 0xff);
                p.ex = 0;
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
                    if (kShock(p.species) < 0)
                        return true;
                    play(p, kShock, 14);
                    p.sub = 3;
                }
            }
            else
                return !busy(app, p);
            return false;
        case A_SADIDLE: // action 0x1d, TickActor_NegativeReaction: the sad idle, picked again and again
            return once(app, p, kSadIdle, 14);
        case A_SAD: // TickNPCReactionAnimationSequence: sad anim, pause (float 0x3f00 = 0.5, unchecked), sad anim again
                    // The second PlaySadReactionAnim is followed by "actor+0xb = 4" (the same flag-4 the celebration
                    // outro uses), which is the reverse play: the pet un-does the sad pose on the way out.
            if (p.sub == 0)
            {
                play(p, kSad, 14);
                feVoice(p.species, 0xb); // the sad sequence's sound
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
    bool visible(App &app, P &p, int margin = 0) // margin: extra pixels of slack, for drawing at fractional positions
    {
        if (p.a.id < 0)
            return true;
        App::AnimTex &A = app.anim(p.a.id, p.colour);
        if (A.frames.empty())
            return true;
        const Tex &t = A.frames[frameIndex(A, p)];
        const int left = p.x - scroll - t.ax;
        return left + t.w > -margin && left <= 0x13f + margin;
    }

    void confirm()
    {
        if (changed || confirmed >= 0)
            return;
        confirmed = cursor;
        feSound(0x59, 0xbe); // HandleStarterPetSelectInput
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
        pScroll = scroll; // remember where everything was before this tick, for the smooth renderer
        pArrowX = arrowX;
        pArrowY = arrowY;
        for (P &q : pets)
            q.px = q.x;
        for (P &p : pets)
        {
            if (!visible(app, p))
                continue; // off-screen pets are frozen, like the original
            step(app, p, rb);
            p.a.t += 1.0 / 30;
        }
        for (P &p : pets)
            if (p.ex >= 0)
                ++p.ex;
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

    void drawPet(App &app, P &p, float fScroll)
    {
        if (p.a.id < 0)
            return;
        App::AnimTex &A = app.anim(p.a.id, p.colour);
        if (A.frames.empty())
            return;
        const int f = frameIndex(A, p), h = A.frames[f].w;
        const float vx = sm(p.px, p.x) - fScroll;
        int idx = (h - 16 >= 0) ? (h - 16) >> 3 : (h - 9) >> 3; // DrawShadow
        idx = std::clamp(idx, 0, 11);
        const bool s2 = gp::artOf(p.species).wideShadow;
        app.blit((s2 ? 0x9a0 : 0x994) + idx, 0x28c, vx - (idx * 8 + 8) / 2, p.y - (s2 ? 0x10 : 6));
        app.drawFrame(A, f, vx, p.y, p.a.flip);
        if (p.ex >= 0) // the exclamation over the head (SpawnMessageBubble 0xb, then 0xc)
        {
            App::AnimTex &I = app.anim(gp::kExclaimIntroAnim, 0x28c), &O = app.anim(gp::kExclaimOutroAnim, 0x28c);
            const gp::ExclaimView v = gp::exclaimView(p.ex, int(I.frames.size()), int(O.frames.size()));
            App::AnimTex &B = v.anim == gp::kExclaimOutroAnim ? O : I;
            if (v.done)
                p.ex = -1;
            else if (!B.frames.empty())
            {
                const gp::BubbleOffset bo = gp::overheadOffset(p.species, 2, p.a.id);
                app.drawFrame(B, std::min(v.frame, int(B.frames.size()) - 1), vx + bo.dx, p.y + bo.dy, false); // the pets here sit at their ROM positions, so no bubbleLower
            }
        }
    }

    void draw(App &app, FrontGfx &gfx, Uint32 now)
    {
        const float fScroll = sm(pScroll, scroll); // == scroll unless smooth mode is on
        const int margin = smoothCam ? 2 : 0;
        app.drawLayer(app.tileLayer(mapA, bgCols, 8, frameA, palA), fScroll);
        app.drawLayer(app.tileLayer(mapB, bgCols, 8, frameB, palB), fScroll);
        auto band = [&](int lo, int hi)
        {
            for (const Decor &d : decor)
                if (d.y >= lo && d.y <= hi)
                    app.blit(d.spr, d.pal, d.x - fScroll, d.y);
        };
        int first = -1, last = -1;
        auto row = [&](int y)
        {
            for (int i = 0; i < int(pets.size()); ++i)
            {
                P &p = pets[i];
                if (p.y != y || !visible(app, p, margin))
                    continue;
                if (first < 0)
                    first = i;
                last = i;
                drawPet(app, p, fScroll);
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
            app.drawAnim(0x11, 0x28c, sm(pArrowX, arrowX) - fScroll, sm(pArrowY, arrowY) - h, now);
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
    Browse,
    Options,
    TestMode
};

// The ROM's test mode screens (game/test_mode.hpp). Coordinates and strings are from TestModeMenu (0x045d9d) and the loops it starts.
enum class TmScreen
{
    Menu,
    Button,
    Sound,
    Memory,
    Video1,
    Video2,
    Carrier,
    Handheld
};

inline void drawTestText(Gfx &g, int x, int y, const std::string &s, int w = 0x120, int mode = 0)
{
    g.drawTextBox(2, 0x28c, x, y, s, w, mode, 0x3000);
}

inline void drawTestMenu(Gfx &g, int sel)
{
    drawTestText(g, 0x10, 0x10, gp::testmode::kTitle);
    drawTestText(g, 0x10, 0x10, gp::testmode::kSubtitle, 0x120, 2);
    for (int i = 0; i < gp::testmode::kRows; ++i) // DrawNumberedSelectableList: the chosen row is in the big font
    {
        const int y = 0x30 + i * 0x10, style = i == sel ? 0 : 2;
        g.drawTextBox(style, 0x28c, 0x4b, y, std::to_string(i + 1) + ".", 0x19, 0, 0x3000);
        g.drawTextBox(style, 0x28c, 100, y, gp::testmode::kRowNames[i], 0x140, 0, 0x3000);
    }
    drawTestText(g, 0x10, 200, std::string("VERSION ") + gp::testmode::kVersion);
    drawTestText(g, 0x10, 200, gp::testmode::kDate, 0x120, 2);
}

inline void drawSoundTest(Gfx &g, int id)
{
    static const char *const keys[5] = {"LEFT -", "RIGHT -", "DOWN -", "UP -", "CANCEL -"};
    static const char *const what[5] = {"PLAY PREVIOUS SOUND", "PLAY NEXT SOUND", "PLAY ALL SOUNDS", "REPLAY CURRENT SOUND", "EXIT SOUND TEST"};
    for (int i = 0; i < 5; ++i)
    {
        drawTestText(g, 0x10, 0x10 + i * 0x10, keys[i], 0x50, 2);
        drawTestText(g, 0x68, 0x10 + i * 0x10, what[i], 200);
    }
    drawTestText(g, 0x10, 0x80, "CURRENT SOUND - " + std::to_string(id), 300);
}

inline void drawButtonTest(Gfx &g, const gp::testmode::ButtonTest &t)
{
    using namespace gp::testmode;
    g.drawTile(kControllerSprite, kControllerPalette, kControllerX, kControllerY, 0x3000, 999);
    if (t.lit >= 0)
        g.drawTile(buttonIconSprite(t.lit), kControllerPalette, kControllerX + buttonIconX(t.lit), kControllerY + buttonIconY(t.lit), 0x3000, 999);
    if (!t.done())
    {
        drawTestText(g, 0xa0, 0x10, std::string("PRESS THE ") + kButtonNames[t.step] + " BUTTON.", 0x8c);
        if (t.verdict)
            drawTestText(g, 0xa0, 0x60, t.verdict == 1 ? "SUCCESS" : "FAIL", 0x8c);
    }
    else
    {
        drawTestText(g, 0xa0, 0x10, "DONE WITH TEST.", 0x8c);
        drawTestText(g, 0xa0, 0x20, "HOLD THE CANCEL BUTTON TO EXIT.", 0x8c);
    }
    if (t.failed)
        g.drawTile(kFailSprite, kFailPalette, kFailX, kFailY, 0x3000, 999);
}

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
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest (SDL's default): crisp pixels at fractional positions
        SDL_RenderSetLogicalSize(ren, gp::logicalW(), 240);

        // Fonts come from the ContentDB. Built once (static) and reused when we come back from gameplay.
        // Species 0 is arbitrary: nothing the front end needs depends on it.
        static std::unique_ptr<gp::ContentDB> uiDb;
        static gp::ScriptHost feScripts; // the mods' scripts in the front end: Game.hooks().onHud draws over its screens, the systems frontend.menu / frontend.start (systems.hpp)
        if (!uiDb)
        {
            std::printf("front end: loading fonts (importRom)...\n");
            uiDb = std::make_unique<gp::ContentDB>();
            gp::importRom(rom, *uiDb, 0);
            // A pet that wears a mod's colour needs that palette to be drawn in the slot list: load the mods (their palette records are what
            // matter here) and fill the palette registry the sprite code looks mod colours up in. The game does the same when it starts.
            gp::ModReport feRep = gp::importMods(*uiDb, "mods");
            for (const std::string &p : gp::checkModScripts(*uiDb, feRep, feScripts, "mods")) // (a script that does not load is reported by the game itself, once it starts)
                std::printf("front end: script: %s\n", p.c_str());
            gp::resolveModPalettes(rom, *uiDb);
            // Every (font style, palette) pair the front end draws with, imported up front so the db never
            // changes once a TextDrawer holds a pointer into it. drawTextBox's first two arguments are
            // exactly (fontIndex, palette): the palette is baked into the glyphs by importFont.
            static const int kFrontFonts[][2] = {{0, 0x28c}, {2, 0x28c}, {3, 0x104}, {4, 0x4c7}, {5, 0x4c7}, {6, 0x4c7}, {4, 0x105}, {5, 0x106}};
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
        SpriteCache feSprites(ren, *uiDb); // the pictures a script draws in the front end
        ScriptDrawer feDrawer(ren);
        GlShaderPass feGl;
        feDrawer.shader = [&](const gp::DrawCmd &c, const std::vector<std::pair<std::string, SDL_Texture *>> &texs)
        {
            if (const std::string *src = uiDb->shader(c.str))
                feGl.apply(ren, c.str, *src, c.uniforms, float(SDL_GetTicks()) / 1000.f, gp::logicalW(), 240, texs);
        };
        feDrawer.sprite = [&](const std::string &id) -> AnimRef
        {
            AnimRef A;
            A.anim = uiDb->anim(id);
            A.tex = A.anim ? feSprites.animTextures(id) : nullptr;
            return A;
        };
        feDrawer.text = [&](const gp::DrawCmd &c)
        {
            std::string str;
            if (c.kind == gp::DrawCmd::Kind::Num)
                str = c.num == std::floor(c.num) && std::fabs(c.num) < 1e15 ? std::to_string(static_cast<long long>(c.num)) : std::to_string(c.num);
            else
                str = gp::fillTemplateVars(uiDb->textOr(c.str, ""), c.vars);
            const gp::Align al = c.align == 1 ? gp::Align::Center : c.align == 2 ? gp::Align::Right : gp::Align::Left;
            int bx = c.x, bw = c.width;
            if (bw == 0)
            {
                bw = 320;
                bx = c.align == 1 ? c.x - bw / 2 : c.align == 2 ? c.x - bw : c.x;
            }
            fontFor(c.font == 3 ? 3 : 0, c.font == 3 ? 0x104 : 0x28c).draw(str, bx, c.y, bw, al);
        };
        gp::DebugCode debugCode;
        bool showOutlines = false; // F8: the old white debug boxes

        App app(rom, ren);
        app.modDb = uiDb.get();
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
                int spn = si.species; // a mod's species is found by its id (the front end has loaded the mods)
                if (!si.speciesId.empty())
                    if (const int n = gp::speciesNumber(si.speciesId); n >= 0)
                        spn = n;
                slotSpecies[i] = si.full ? spn : -1;
                const int sp = std::clamp(spn, 0, 2);
                slotColour[i] = si.appearance >= 0 ? si.appearance : menu.colour[sp]; // old saves: random menu colour
                if (!si.appearanceId.empty()) // a mod's colour: its number is only known once the mods are loaded
                    if (const gp::PaletteDef *pd = uiDb->palette(si.appearanceId); pd && gp::modPalettes().count(pd->number))
                        slotColour[i] = pd->number;
                if (si.full)
                    std::printf("slot %d: %s, colour 0x%x%s%s\n", i, si.name.c_str(), slotColour[i], si.appearanceId.empty() ? "" : " of ", si.appearanceId.c_str());
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
        gp::Settings opts; // the options screen works on a copy and saves it when you leave
        int optSel = 0, optScroll = 0;
        int optCapture = -1; // the key binding waiting for a key, -1 = none
        bool optPressed = false;

        // The ROM's test mode (game/test_mode.hpp): the sequence on the logo, and the screens.
        namespace tmn = gp::testmode;
        tmn::EntrySequence testEntry;
        TmScreen tmScreen = TmScreen::Menu;
        tmn::MenuList tmMenu;
        tmn::ButtonTest tmButton;
        tmn::SoundTest tmSound;
        int tmPress = -1, tmSongTicks = 0;
        unsigned tmChecksum = 0;
        auto tmEvent = [](SDL_Keycode k) -> int
        {
            switch (k)
            {
            case SDLK_LEFT:
                return tmn::Left;
            case SDLK_RIGHT:
                return tmn::Right;
            case SDLK_UP:
                return tmn::Up;
            case SDLK_DOWN:
                return tmn::Down;
            case SDLK_z:
                return tmn::Select;
            case SDLK_x:
                return tmn::Cancel;
            case SDLK_RETURN:
                return tmn::Menu;
            default:
                return -1;
            }
        };
        // SoundTestPlaySelected: every channel stops, then ids 0..0x5f play as effects at 0x7f and the rest are the nine songs.
        auto tmPlay = [&]
        {
            gp::audio().stopSfx();
            gp::audio().stopMusic();
            tmSongTicks = 0;
            tmSound.started();
            if (tmSound.isSong())
                gp::audio().playMusic(tmSound.songIndex());
            else
                gp::audio().playSfx(tmSound.id, tmn::kSoundVolume);
        };
        auto tmStart = [&]
        {
            tmScreen = TmScreen::Menu;
            tmMenu = {};
            tmButton = {};
            tmSound = {};
            tmPress = -1;
        };
        auto tmLeaveSound = [&]
        {
            gp::audio().stopSfx();
            gp::audio().stopMusic();
            gp::audio().playMusic(gp::rau::kMenuSong);
            tmScreen = TmScreen::Menu;
        };
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
            menu.pa = menu.a; // before this tick, for the smooth renderer
            menu.pb = menu.b;
            menu.pc = menu.c;
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
            // the engine system "frontend.start": a save slot is about to be played (a new pet's, or a loaded one); cancel or replace and the front end stays; a mod may change `slot`
            gp::SystemCtx sc;
            sc.set("slot", slot);
            feScripts.system("frontend.start", sc, [&]
                             {
                chosenSlot = std::clamp(int(sc.num("slot")), 0, 2);
                leaving = true;
                fadeOutTick = 0; });
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
                    { // a mod's colour (a species of a mod can come in them) is kept by its id: its number depends on the mods loaded
                        const gp::SpeciesDef *spd = uiDb->speciesByLegacy(chosen.species);
                        gp::saveColourIds(sd, chosen.colour, chosen.colour, spd ? spd->defaultPalette : 0x1df, [&](int n) -> gp::Id
                                          {
                                              for (const gp::PaletteDef *pd : uiDb->allPalettes())
                                                  if (pd->number == n)
                                                      return pd->id;
                                              return gp::Id();
                                          });
                    }
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

        auto smoothMenu = [&] // slide-in positions: part-way between the last two 30 Hz ticks while sliding, exact otherwise
        {
            const float al = fadeSkip > 0 ? 1.f : float(tickAcc);
            const bool sliding = !menu.ready;
            menu.fa = sliding ? feSmooth(menu.pa, menu.a, al, 4) : float(menu.a);
            menu.fb = sliding ? feSmooth(menu.pb, menu.b, al, 4) : float(menu.b);
            menu.fc = sliding ? feSmooth(menu.pc, menu.c, al, 4) : float(menu.c);
        };

        if (skipBoot)
            screen = Screen::Menu; // menu fields default to the final slide position
        const char *loadShot = std::getenv("GP_FE_LOAD_SHOT"); // debug: open on the load screen (slot GP_FE_LOAD_SLOT), save the 20th frame as a BMP, quit
        if (loadShot)
        {
            screen = Screen::Load;
            if (const char *sl = std::getenv("GP_FE_LOAD_SLOT"))
                loadCur = std::clamp(std::atoi(sl), 0, 3);
        }
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
                const SDL_Keycode k = gp::keys().canonical(e.key.keysym.scancode, e.key.keysym.sym); // a rebound key acts as its action's default
                const bool down = e.type == SDL_KEYDOWN, rep = e.key.repeat != 0;
                const bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                if (screen == Screen::Options && optCapture >= 0) // waiting for the new key of a binding: Escape gives up
                {
                    if (down && !rep)
                    {
                        if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                            optCapture = -1;
                        else if (gp::keys().bind(optCapture, e.key.keysym.scancode))
                        {
                            opts.keys = gp::keys().key;
                            optCapture = -1;
                            feSound(gp::fe::kSfxConfirm, 0x7f);
                        }
                    }
                    continue;
                }

                if (screen == Screen::Menu && down && !rep && !gp::debugMode() && debugCode.press(k))
                {
                    gp::debugMode() = true;
                    feSound(0x53, 200);
                    std::printf("debug mode on\n");
                }
                if (down && !rep)
                {
                    if (gp::debugMode() && k == SDLK_F1)
                        screen = Screen::Menu;
                    else if (gp::debugMode() && k == SDLK_F2)
                        screen = Screen::Name;
                    else if (gp::debugMode() && k == SDLK_F3)
                        screen = Screen::Browse;
                    else if (k == SDLK_x)
                    {
                        if (screen == Screen::Boot)
                        {
                            if (!testEntry.armed()) // (Cancel ends the test mode sequence)
                                screen = Screen::Menu; // debug skip, as before
                        }
                        else if (screen == Screen::Name || screen == Screen::Browse)
                            goMenu();
                        // the other screens handle Escape themselves below
                    }
                    else if (gp::debugMode() && k == SDLK_F4)
                    {
                        smoothCam = !smoothCam;
                        std::printf("smooth mode %s\n", smoothCam ? "on" : "off");
                    }
                    else if (gp::debugMode() && k == SDLK_F5)
                    {
                        app.placeMode = (app.placeMode + 1) % 3;
                        std::printf("placement mode %d\n", app.placeMode);
                    }
                    else if (gp::debugMode() && k == SDLK_F6)
                    {
                        app.swapRB = !app.swapRB;
                        app.clearCaches();
                        std::printf("swapRB %d\n", int(app.swapRB));
                    }
                    else if (gp::debugMode() && k == SDLK_F7)
                    {
                        species = (species + 1) % 4;
                        std::printf("species %d\n", species);
                    }
                    else if (gp::debugMode() && k == SDLK_F8)
                        showOutlines = !showOutlines;
                    else if (gp::debugMode() && k == SDLK_F9)
                    {
                        app.transMode = (app.transMode + 1) % 3;
                        app.clearCaches();
                        std::printf("layer transparency mode %d\n", app.transMode);
                    }
                }
                if (screen == Screen::Boot && down && !rep && bootPhase <= 1)
                {
                    const int ev = tmEvent(k);
                    if (testEntry.armed()) // the chime has gone: Up, Down, Menu, Cancel
                    {
                        if (testEntry.press(ev) == tmn::EntrySequence::Result::Entered)
                        {
                            tmStart();
                            setScreen(Screen::TestMode);
                        }
                        continue;
                    }
                    if (ev == tmn::Left || ev == tmn::Select) // the hold must not skip the logo
                        continue;
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
                            --menu.cursor, feSound(gp::fe::kSfxMove, 0x7f);
                        if (k == SDLK_DOWN && menu.cursor < 2)
                            ++menu.cursor, feSound(gp::fe::kSfxMove, 0x7f);
                        if (k == SDLK_z && !rep)
                            menu.pressed = true, feSound(gp::fe::kSfxConfirm, 0x7f); // ShowMainMenuScreen: 0x2d / 0x59 at volume 0x7f
                        const int d = shift ? 8 : 1;
                        bool changed = gp::debugMode();
                        if (!changed)
                            ;
                        else if (k == SDLK_q)
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
                        // the engine system "frontend.menu": the player chose on the title menu (choice "new", "load" or "options"); cancel or replace and nothing happens
                        gp::SystemCtx mc;
                        mc.set("choice", menu.cursor == 0 ? "new" : menu.cursor == 1 ? "load" : "options");
                        const int cursor = menu.cursor;
                        feScripts.system("frontend.menu", mc, [&]
                                         {
                        if (cursor == 0)
                        {
                            name = NameEntry();
                            setScreen(Screen::Intro);
                        }
                        else if (cursor == 1)
                        {
                            loadStart();
                            setScreenNoFade(Screen::Load);
                        }
                        else if (cursor == 2)
                        {
                            opts = gp::loadSettings();
                            optSel = 0;
                            optScroll = 0;
                            optCapture = -1;
                            opts.keys = gp::keys().key;
                            optPressed = false;
                            setScreenNoFade(Screen::Options);
                        } });
                    }
                }
                else if (screen == Screen::Intro && down && !rep && fadeTick >= 16)
                {
                    if (k == SDLK_x)
                        goMenu(); // event 5 -> state 4
                    else
                    {
                        starter.start(rom, randBelow, makeStarterPool(*uiDb, gp::loadProfileFile()));
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
                                feSound(gp::fe::kSfxMove, 0x7f);
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
                    moved:
                        feSound(gp::fe::kSfxMove, 0x7f);
                    }
                    else if (k == SDLK_x)
                        setScreenNoFade(Screen::Menu); // event 5 -> state 4
                    else if (k == SDLK_z)
                    {
                        feSound(gp::fe::kSfxConfirm, 0x7f);
                        if (loadCur == 3)
                            setScreenNoFade(Screen::Menu);
                        else if (slotFull[loadCur])
                        {
                            loadPicked = true;
                            loadPickedAt = SDL_GetTicks();
                        }
                    }
                }
                else if (screen == Screen::Options && down && !optPressed)
                {
                    // Up and down move (wrapping, and the list scrolls to follow); left and right step a volume, a choice or a
                    // switch; Z flips a switch, cycles a choice and presses Done; X leaves. The settings are saved on the way out
                    // (SaveSettingsToSlot). The sound and the window follow the choices at once.
                    auto leave = [&](bool pressed)
                    {
                        opts.keys = gp::keys().key;
                        gp::saveSettings(opts);
                        optPressed = pressed;
                        goMenu();
                    };
                    auto applyAudio = [&]
                    {
                        gp::audio().setVolume(opts.volume);
                        gp::audio().setMusicVolume(opts.musicVolume);
                        gp::audio().setSfxVolume(opts.sfxVolume);
                    };
                    auto applyWindow = [&]
                    {
                        smoothCam = opts.interpolate;
                        gp::applyDisplay(win, ren, gp::DisplaySettings{opts.scale, opts.perfectScale, opts.windowMode, opts.aspect});
                    };
                    // true = Z/Right (forward), false = Left
                    auto change = [&](bool forward, bool byZ)
                    {
                        switch (optSel)
                        {
                        case gp::kOptTutorials:
                            opts.tutorials = !opts.tutorials;
                            break;
                        case gp::kOptInterpolate:
                            opts.interpolate = !opts.interpolate;
                            applyWindow();
                            break;
                        case gp::kOptScaleMode:
                            opts.perfectScale = !opts.perfectScale;
                            applyWindow();
                            break;
                        case gp::kOptScale:
                            opts.scale = byZ && opts.scale == 8 ? 1 : gp::stepScale(opts.scale, forward);
                            applyWindow();
                            break;
                        case gp::kOptWindowMode:
                            opts.windowMode = gp::stepWindowMode(opts.windowMode, forward);
                            applyWindow();
                            break;
                        case gp::kOptAspect:
                            opts.aspect = 1 - opts.aspect;
                            applyWindow();
                            break;
                        case gp::kOptRetro:
                            opts.retroAudio = !opts.retroAudio;
                            gp::audio().setRetro(opts.retroAudio);
                            feSound(gp::fe::kSfxConfirm, 0x7f); // hear the difference
                            break;
                        case gp::kOptLive:
                            opts.liveMusic = !opts.liveMusic;
                            gp::audio().setLive(opts.liveMusic);
                            break;
                        default:
                            break;
                        }
                    };
                    int *slider = optSel == gp::kOptMusic ? &opts.musicVolume : optSel == gp::kOptVolume ? &opts.volume
                                                                            : optSel == gp::kOptSfx      ? &opts.sfxVolume
                                                                                                         : nullptr;
                    if (k == SDLK_UP && !rep)
                        optSel = gp::optionsMove(optSel, false, gp::debugMode() ? gp::kOptRowsDebug : gp::kOptRows), feSound(gp::fe::kSfxMove, 0x7f);
                    else if (k == SDLK_DOWN && !rep)
                        optSel = gp::optionsMove(optSel, true, gp::debugMode() ? gp::kOptRowsDebug : gp::kOptRows), feSound(gp::fe::kSfxMove, 0x7f);
                    else if (k == SDLK_x && !rep)
                        leave(false);
                    else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && slider)
                    {
                        const int before = *slider;
                        *slider = gp::volumeStep(*slider, k == SDLK_RIGHT); // held keys repeat
                        applyAudio();
                        if (optSel != gp::kOptMusic && *slider != before) // hear the level (the music is playing already)
                            feSound(gp::fe::kSfxConfirm, 0x7f);
                    }
                    else if (k == SDLK_LEFT && gp::isKeyRow(optSel) && !rep) // Left on a binding: back to its default key
                    {
                        gp::keys().reset(gp::keyRowAction(optSel));
                        opts.keys = gp::keys().key;
                    }
                    else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && !rep)
                        change(k == SDLK_RIGHT, false);
                    else if (k == SDLK_z && !rep)
                    {
                        feSound(gp::fe::kSfxConfirm, 0x7f); // ShowOptionsMenuScreen
                        if (optSel == gp::kOptDone)
                            leave(true);
                        else if (optSel == gp::kOptTestMode && gp::debugMode()) // the ROM's test mode, from the debug unlock
                        {
                            opts.keys = gp::keys().key;
                            gp::saveSettings(opts);
                            tmStart();
                            setScreen(Screen::TestMode);
                        }
                        else if (gp::isKeyRow(optSel))
                            optCapture = gp::keyRowAction(optSel); // the next key pressed is the new binding
                        else
                            change(true, true);
                    }
                    optScroll = gp::optionsScroll(optSel, optScroll, gp::debugMode() ? gp::kOptRowsDebug : gp::kOptRows);
                    applyAudio();
                }
                else if (screen == Screen::TestMode && down && !rep)
                {
                    const int ev = tmEvent(k);
                    switch (tmScreen)
                    {
                    case TmScreen::Menu:
                        if (ev == tmn::Up)
                            tmMenu.move(-1);
                        else if (ev == tmn::Down)
                            tmMenu.move(1);
                        else if (ev == tmn::Select)
                            switch (tmMenu.sel)
                            {
                            case tmn::RowButton:
                                tmButton = {};
                                tmPress = -1;
                                tmScreen = TmScreen::Button;
                                break;
                            case tmn::RowSound:
                                tmSound = {};
                                tmScreen = TmScreen::Sound;
                                tmPlay(); // SoundTestPlaySelected(0)
                                break;
                            case tmn::RowMemory:
                            {
                                unsigned sum = 0; // TestEeprom: the 16-bit sum of the ROM words from 0x4000 up
                                for (uint32_t a = 0x4000; a < rom.sizeWords() && a < 0x400000u; ++a)
                                    sum += rom.u16(a);
                                tmChecksum = sum & 0xffff;
                                tmScreen = TmScreen::Memory;
                                break;
                            }
                            case tmn::RowVideo:
                                tmScreen = TmScreen::Video1;
                                break;
                            case tmn::RowHandheld:
                                tmScreen = TmScreen::Handheld;
                                break;
                            case tmn::RowCarrier:
                                tmScreen = TmScreen::Carrier;
                                break;
                            default: // EXIT TEST MODE (the ROM powers down)
                                goMenu();
                                break;
                            }
                        break;
                    case TmScreen::Button:
                        if (tmPress < 0)
                            tmPress = ev;
                        break;
                    case TmScreen::Sound:
                        if (ev == tmn::Left)
                            tmSound.prev(), tmSound.playAll = false, tmPlay();
                        else if (ev == tmn::Right)
                            tmSound.next(), tmSound.playAll = false, tmPlay();
                        else if (ev == tmn::Up)
                            tmSound.playAll = false, tmPlay();
                        else if (ev == tmn::Down)
                        {
                            if (tmSound.togglePlayAll())
                                tmPlay();
                        }
                        else if (ev == tmn::Cancel)
                            tmLeaveSound();
                        break;
                    case TmScreen::Memory:
                    case TmScreen::Carrier:
                    case TmScreen::Handheld:
                        if (ev == tmn::Cancel)
                            tmScreen = TmScreen::Menu;
                        break;
                    case TmScreen::Video1:
                        tmScreen = TmScreen::Video2; // any button
                        break;
                    case TmScreen::Video2:
                        tmScreen = TmScreen::Menu;
                        break;
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
                    // HandleNameEntryInput: every direction clicks 0x2d (0xaf); Z clicks 0x59 (0x7f), except Back or Space with
                    // nothing typed and OK on a blank name, which do nothing
                    if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)
                        feSound(0x2d, 0xaf);
                    else if (k == SDLK_z && !rep && (name.onGrid || (name.col < 2 ? !name.name.empty() : NameEntry::nonBlank(name.name))))
                        feSound(0x59, 0x7f);
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
            gp::useUiScreen(ren); // 16:9: the 320 x 240 screens sit in the middle
            gp::clearFrame(ren);
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
                    { // HandleTestModeEntrySequence: Left + Select held for 3 s, a chime, then Up, Down, Menu, Cancel; the logo waits for it
                        const Uint8 *ks = SDL_GetKeyboardState(nullptr);
                        const bool both = gp::keys().held(ks, gp::Action::Left) && gp::keys().held(ks, gp::Action::Select);
                        for (int i = 0; i < ticks; ++i)
                            if (testEntry.tick(both) == tmn::EntrySequence::Result::Chime)
                                feSound(0x59, 0xbe);
                        if (testEntry.armed() && now - bootT0 >= kLogoMs - 100)
                            bootT0 = now - (kLogoMs - 100);
                    }
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
                smoothMenu();
                drawMainMenu(gfx, menu);
                if (menu.ready)
                    screen = Screen::Menu; // plain assignment: no new fade
                break;
            }
            case Screen::Menu:
                for (int i = 0; i < gt && !menu.ready; ++i) // replayed slide-in after the pet flow
                    stepSlide();
                app.drawBackground(2, 0x5c5);
                smoothMenu();
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
                starter.alpha = smoothCam ? float(tickAcc) : 1.f; // how far through the current tick we are
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
                if (loadCur < 3 && slotFull[loadCur] && slotSpecies[loadCur] >= 0 && gp::knownSpecies(slotSpecies[loadCur]))
                    app.drawLoadPicture(gp::artOf(slotSpecies[loadCur]), slotColour[loadCur]);
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
            case Screen::Options:
                app.drawBackground(2, 0x5c5);
                drawOptions(gfx, optSel, optPressed, opts, optScroll, optCapture, gp::debugMode() ? gp::kOptRowsDebug : gp::kOptRows);
                break;
            case Screen::TestMode:
            {
                if (tmScreen == TmScreen::Video1 || tmScreen == TmScreen::Video2)
                {
                    app.drawBackground(tmScreen == TmScreen::Video1 ? 0x21 : 0x22, 0x5ea); // RunVideoTestLoop
                    break;
                }
                app.drawBackground(0, 0x5c3);
                switch (tmScreen)
                {
                case TmScreen::Menu:
                    drawTestMenu(gfx, tmMenu.sel);
                    break;
                case TmScreen::Button:
                {
                    const Uint8 *ks = SDL_GetKeyboardState(nullptr);
                    const bool cancelDown = gp::keys().held(ks, gp::Action::Cancel);
                    for (int i = 0; i < ticks; ++i)
                    {
                        tmButton.tick(i == 0 ? tmPress : -1, cancelDown);
                        if (i == 0)
                            tmPress = -1;
                    }
                    if (tmButton.failSound)
                    {
                        tmButton.failSound = false;
                        feSound(0x59, 0x7f);
                    }
                    if (tmButton.exit)
                        tmScreen = TmScreen::Menu;
                    drawButtonTest(gfx, tmButton);
                    break;
                }
                case TmScreen::Sound:
                {
                    for (int i = 0; i < ticks; ++i)
                    {
                        // The songs loop here, so one counts as playing for 30 s; sound 0 starts over when it ends.
                        tmSongTicks += tmSound.isSong();
                        const bool playing = tmSound.isSong() ? tmSongTicks < 30 * tmn::kTicksPerSecond : gp::audio().sfxActive(tmSound.id);
                        if (tmSound.id == 0 && !playing)
                            tmPlay();
                        else if (tmSound.tick(playing))
                        {
                            tmSound.next();
                            tmPlay();
                        }
                    }
                    drawSoundTest(gfx, tmSound.id);
                    break;
                }
                case TmScreen::Memory:
                    drawTestText(gfx, 0x10, 0x10, "EEPROM TEST:");
                    drawTestText(gfx, 0xb4, 0x10, "PASS"); // there is no EEPROM here: the saves are files
                    drawTestText(gfx, 0x10, 0x30, "ROM CHECKSUM:");
                    drawTestText(gfx, 0xb4, 0x30, std::to_string(tmChecksum));
                    drawTestText(gfx, 0x10, 0x60, "PRESS SELECT TO RESTART THE TEST.");
                    drawTestText(gfx, 0x10, 0x90, "PRESS CANCEL TO EXIT THIS TEST.");
                    break;
                case TmScreen::Carrier:
                    drawTestText(gfx, 0x10, 0x10, "NO PET CARRIER INSERTED");
                    drawTestText(gfx, 0x10, 0x90, "PRESS CANCEL TO EXIT THIS TEST.");
                    break;
                case TmScreen::Handheld:
                    drawTestText(gfx, 0x10, 0x30, "INSERT HANDHELD");
                    drawTestText(gfx, 0x10, 0x90, "PRESS CANCEL TO EXIT THIS TEST.");
                    break;
                default:
                    break;
                }
                break;
            }
            case Screen::House:
                app.drawBackground(2, 0x5c5);
                drawHouse(gfx, houseCur, slotName, slotFull);
                if (dlg.open())
                    drawConfirm(*dlg.box);
                break;
            case Screen::Name:
                app.drawBackground(0x1f, 0x5dd);
                drawNameEntry(gfx, name, chosen.species, SDL_GetTicks() < pressedUntil);
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
            // Game.hooks().onHud(g) and onPostDraw(g) draw over the front end's screens too, with everything a script can draw (script_draw.hpp: shapes, canvases, blits, meshes, "@frame",
            // sprites of mods and the game's pictures, text)
            feDrawer.run(feScripts.drawHud(""));
            gp::useWholeScreen(ren);
            feDrawer.frameDrawW = gp::logicalW();
            feDrawer.frameDrawH = 240;
            feDrawer.run(feScripts.drawPost(gp::logicalW()));
            if (showOutlines)
            {
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                for (const auto &t : gfx.texts)
                {
                    SDL_Rect r{t.x, t.y, std::min(t.w, int(t.s.size()) * 6), 10};
                    SDL_RenderDrawRect(ren, &r);
                }
            }
            // Fades are counted in 30 Hz ticks. In smooth mode the part-way-through-a-tick fraction is added, so the
            // black overlay ramps every frame instead of in 16 steps. (Off: frac is 0 and this is the old integer maths.)
            const float frac = (smoothCam && fadeSkip == 0) ? float(tickAcc) : 0.f;
            float alphaF = 0.f;
            if (hasPending || leaving)
                alphaF = 255.f * std::min(float(kFade), float(fadeOutTick) + frac) / float(kFade);
            else if (fadeTick < kFade)
                alphaF = 255.f * std::max(0.f, float(kFade) - (float(fadeTick) + frac)) / float(kFade);
            if (screen == Screen::Boot && bootPhase == 1)
                alphaF = 255.f * std::min(16.f, float(outTick) + frac) / 16.f;
            const int alpha = int(alphaF);
            gp::useWholeScreen(ren); // the fade covers the whole window
            if (alpha > 0)
            {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, uint8_t(alpha));
                SDL_RenderFillRect(ren, nullptr);
            }
            if (loadShot && frame == 20)
            {
                int w = 0, h = 0;
                SDL_GetRendererOutputSize(ren, &w, &h);
                if (SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888))
                {
                    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
                    SDL_SaveBMP(surf, loadShot);
                    SDL_FreeSurface(surf);
                }
                run = false;
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
        std::fprintf(stderr, "usage: frontend_demo rom.u7 [--frames N] [--smooth]\n");
        return 1;
    }
    int maxFrames = -1;
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--smooth"))
            smoothCam = true;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            maxFrames = std::atoi(argv[++i]);
    }
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