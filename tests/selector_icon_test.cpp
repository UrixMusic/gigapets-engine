// tests/selector_icon_test.cpp - the arrow over what the action button would act on (InitMapMarkerQueue 0x04c8e4 and the
// end of RenderRoomObjects 0x04c9a3).
#include <cstdio>
#include <initializer_list>
#include "../src/game/selector_icon.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    // ---- which animation is which (InitMapMarkerQueue starts 0x11, 0x12, 0x10 at 18 fps, palette 0x28c) ----
    check("ring 0x10, arrow 0x11, indoor arrow 0x12", kMarkerObjectAnim == 0x10 && kMarkerArrowAnim == 0x11 && kMarkerIndoorItemAnim == 0x12);
    check("18 fps, palette 0x28c", kMarkerFps == 18 && kMarkerPalette == 0x28c);

    // ---- priority: item > object > NPC ----
    check("an item beats everything", chooseMarker(true, true, 4, 1, true) == MarkerKind::Item);
    check("no item, an ordinary object: the ring", chooseMarker(false, true, 4, 1, true) == MarkerKind::Object);
    check("an ordinary object beats an NPC", chooseMarker(false, true, 1, 2, true) == MarkerKind::Object);
    check("no object: the NPC arrow", chooseMarker(false, false, -1, 1, true) == MarkerKind::Npc);
    check("nothing near: no marker", chooseMarker(false, false, -1, 1, false) == MarkerKind::None);

    // ---- the objects with no ring fall through to the NPC arrow ----
    check("a no-marker object with an NPC near: the NPC arrow", chooseMarker(false, true, 0x3a, 3, true) == MarkerKind::Npc);
    check("a no-marker object and no NPC: nothing", chooseMarker(false, true, 0x3a, 3, false) == MarkerKind::None);
    check("the same slot in another area is an ordinary object", chooseMarker(false, true, 0x3a, 4, true) == MarkerKind::Object);
    check("a no-marker object still loses to an item", chooseMarker(true, true, 0x3a, 3, true) == MarkerKind::Item);
    {
        struct P
        {
            int slot, area;
        };
        const P all[] = {{0x17, 0}, {0x32, 6}, {0x32, 0}, {0x26, 1}, {5, 3}, {0x5d, 1}, {0x3a, 3}, {0x1b, 2}, {0x49, 5}, {0x4a, 5}, {3, 1},
                         {0xe6, 9}, {0x17, 1}, {2, 8}, {4, 5}, {0, 7}, {0x76, 0}, {6, 0}, {0x2b, 6}, {0x1b, 3}, {0, 0x22}};
        bool every = true;
        for (const P &p : all)
            every = every && objectHasNoMarker(p.slot, p.area);
        check("all 21 (slot, area) pairs from the ROM are in the list", every);
        check("slot 0 in area 0 is not (0 is only listed for areas 7 and 0x22)", !objectHasNoMarker(0, 0) && !objectHasNoMarker(0, 1));
    }

    // ---- outdoors vs indoors ----
    for (int a : {0, 5, 9, 0xe, 0x15, 0x1b, 0x1c, 0x1d, 0x22})
        if (!markerOutdoors(a))
        {
            check("an outdoor area is recognised", false);
            break;
        }
    check("outdoor areas", markerOutdoors(0) && markerOutdoors(9) && markerOutdoors(0xe) && markerOutdoors(0x22));
    check("indoor areas", !markerOutdoors(10) && !markerOutdoors(0xf) && !markerOutdoors(0x14) && !markerOutdoors(0x1e));
    check("an outdoor item gets the tall arrow 0x11", itemMarkerAnim(3) == 0x11);
    check("an indoor item gets 0x12", itemMarkerAnim(0x10) == 0x12);

    // ---- positions ----
    {
        const MarkerPos p = itemMarkerPos(3, 100, 200, 24);
        check("outdoor item: arrow tip 'item height' above its position", p.x == 100 && p.y == 176);
        const MarkerPos q = itemMarkerPos(0x10, 100, 200, 24);
        check("indoor item: exactly at its position", q.x == 100 && q.y == 200);
        const MarkerPos n = npcMarkerPos(12, 150, 180, 48);
        check("NPC: above its head by its sprite height", n.x == 150 && n.y == 132);
        const MarkerPos t = npcMarkerPos(kMarkerTallNpc, 150, 180, 48);
        check("NPC 0x36 gets 0x14 extra", t.y == 180 - 48 - 0x14);
        const MarkerPos o = objectMarkerPos(40, 60, 65, 49);
        check("object: the centre of its sprite (integer halves)", o.x == 40 + 32 && o.y == 60 + 24);
    }

    // ---- the ring is skipped when its centre is off screen vertically ----
    check("on screen", objectMarkerOnScreen(100) && objectMarkerOnScreen(0) && objectMarkerOnScreen(239));
    check("just inside the lower limit (y + 0x1d == 0x148)", objectMarkerOnScreen(0x148 - 0x1d));
    check("below the limit: skipped", !objectMarkerOnScreen(0x148 - 0x1d + 1));
    check("far above: skipped", !objectMarkerOnScreen(-100));
    check("a little above the top is still drawn (its art extends down)", objectMarkerOnScreen(-29) && !objectMarkerOnScreen(-30));

    // ---- animation frame: shared, 18 fps, gameTicks +2 per 30 Hz tick ----
    check("frame 0 at tick 0", markerFrame(0, 9) == 0);
    check("one second (60 game ticks) = 18 frames = two full loops of 9", markerFrame(60, 9) == 0);
    check("a third of a second (20 game ticks) = 6 frames", markerFrame(20, 9) == 6);
    check("it loops", markerFrame(30, 9) == (30 * 18 / 60) % 9);
    check("no frames: 0, not a crash", markerFrame(100, 0) == 0);

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
