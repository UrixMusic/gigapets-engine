// room_render.hpp - the background of a home or hotel room as one image: the wall layer, then the floor layer on top.
// Each layer is a 36 x 8 tilemap rendered with the same tileset code the overworld uses (render_content.hpp).
#pragma once
#include "../content/db.hpp"
#include "../game/room_scene.hpp"
#include "../loaders/import.hpp"
#include "render_content.hpp"

namespace gp
{

    // Imports the tilesets every room theme can use. Do this at start-up, while the db may still change:
    // building a room later only reads it.
    inline void importRoomTilesets(const Rom &rom, ContentDB &db)
    {
        for (int i = 0; i < kRoomWallThemeCount; ++i)
        {
            const SceneLayer l = roomWallTheme(rom, i);
            importTileset(rom, db, l.frame, l.palette);
        }
        for (int i = 0; i < kRoomFloorThemeCount; ++i)
        {
            const SceneLayer l = roomFloorTheme(rom, i);
            importTileset(rom, db, l.frame, l.palette);
        }
    }

    inline Image renderRoomLayer(const Rom &rom, const ContentDB &db, const SceneLayer &l)
    {
        char buf[48];
        std::snprintf(buf, sizeof buf, "base:tileset/%d@%x", l.frame, l.palette);
        AreaDef a; // a one-off area, just to reuse renderBackground
        a.tilesW = kRoomWidthTiles;
        a.tilesH = kRoomHeightTiles;
        a.tileset = buf;
        a.tiles.resize(size_t(a.tilesW) * a.tilesH);
        for (size_t k = 0; k < a.tiles.size(); ++k)
            a.tiles[k] = rom.u16(l.map + uint32_t(k));
        Image img = renderBackground(db, a);
        // Palette entry 0 of these background palettes is 0x7c1f, the hardware's transparency key (it renders as pure
        // magenta). The empty tiles of a layer are all that colour, so make it see-through, or the floor layer would
        // paint over the wall. (The overworld tilesets don't use the key, so renderBackground leaves it alone.)
        for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
            if (img.rgba[i] == 255 && img.rgba[i + 1] == 0 && img.rgba[i + 2] == 255)
                img.rgba[i + 3] = 0;
        return img;
    }

    // Wall behind, floor in front (blit skips transparent pixels).
    inline Image renderRoom(const Rom &rom, const ContentDB &db, const RoomTheme &t)
    {
        Image img = renderRoomLayer(rom, db, t.wall);
        const Image floor = renderRoomLayer(rom, db, t.floor);
        blit(img, floor.rgba.data(), floor.w, floor.h, 0, 0);
        return img;
    }

} // namespace gp
