// content_parity_test.cpp - new ContentDB path vs the existing ROM path: rendered image, object blocking, interaction.
// Needs render.hpp to include engine/image.hpp instead of defining its own Image/blit (see image.hpp).
// Usage: content_parity_test <rom>
#include "../src/loaders/import.hpp"
#include "../src/engine/render.hpp"
#include "../src/engine/render_content.hpp"
#include "../src/engine/area_runtime.hpp"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: content_parity_test <rom>\n"); return 1; }
    gp::Rom rom(argv[1]);
    gp::ContentDB db;
    gp::importAreas(rom, db);

    long bad = 0;
    for (int id = 0; id < gp::areaCount(rom); ++id) {
        const gp::AreaDef* a = db.areaByLegacy(id);
        gp::Area area; gp::RoomLink link;
        if (!a || !gp::readArea(rom, id, area) || !gp::readRoomLink(rom, id, link)) { std::printf("area %2d: skipped\n", id); continue; }

        std::printf("area %2d: checking...\n", id); std::fflush(stdout);

        // 1. rendered image (background + objects)
        gp::Palette pal; const bool hasPal = gp::readPalette(rom, link.paletteId, pal);
        gp::Image oldImg = gp::renderBackground(rom, area, gp::TileSheet::fromFrame(rom, link.tileFrame), hasPal ? &pal : nullptr);
        gp::drawObjects(oldImg, rom, area);
        gp::Image newImg = gp::renderBackground(db, *a);
        gp::drawObjects(newImg, db, *a);
        long imgBad = oldImg.w != newImg.w || oldImg.h != newImg.h ? 1 : 0;
        if (!imgBad) for (size_t i = 0; i < oldImg.rgba.size(); i += 4)
            if (!std::equal(&oldImg.rgba[i], &oldImg.rgba[i] + 4, &newImg.rgba[i])) ++imgBad;

        // 2a. each object in isolation, over its own mask box (+3 px)
        gp::ProgressFlags flags = gp::ProgressFlags::allSet();
        gp::ObjectCollision oldObj(rom, area, flags, 0);
        gp::AreaRuntime neu(db, *a);
        long blkBad = 0;
        for (const gp::ObjectDef& o : a->objects) {
            const gp::ObjectType* t = db.objectType(o.type);
            if (!t || t->mask.blocked.empty()) continue;
            oldObj.setVisible({o.legacySlot});
            neu.setVisibleOnly(o.id);
            for (int y = o.y + t->mask.oy - 3; y < o.y + t->mask.oy + t->mask.h + 3; ++y)
                for (int x = o.x + t->mask.ox - 3; x < o.x + t->mask.ox + t->mask.w + 3; ++x)
                    if (oldObj.blocks(x, y) != neu.blocks(x, y)) ++blkBad;
        }
        // 2b. every object visible, coarse grid over the whole area
        std::vector<int> all; for (size_t k = 0; k < area.objects.size(); ++k) all.push_back(int(k));
        oldObj.setVisible(all); neu.setAllVisible();
        for (int y = -8; y < a->heightPx() + 8; y += 7)
            for (int x = -8; x < a->widthPx() + 8; x += 7)
                if (oldObj.blocks(x, y) != neu.blocks(x, y)) ++blkBad;

        // 3. nearest interactable from a grid of player positions (visible list = all)
        long nearBad = 0;
        const gp::ProgressFlags clear = gp::ProgressFlags::allClear();
        for (int y = 0; y < a->heightPx(); y += 16)
            for (int x = 0; x < a->widthPx(); x += 16) {
                const int o = gp::findNearestInteractable(area, all, clear, x, y);
                const gp::ObjectDef* n = neu.nearestInteractable(x, y);
                // object ids carry the legacy table index, compare through it
                if ((o < 0) != (n == nullptr) || (n && n->legacySlot != o)) ++nearBad;
            }

        std::printf("area %2d: image %s, blocking %s, interact %s\n", id,
                    imgBad ? "MISMATCH" : "ok", blkBad ? "MISMATCH" : "ok", nearBad ? "MISMATCH" : "ok");
        if (imgBad) std::printf("         %ld px differ\n", imgBad);
        if (blkBad) std::printf("         %ld blocking px differ\n", blkBad);
        if (nearBad) std::printf("         %ld positions differ\n", nearBad);
        bad += imgBad + blkBad + nearBad;
    }
    std::printf(bad ? "FAILED\n" : "all areas match\n");
    return bad ? 1 : 0;
}