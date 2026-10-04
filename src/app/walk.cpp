// walk.cpp - walk around an area with the arrow keys, and take story exits between areas.
// Usage: walk <rom> <area> [--collision]     (--collision tints blocked ground red, objects blue)
#include <SDL2/SDL.h>
#include "room.hpp"
#include "render.hpp"
#include "player.hpp"
#include "exits.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

class AreaWorld : public gp::IWorld
{
public:
    AreaWorld(const gp::GroundCollision &gc, gp::ObjectCollision &obj, const gp::Room &room)
        : gc_(gc), obj_(obj), room_(room) {}
    bool tileWalkable(int x, int y) const override { return gc_.walkable(x, y); }
    bool inRoom(int x, int y) const override { return gp::inRoom(room_, x, y); }
    bool objectBlocks(int x, int y) const override { return obj_.blocks(x, y); }

private:
    const gp::GroundCollision &gc_;
    gp::ObjectCollision &obj_;
    gp::Room room_;
};

static void overlayObjectCollision(gp::Image &img, const gp::Rom &rom, const gp::Area &area,
                                   const gp::ProgressFlags &flags)
{
    gp::ObjectCollision scratch(rom, area, flags, 0);
    for (size_t i = 0; i < area.objects.size(); ++i)
    {
        const gp::ObjectRec &o = area.objects[i];
        const gp::TypeRec t = gp::readTypeRec(rom, o.type);
        if (t.overlayId == 0xffff)
            continue;
        gp::SpriteHeader h;
        if (!gp::readSpriteHeader(rom, t.overlayId, h))
            continue;
        scratch.setVisible({int(i)});
        const int left = o.x - h.anchorX, top = o.y - h.anchorY;
        for (int y = std::max(top, 0); y < std::min(top + h.h, img.h); ++y)
            for (int x = std::max(left, 0); x < std::min(left + h.w, img.w); ++x)
                if (scratch.blocks(x, y))
                {
                    uint8_t *p = &img.rgba[(size_t(y) * img.w + x) * 4];
                    p[0] = uint8_t(p[0] * 6 / 10);
                    p[1] = uint8_t(p[1] * 6 / 10);
                    p[2] = uint8_t((p[2] * 6 + 255 * 4) / 10);
                }
    }
}

static int clampCam(int raw, int world, int view)
{
    return std::min(std::max(raw, 0), world - view); // same order as the game: low clamp, then high clamp
}

// ---- transitions ----
struct Transition
{
    int tileFrame, paletteId, collisionFrame, flags;
    gp::Point spawn;
};

// from = area you are leaving, to = destination. hardcoded = the exit has flag1a9d == 0.
static bool findTransition(const gp::Rom &rom, int from, int to, bool hardcoded, Transition &out)
{
    for (int i = 0; i < gp::kRoomTableCount; ++i)
    {
        const uint32_t a = gp::kRoomTable + uint32_t(i) * 8;
        if (rom.u16(a) != from || rom.u16(a + 1) != to)
            continue;
        out = {rom.u16(a + 2), rom.u16(a + 3), rom.u16(a + 4), rom.u16(a + 7), {rom.u16(a + 5), rom.u16(a + 6)}};
        if (hardcoded && from == 6 && to == 0)
            out.spawn = {0x84c, 0xb9};
        if (hardcoded && from == 0 && to == 6)
            out.spawn = {0x893, 0x34c};
        return true;
    }
    return false;
}

// Everything one area needs. Keep it on the heap and never copy it:
// the collision objects hold references to `area` and `flags` inside it.
struct Loaded
{
    gp::Area area;
    gp::ProgressFlags flags = gp::ProgressFlags::allSet(); // fresh per load, like ResetSpriteSlots
    std::unique_ptr<gp::GroundCollision> ground;
    std::unique_ptr<gp::ObjectCollision> objects;
    gp::Image img;
    gp::Room room;
};

