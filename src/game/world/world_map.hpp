// world_map.hpp - the Giga Island map: the sign in Gigatown (area 0, object 0x38) and the island map item (type 17, item 1).
//
// Both open a type-6 dialogue (InitDialogueBoxStruct with the world-map picture): RenderActiveDialogueWindow draws the full
// screen map (tile 0x47b, palette 0x5c2) and a "you are here" marker (tile 0x47a, palette 0x28c) at
// SetDialogueBoxPositionForStage (0x047e59): the pet's world position divided by 32, plus an offset per area. Any button
// closes it. An area that is not in the table keeps the marker where it was last (it is not drawn from fresh state here).
#pragma once
#include <optional>

namespace gp
{

    constexpr int kMapTile = 0x47b, kMapPalette = 0x5c2, kMapMarkerTile = 0x47a, kMapMarkerPalette = 0x28c;

    struct MapPoint
    {
        int x, y;
    };

    // The marker for a pet standing at world pixel (x, y) in this area; nothing for an area the map does not cover.
    constexpr std::optional<MapPoint> mapMarker(int area, int worldX, int worldY)
    {
        struct Offset
        {
            int area, x, y;
        };
        constexpr Offset kOffsets[] = {
            {0, 0x34, 0x55}, {1, 0xa7, 0x55}, {2, 0x88, 0x93}, {3, 0xd2, 0x87}, {4, 0x14, 0xa3},
            {5, 0xcb, 0x23}, {6, 0x34, 0x23}, {7, 0x34, 0x86}, {8, 0xf3, 0x12}, {9, 0x85, 0x2f},
        };
        for (const Offset &o : kOffsets)
            if (o.area == area)
                return MapPoint{(worldX >> 5) + o.x, (worldY >> 5) + o.y};
        return std::nullopt;
    }

} // namespace gp
