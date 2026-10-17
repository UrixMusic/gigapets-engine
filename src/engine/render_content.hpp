// render_content.hpp - area rendering from ContentDB. Same output as render.hpp, no ROM access.
// Tiles past the sheet are magenta (as before). Objects are drawn in y-sorted order, art offset as in render.hpp
// (x - originX - anchorX, y - originY - anchorY + 1), which the importer folded into ObjectType::art.dx/dy.
#pragma once
#include "image.hpp"
#include "ground.hpp"
#include "../content/db.hpp"
#include <algorithm>
#include <unordered_map>

namespace gp {

inline Image renderBackground(const ContentDB& db, const AreaDef& a) {
    Image img(a.widthPx(), a.heightPx());
    const Tileset* ts = db.tileset(a.tileset);
    std::unordered_map<int, std::vector<uint8_t>> cache;
    for (int r = 0; r < a.tilesH; ++r)
        for (int c = 0; c < a.tilesW; ++c) {
            const int id = a.tiles[size_t(r) * a.tilesW + c];
            auto it = cache.find(id);
            if (it == cache.end()) {
                std::vector<uint8_t> cell(32 * 32 * 4);
                if (ts && id < ts->count) {
                    const uint8_t* src = &ts->rgba[size_t(id) * ts->tileW * ts->tileH * 4];
                    for (int y = 0; y < 32; ++y)
                        for (int x = 0; x < 32; ++x) {        // nearest scale to a 32x32 cell
                            const uint8_t* s = &src[(size_t(y * ts->tileH / 32) * ts->tileW + (x * ts->tileW / 32)) * 4];
                            std::copy(s, s + 4, &cell[(size_t(y) * 32 + x) * 4]);
                        }
                } else for (size_t i = 0; i < cell.size(); i += 4) { cell[i] = 255; cell[i + 1] = 0; cell[i + 2] = 255; cell[i + 3] = 255; }
                it = cache.emplace(id, std::move(cell)).first;
            }
            blit(img, it->second.data(), 32, 32, c * 32, r * 32);
        }
    return img;
}

inline int drawObjects(Image& img, const ContentDB& db, const AreaDef& a) {
    std::vector<const ObjectDef*> v;
    for (const ObjectDef& o : a.objects) v.push_back(&o);
    std::stable_sort(v.begin(), v.end(), [](const ObjectDef* p, const ObjectDef* q) { return p->y < q->y; });
    int drawn = 0;
    for (const ObjectDef* o : v) {
        const ObjectType* t = db.objectType(o->type);
        if (!t || !t->hasArt) continue;
        blit(img, t->art.rgba.data(), t->art.w, t->art.h, o->x - t->art.anchorX + t->art.dx, o->y - t->art.anchorY + t->art.dy);
        ++drawn;
    }
    return drawn;
}

inline void overlayGroundCollision(Image& img, const GroundField& g) {
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (!g.walkable(x, y)) {
                uint8_t* p = &img.rgba[(size_t(y) * img.w + x) * 4];
                p[0] = uint8_t((p[0] * 6 + 255 * 4) / 10); p[1] = uint8_t(p[1] * 6 / 10); p[2] = uint8_t(p[2] * 6 / 10);
            }
}

inline void overlayObjectCollision(Image& img, const ContentDB& db, const AreaDef& a) {
    for (const ObjectDef& o : a.objects) {
        const ObjectType* t = db.objectType(o.type);
        if (!t || t->mask.blocked.empty()) continue;
        for (int my = 0; my < t->mask.h; ++my)
            for (int mx = 0; mx < t->mask.w; ++mx) {
                if (!t->mask.blocked[size_t(my) * t->mask.w + mx]) continue;
                const int x = o.x + t->mask.ox + mx, y = o.y + t->mask.oy + my;
                if (x < 0 || y < 0 || x >= img.w || y >= img.h) continue;
                uint8_t* p = &img.rgba[(size_t(y) * img.w + x) * 4];
                p[0] = uint8_t(p[0] * 6 / 10); p[1] = uint8_t(p[1] * 6 / 10); p[2] = uint8_t((p[2] * 6 + 255 * 4) / 10);
            }
    }
}

} // namespace gp
