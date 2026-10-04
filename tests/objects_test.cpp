// g++ -std=c++17 -Wall -Wextra -I. objects_test.cpp -o objects_test && ./objects_test
#include "objects.hpp"
#include <cstdio>
using namespace gp;

static int fails = 0;
static void check(const char* n, bool got, bool want) { std::printf("%-34s %s\n", n, got == want ? "ok" : "FAIL"); if (got != want) ++fails; }

int main() {
    // Synthetic ROM: overlay sprite 0 = 8x8, anchor (0,0), top 4 rows solid (4bpp index 1)
    std::vector<uint8_t> b((0x60000 + 0x100) * 2, 0);
    auto w = [&](uint32_t a, std::initializer_list<int> v) { for (int x : v) { b[a * 2] = x & 255; b[a * 2 + 1] = (x >> 8) & 255; ++a; } };
    w(0x40000, {8, 8, 1, 0, 0, 0});
    w(0x30000, {0, 1, 0xFFFF, 0x01});
    for (int i = 0; i < 8; ++i) w(0x60000 + i, {0x1111});            // rows 0-3
    w(kTypeTable + 3 * 5, {0, 0, 0, 0, 0});                          // type 3: overlay sprite 0
    w(kTypeTable + 4 * 5, {0, 0, 0, 0, 0xFFFF});                     // type 4: no overlay
    Rom rom(b);

    Area a; a.objects.push_back({100, 100, 0, 0, 3, 0xFFFF, 110, 90, 90, 110});   // x y rx ry type id yBot yTop xL xR
    a.objects.push_back({200, 100, 0, 0, 4, 0xFFFF, 110, 90, 190, 210});           // object without overlay
    ProgressFlags on; on.words = {0x0003};                                         // objects 0 and 1 active
    ProgressFlags off; off.words = {0x0002};                                       // object 0 inactive

    ObjectCollision oc(rom, a, on); oc.updateVisible(0, 0);
    check("visible count == 2", oc.visible().size() == 2, true);
    check("solid pixel (100,100)", oc.blocks(100, 100), true);
    check("solid pixel (107,103)", oc.blocks(107, 103), true);
    check("empty row (100,104)", oc.blocks(100, 104), false);
    check("outside width (108,100)", oc.blocks(108, 100), false);
    check("object without overlay", oc.blocks(200, 100), false);

    ObjectCollision oc2(rom, a, off); oc2.updateVisible(0, 0);
    check("inactive flag: not visible", oc2.visible().size() == 1, true);
    check("inactive flag: no block", oc2.blocks(100, 100), false);

    ObjectCollision oc3(rom, a, on); oc3.updateVisible(1000, 0);
    check("camera away: culled", oc3.visible().empty(), true);

    ObjectCollision oc4(rom, a, on, 4); oc4.updateVisible(0, 0);
    check("yOffset 4 shifts the mask", oc4.blocks(100, 100), false);
    check("yOffset 4 (100,96) hits row 0", oc4.blocks(100, 96), true);
    ProgressFlags f = ProgressFlags::allSet();
    check("allSet: object 44 active", f.isActive(44), true);
    f.clear(44);
    check("clear(44): word 0x542 = 0xEFFF", f.words[2] == 0xEFFF && !f.isActive(44) && f.isActive(43), true);
    f.set(44);
    check("set(44) restores", f.words[2] == 0xFFFF, true);
    // FindNearestInteractableObject
    Area n; n.objects = {
        {100, 100, 30, 30, 3, 0xFFFF, 0, 0, 0, 0},   // 0: scenery
        {110, 100, 30, 30, 3, 7, 0, 0, 0, 0},        // 1: interactive, dx 10
        {105, 100, 30, 30, 3, 8, 0, 0, 0, 0},        // 2: interactive, dx 5 (nearest)
        {100, 140, 30, 30, 3, 9, 0, 0, 0, 0},        // 3: out of reach (dy 40)
    };
    std::vector<int> all = {0, 1, 2, 3};
    ProgressFlags done = ProgressFlags::allClear();
    check("nearest interactable = object 2", findNearestInteractable(n, all, done, 100, 100) == 2, true);
    done.set(2);
    check("used object skipped -> object 1", findNearestInteractable(n, all, done, 100, 100) == 1, true);
    check("nothing in reach = -1", findNearestInteractable(n, all, ProgressFlags::allClear(), 500, 500) == -1, true);
    check("allClear is all zero", ProgressFlags::allClear().words.size() == 15 && !ProgressFlags::allClear().isActive(0), true);
    return fails ? 1 : 0;
}