static std::unique_ptr<Loaded> loadArea(const gp::Rom &rom, int id, const Transition &tr, bool showCollision)
{
    auto L = std::make_unique<Loaded>();
    if (!gp::readArea(rom, id, L->area))
        return nullptr;
    L->ground = std::make_unique<gp::GroundCollision>(rom, L->area, tr.collisionFrame);
    L->objects = std::make_unique<gp::ObjectCollision>(rom, L->area, L->flags, 0);
    L->room = {gp::areaWidthPx(L->area.tilesW), gp::areaHeightPx(L->area.tilesH),
               gp::kMarginLeft, gp::kMarginRight, gp::kMarginTop, gp::kMarginBottom};
    const gp::TileSheet sheet = gp::TileSheet::fromFrame(rom, tr.tileFrame);
    gp::Palette pal;
    const bool hasPal = gp::readPalette(rom, tr.paletteId, pal);
    L->img = gp::renderBackground(rom, L->area, sheet, hasPal ? &pal : nullptr);
    gp::drawObjects(L->img, rom, L->area);
    if (showCollision)
    {
        gp::overlayGroundCollision(L->img, *L->ground);
        overlayObjectCollision(L->img, rom, L->area, L->flags);
    }
    return L;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::printf("usage: walk <rom> <area> [--collision]\n");
        return 1;
    }
    const bool showCollision = argc > 3 && std::strcmp(argv[3], "--collision") == 0;

    try
    {
        // ---- 1. LOAD ----
        gp::Rom rom(argv[1]);
        const int id = std::atoi(argv[2]);
        gp::RoomLink link;
        if (!gp::readRoomLink(rom, id, link))
        {
            std::printf("area %d not in room table\n", id);
            return 1;
        }
        Transition first{link.tileFrame, link.paletteId, link.collisionFrame, 0, {0, 0}};
        std::unique_ptr<Loaded> cur = loadArea(rom, id, first, showCollision);
        if (!cur)
        {
            std::printf("area %d is invalid\n", id);
            return 1;
        }
        std::unique_ptr<AreaWorld> world = std::make_unique<AreaWorld>(*cur->ground, *cur->objects, cur->room);

        // ---- start position: the free spot closest to the middle of the room ----
        gp::Point pos{cur->room.width / 2, cur->room.height / 2};
        long best = -1;
        for (int y = 0; y < cur->room.height; y += 4)
            for (int x = 0; x < cur->room.width; x += 4)
            {
                if (!world->tileWalkable(x, y) || !world->inRoom(x, y))
                    continue;
                const long dx = x - cur->room.width / 2, dy = y - cur->room.height / 2;
                const long d = dx * dx + dy * dy;
                if (best < 0 || d < best)
                {
                    best = d;
                    pos = {x, y};
                }
            }
        if (best < 0)
            std::printf("warning: no walkable spot found, starting at the center\n");

        // ---- 3. WINDOW ----
        const int W = 426, H = 240;
        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window *win = SDL_CreateWindow("walk", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 2, H * 2, 0);
        SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        SDL_RenderSetLogicalSize(ren, W, H);
        SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cur->img.w, cur->img.h);
        SDL_UpdateTexture(tex, nullptr, cur->img.rgba.data(), cur->img.w * 4);

        // Prevents bouncing straight back through the door you arrived by:
        // exits stay disarmed until the player has stood outside every exit's reach once.
        bool exitsArmed = false;

        bool prevZ = false;

        // Loads `to` via the room table and moves the player there. Returns true on success.
        auto goTo = [&](int to, bool hardcoded) -> bool
        {
            Transition tr;
            if (!findTransition(rom, cur->area.id, to, hardcoded, tr))
            {
                std::printf("no room-table row for %d -> %d\n", cur->area.id, to);
                return false;
            }
            auto next = loadArea(rom, to, tr, showCollision);
            if (!next)
            {
                std::printf("area %d could not be loaded\n", to);
                return false;
            }
            std::printf("area %d -> %d, spawn (%d,%d)\n", cur->area.id, to, tr.spawn.x, tr.spawn.y);
            cur = std::move(next);
            world = std::make_unique<AreaWorld>(*cur->ground, *cur->objects, cur->room);
            pos = tr.spawn;
            SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cur->img.w, cur->img.h);
            SDL_UpdateTexture(tex, nullptr, cur->img.rgba.data(), cur->img.w * 4);
            cur->objects->updateVisible(clampCam(pos.x - W / 2, cur->img.w, W),
                                        clampCam(pos.y - H / 2, cur->img.h, H));
            exitsArmed = false;
            return true;
        };

        bool running = true;
        const double kTickHz = 120.0; // verified: the game ticks at 30 Hz
        const Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 prev = SDL_GetPerformanceCounter();
        double acc = 0;
        while (running)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                if (e.type == SDL_QUIT)
                    running = false;

            const Uint64 now = SDL_GetPerformanceCounter();
            acc += double(now - prev) / double(freq);
            prev = now;
            if (acc > 0.25)
                acc = 0.25;

            while (acc >= 1.0 / kTickHz)
            {
                acc -= 1.0 / kTickHz;
                const Uint8 *k = SDL_GetKeyboardState(nullptr);
                const uint8_t dirs = gp::pollHeld(k[SDL_SCANCODE_LEFT], k[SDL_SCANCODE_RIGHT],
                                                  k[SDL_SCANCODE_UP], k[SDL_SCANCODE_DOWN]);
                if (dirs)
                    pos = gp::stepPlayer(*world, pos, dirs).pos;

                // ---- exit trigger ----
                const bool zDown = k[SDL_SCANCODE_Z];
                const bool zPressed = zDown && !prevZ; // only the tick where Z goes down
                prevZ = zDown;

                const int nearest = gp::findNearestInteractable(cur->area, cur->objects->visible(),
                                                                gp::ProgressFlags::allClear(), pos.x, pos.y);
                const gp::InteractExit *ie = (zPressed && nearest >= 0) ? gp::findInteractExit(cur->area.id, nearest) : nullptr;
                const gp::StoryExit *ex = nearest >= 0 ? gp::findStoryExit(cur->area.id, nearest) : nullptr;

                if (ie)
                {
                    goTo(ie->to, false); // interior doors: Z while inside the reach box
                }
                else if (!ex)
                {
                    exitsArmed = true;
                }
                else if (exitsArmed)
                {
                    if (!goTo(ex->spawnReason, ex->flag1a9d == 0))
                        exitsArmed = false; // don't retry every tick
                }
            }

            const int camX = clampCam(pos.x - W / 2, cur->img.w, W);
            const int camY = clampCam(pos.y - H / 2, cur->img.h, H);
            cur->objects->updateVisible(camX, camY);

            // ---- 5. DRAW ----
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_Rect dst{-camX, -camY, cur->img.w, cur->img.h};
            SDL_Rect me{pos.x - camX - 3, pos.y - camY - 3, 6, 6};
            SDL_RenderCopy(ren, tex, nullptr, &dst);
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
            SDL_RenderFillRect(ren, &me);
            SDL_RenderPresent(ren);
        }
        SDL_Quit();
    }
    catch (const std::exception &e)
    {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}