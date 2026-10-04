// walk.cpp - walk around an area with the arrow keys.
// Usage: walk <rom> <area> [--collision]     (--collision tints blocked ground red)
// Build: see build line in the chat / build.bat
#include <SDL2/SDL.h>
#include "room.hpp"
#include "render.hpp"
#include "player.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Your IWorld is an "interface": three questions the movement code asks about the world.
// This class answers them using your loaded data.
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

// Tints pixels that objects block (blue), like overlayGroundCollision does for the ground (red).
static void overlayObjectCollision(gp::Image &img, gp::ObjectCollision &objects, const gp::Area &area)
{
    std::vector<int> all; // pretend every object is visible
    for (size_t i = 0; i < area.objects.size(); ++i)
        all.push_back(int(i));
    objects.setVisible(all);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (objects.blocks(x, y))
            {
                uint8_t *p = &img.rgba[(size_t(y) * img.w + x) * 4];
                p[0] = uint8_t(p[0] * 6 / 10);
                p[1] = uint8_t(p[1] * 6 / 10);
                p[2] = uint8_t((p[2] * 6 + 255 * 4) / 10);
            }
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
        // ---- 1. LOAD (same calls room_dump uses) ----
        gp::Rom rom(argv[1]);
        const int id = std::atoi(argv[2]);
        gp::Area area;
        if (!gp::readArea(rom, id, area))
        {
            std::printf("area %d is invalid\n", id);
            return 1;
        }
        gp::RoomLink link;
        if (!gp::readRoomLink(rom, id, link))
        {
            std::printf("area %d not in room table\n", id);
            return 1;
        }

        gp::GroundCollision gc(rom, area, link.collisionFrame);
        gp::ProgressFlags flags = gp::ProgressFlags::allSet(); // same assumption replay_moves makes
        gp::ObjectCollision objects(rom, area, flags, 0);      // 0 = y offset, same as GroundCollision's default
        const gp::Room room{gp::areaWidthPx(area.tilesW), gp::areaHeightPx(area.tilesH),
                            gp::kMarginLeft, gp::kMarginRight, gp::kMarginTop, gp::kMarginBottom};
        AreaWorld world(gc, objects, room);

        // ---- 2. RENDER ONCE (same calls as render.hpp's header comment describes) ----
        const gp::TileSheet sheet = gp::TileSheet::fromFrame(rom, link.tileFrame);
        gp::Palette pal;
        const bool hasPal = gp::readPalette(rom, link.paletteId, pal);
        gp::Image img = gp::renderBackground(rom, area, sheet, hasPal ? &pal : nullptr);
        gp::drawObjects(img, rom, area);
        if (showCollision)
            gp::overlayGroundCollision(img, gc);

        // ---- start position: the free spot closest to the middle of the room ----
        gp::Point pos{room.width / 2, room.height / 2};
        long best = -1;
        for (int y = 0; y < room.height; y += 4)
            for (int x = 0; x < room.width; x += 4)
            {
                if (!world.tileWalkable(x, y) || !world.inRoom(x, y))
                    continue;
                const long dx = x - room.width / 2, dy = y - room.height / 2;
                const long d = dx * dx + dy * dy;
                if (best < 0 || d < best)
                {
                    best = d;
                    pos = {x, y};
                }
            }
        if (best < 0)
            std::printf("warning: no walkable spot found, starting at the center\n");

        if (showCollision)
            overlayObjectCollision(img, objects, area);
        objects.updateVisible(pos.x - 160, pos.y - 120); // restore the real visible list for walking
        // ---- 3. WINDOW ----
        const int W = 640, H = 480;
        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window *win = SDL_CreateWindow("walk", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
        SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
        SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.w * 4);

        bool running = true;
        while (running)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                if (e.type == SDL_QUIT)
                    running = false;

            // ---- 4. INPUT -> your tested code ----
            const Uint8 *k = SDL_GetKeyboardState(nullptr);
            const uint8_t dirs = gp::pollHeld(k[SDL_SCANCODE_LEFT], k[SDL_SCANCODE_RIGHT],
                                              k[SDL_SCANCODE_UP], k[SDL_SCANCODE_DOWN]);
            if (dirs)
                pos = gp::stepPlayer(world, pos, dirs).pos;
            // The game builds its visible-object list from the camera, and the list used for a move
            // comes from the previous tick, so update it after moving, ready for the next tick.
            objects.updateVisible(pos.x - 160, pos.y - 120); // TODO: real camera formula

            // ---- 5. DRAW, camera centered on player ----
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_Rect dst{W / 2 - pos.x, H / 2 - pos.y, img.w, img.h};
            SDL_RenderCopy(ren, tex, nullptr, &dst);
            SDL_Rect me{W / 2 - 3, H / 2 - 3, 6, 6}; // marker on the player's position
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
            SDL_RenderFillRect(ren, &me);
            SDL_RenderPresent(ren);
            SDL_Delay(16); // ~60 ticks/sec; the game's real rate is unverified
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