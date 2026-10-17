// area_runtime.hpp - object culling, blocking and interaction for one loaded area, driven by content data.
// Replaces ObjectCollision + findNearestInteractable + ProgressFlags. Port of CullVisibleNPCs (0x1c7e4),
// CheckObjectCollisionAtPoint and FindNearestInteractableObject (0x1c117); logic copied from objects.hpp.
//
// State is keyed by object ID, not table index, so areas can have any number of objects and mods can add some.
// The ROM's "present" bits (0x540..) become the hidden set (cleared bit = hidden); the "done" bits (0x531..)
// become the done set. Import legacy saves through ObjectDef::legacySlot.
#pragma once
#include "../content/db.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

namespace gp {

class AreaRuntime {
public:
    AreaRuntime(const ContentDB& db, const AreaDef& area) : db_(db), area_(area) {
        for (const ObjectDef& o : area.objects) objs_.push_back(&o);
        std::stable_sort(objs_.begin(), objs_.end(), [](const ObjectDef* a, const ObjectDef* b) { return a->y < b->y; });
    }

    const AreaDef& area() const { return area_; }
    const std::vector<const ObjectDef*>& objects() const { return objs_; }   // y-sorted (stable): draw order

    void setHidden(const Id& obj, bool h) { if (h) hidden_.insert(obj); else hidden_.erase(obj); }
    void setDone(const Id& obj, bool d)   { if (d) done_.insert(obj); else done_.erase(obj); }
    bool isHidden(const Id& obj) const { return hidden_.count(obj) != 0; }
    bool isDone(const Id& obj) const   { return done_.count(obj) != 0; }

    // CullVisibleNPCs (full scan instead of the original's incremental y-window).
    void updateVisible(int camX, int camY, int viewW = 320, int viewH = 240) {
        visible_.clear();
        for (size_t i = 0; i < objs_.size(); ++i) {
            const ObjectDef& o = *objs_[i];
            if (isHidden(o.id)) continue;
            if (o.y + 45 < camY) continue;
            if (o.y - 200 >= camY + viewH) continue;
            if (o.boxRight < camX || o.boxLeft > camX + viewW) continue;
            if (o.boxBottom < camY || o.boxTop > camY + viewH) continue;
            visible_.push_back(int(i));
        }
    }
    void setVisibleOnly(const Id& obj) {   // for tests: only this object is visible
        visible_.clear();
        for (size_t i = 0; i < objs_.size(); ++i) if (objs_[i]->id == obj) visible_.push_back(int(i));
    }
    void setAllVisible() { visible_.clear(); for (size_t i = 0; i < objs_.size(); ++i) visible_.push_back(int(i)); }   // for tests

    // CheckObjectCollisionAtPoint, inverted: true = blocked by an object overlay. yOffset = RAM 0x1afa (0 so far).
    bool blocks(int x, int y, int yOffset = 0) const {
        const int yy = y + yOffset;
        for (int idx : visible_) {
            const ObjectDef& o = *objs_[size_t(idx)];
            const ObjectType* t = db_.objectType(o.type);
            if (!t || t->mask.blocked.empty()) continue;
            const int mx = x - (o.x + t->mask.ox), my = yy - (o.y + t->mask.oy);
            if (mx >= 0 && my >= 0 && mx < t->mask.w && my < t->mask.h && t->mask.blocked[size_t(my) * t->mask.w + mx]) return true;
        }
        return false;
    }

    // FindNearestInteractableObject: nearest interactable visible object whose reach box holds the player, or nullptr.
    // 16-bit distance arithmetic like the original.
    const ObjectDef* nearestInteractable(int px, int py) const {
        const ObjectDef* best = nullptr; uint16_t bestD2 = 0xFFFF;
        for (int idx : visible_) {
            const ObjectDef& o = *objs_[size_t(idx)];
            if (!o.interactive() || isDone(o.id)) continue;
            const int dx = std::abs(px - o.x), dy = std::abs(py - o.y);
            if (dx >= o.reachX || dy >= o.reachY) continue;
            const uint16_t d2 = uint16_t(uint16_t(dx * dx) + uint16_t(dy * dy));
            if (d2 < bestD2) { best = &o; bestD2 = d2; }
        }
        return best;
    }

    const std::vector<int>& visible() const { return visible_; }

private:
    const ContentDB& db_;
    const AreaDef& area_;
    std::vector<const ObjectDef*> objs_;
    std::vector<int> visible_;
    std::unordered_set<Id> hidden_, done_;
};

} // namespace gp