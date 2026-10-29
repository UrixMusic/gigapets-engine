// tests/world_map_test.cpp - where the island map's marker goes (game/world_map.hpp).
#include <cstdio>
#include "../src/game/world_map.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    const auto a = mapMarker(0, 2353, 821);
    check("Gigatown: pet x/32 + 0x34, y/32 + 0x55", a && a->x == 2353 / 32 + 0x34 && a->y == 821 / 32 + 0x55);
    const auto b = mapMarker(8, 0, 0);
    check("the spaceship's corner is its offset: (0xf3, 0x12)", b && b->x == 0xf3 && b->y == 0x12);
    check("every area 0..9 has an offset", mapMarker(1, 0, 0) && mapMarker(2, 0, 0) && mapMarker(3, 0, 0) && mapMarker(4, 0, 0) &&
                                              mapMarker(5, 0, 0) && mapMarker(6, 0, 0) && mapMarker(7, 0, 0) && mapMarker(9, 0, 0));
    check("area 6: (0x34, 0x23), area 5: (0xcb, 0x23)", mapMarker(6, 0, 0)->x == 0x34 && mapMarker(6, 0, 0)->y == 0x23 && mapMarker(5, 0, 0)->x == 0xcb);
    check("areas past 9 are not on the map", !mapMarker(10, 0, 0) && !mapMarker(0x22, 0, 0));
    check("the map is the 0x47b tile, the marker 0x47a", kMapTile == 0x47b && kMapMarkerTile == 0x47a && kMapPalette == 0x5c2);

    std::printf(fails ? "world_map_test: %d failure(s)\n" : "world_map_test: all passed\n", fails);
    return fails ? 1 : 0;
}
