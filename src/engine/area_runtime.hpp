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
#include <deque>
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

    // ---- changes made while the area is loaded (scripts: object.spawn / move / remove) ----
    // The area's own object table is the db's and never changes; the runtime keeps copies of what a script touches, so a
    // moved or added object lives only until the area is left (a new AreaRuntime is built when it is entered again).
    // Call updateVisible after any of these, the visible indices refer to positions in objects().
    bool has(const Id& obj) const { return find(obj) >= 0; }
    // Adds an object (its id must be new). Returns the stored copy.
    const ObjectDef* addObject(ObjectDef o) {
        if (has(o.id)) return nullptr;
        own_.push_back(std::move(o));
        owned_.insert(own_.back().id);
        objs_.push_back(&own_.back());
        resort();
        return &own_.back();
    }
    // Moves an object (a base one is copied first). The art/cull box moves with it. False if there is no such object.
    bool moveObject(const Id& obj, int x, int y) {
        ObjectDef* o = ownedCopy(obj);
        if (!o) return false;
        const int dx = x - o->x, dy = y - o->y;
        o->x = x; o->y = y;
        o->boxLeft += dx; o->boxRight += dx; o->boxTop += dy; o->boxBottom += dy;
        resort();
        return true;
    }
    // Takes an object out of the area (it is not drawn, blocks nothing, cannot be used). False if there is no such object.
    bool removeObject(const Id& obj) {
        const int i = find(obj);
        if (i < 0) return false;
        objs_.erase(objs_.begin() + i);
        return true;
    }

private:
    int find(const Id& obj) const {
        for (size_t i = 0; i < objs_.size(); ++i) if (objs_[i]->id == obj) return int(i);
        return -1;
    }
    void resort() {
        std::stable_sort(objs_.begin(), objs_.end(), [](const ObjectDef* a, const ObjectDef* b) { return a->y < b->y; });
    }
    ObjectDef* ownedCopy(const Id& obj) {
        const int i = find(obj);
        if (i < 0) return nullptr;
        if (!owned_.count(obj)) {                       // a base object: copy it, and use the copy from now on
            own_.push_back(*objs_[size_t(i)]);
            owned_.insert(obj);
            objs_[size_t(i)] = &own_.back();
        }
        return const_cast<ObjectDef*>(objs_[size_t(i)]);  // (it points into own_ now, so it is ours to change)
    }
    std::deque<ObjectDef> own_;   // stable addresses: objs_ points into it
    std::unordered_set<Id> owned_;
    const ContentDB& db_;
    const AreaDef& area_;
    std::vector<const ObjectDef*> objs_;
    std::vector<int> visible_;
    std::unordered_set<Id> hidden_, done_;
};

} // namespace gp