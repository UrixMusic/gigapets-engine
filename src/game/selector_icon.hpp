// selector_icon.hpp - the bobbing arrow over whatever the action button would act on.
//
// InitMapMarkerQueue (0x04c8e4) starts three shared looping animations at 18 fps (palette 0x28c):
//   0x11: the arrow over an NPC, and over a ground item outdoors   (tip at the owner point, so it sits above)
//   0x12: the arrow over a ground item indoors                      (hangs from the owner point)
//   0x10: the ring over a story object                              (centred on its anchor)
// RenderRoomObjects (0x04c9a3) then draws ONE of them, last, on top of everything, for the current target:
//   1. a ground item in prompt range (g_promptEventIndex);
//   2. else the nearest interactable object, unless it is one of the objects with no marker (below);
//   3. else the selected NPC (the one the pet can talk to, FindEligibleNPCForEncounter).
// Sprite_RenderTile (0x04dd8d) draws it at owner + the frame's own offsets, like every other animation here.
#pragma once
#include <cstdint>

namespace gp
{

    constexpr int kMarkerArrowAnim = 0x11, kMarkerIndoorItemAnim = 0x12, kMarkerObjectAnim = 0x10;
    constexpr int kMarkerFps = 18;
    constexpr int kMarkerPalette = 0x28c;
    constexpr int kMarkerTallNpc = 0x36; // GetNPCSpriteWidthForLayout adds 0x14 for this pool entry

    enum class MarkerKind
    {
        None,
        Item,
        Object,
        Npc
    };

    struct MarkerPos
    {
        int x, y;
    };

    // The areas whose items get the tall arrow (0x11, above the item): the outdoor areas. Everywhere else it is 0x12.
    inline bool markerOutdoors(int area)
    {
        return area < 10 || area == 0xe || area == 0x15 || area == 0x1b || area == 0x1c || area == 0x1d || area == 0x22;
    }

    // Objects (original table index, area) that get no ring: the ones the pet shakes or digs, whose Z press spawns items
    // (the same list as the spawn reasons in HandlePetStageEvents). RenderRoomObjects falls through to the NPC arrow.
    inline bool objectHasNoMarker(int slot, int area)
    {
        struct P
        {
            int slot, area;
        };
        static constexpr P kNone[] = {
            {0x17, 0}, {0x32, 6}, {0x32, 0}, {0x26, 1}, {5, 3}, {0x5d, 1}, {0x3a, 3}, {0x1b, 2}, {0x49, 5}, {0x4a, 5}, {3, 1},
            {0xe6, 9}, {0x17, 1}, {2, 8}, {4, 5}, {0, 7}, {0x76, 0}, {6, 0}, {0x2b, 6}, {0x1b, 3}, {0, 0x22}};
        for (const P &p : kNone)
            if (p.slot == slot && p.area == area)
                return true;
        return false;
    }

    // What to mark. objectSlot is the object's original table index (-1 when there is no nearby object).
    inline MarkerKind chooseMarker(bool itemPrompt, bool hasObject, int objectSlot, int area, bool hasNpc)
    {
        if (itemPrompt)
            return MarkerKind::Item;
        if (!hasObject || objectHasNoMarker(objectSlot, area))
            return hasNpc ? MarkerKind::Npc : MarkerKind::None;
        return MarkerKind::Object;
    }

    // All positions are screen space (world minus camera). spriteH = the target sprite's current frame height.
    inline int itemMarkerAnim(int area) { return markerOutdoors(area) ? kMarkerArrowAnim : kMarkerIndoorItemAnim; }
    inline MarkerPos itemMarkerPos(int area, int x, int y, int spriteH)
    {
        return markerOutdoors(area) ? MarkerPos{x, y - spriteH} : MarkerPos{x, y};
    }
    inline MarkerPos npcMarkerPos(int poolIndex, int x, int y, int spriteH)
    {
        return {x, y - (spriteH + (poolIndex == kMarkerTallNpc ? 0x14 : 0))};
    }
    // The ring sits on the centre of the object's sprite. left/top = the sprite's top-left on screen.
    inline MarkerPos objectMarkerPos(int left, int top, int w, int h) { return {left + (w >> 1), top + (h >> 1)}; }
    // The ring is skipped when its centre is off the bottom or top of the screen (y + 0x1d must lie in 0..0x148).
    inline bool objectMarkerOnScreen(int y) { return y + 0x1d >= 0 && y + 0x1d <= 0x148; }

    // The three animations are shared and loop at 18 fps. gameTicks advances by 2 per 30 Hz tick; frame = ticks * rate / 60.
    inline int markerFrame(uint32_t gameTicks, int frames)
    {
        return frames > 0 ? int((gameTicks * uint32_t(kMarkerFps) / 60u) % uint32_t(frames)) : 0;
    }

} // namespace gp
