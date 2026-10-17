// ground.hpp - ground walkability from content data (replaces GroundCollision's ROM reads).
#pragma once
#include "../content/db.hpp"

namespace gp {

class GroundField {
public:
    GroundField(const AreaDef& area, const CollisionSet& cs) : a_(area), c_(cs) {}
    // Same rules as GroundCollision::walkable: y is raw, the offset is applied here and nowhere else.
    bool walkable(int x, int y) const {
        const int yy = y + a_.collisionYOffset;
        if (x < 0 || yy < 0) return false;
        const int col = x >> 5, row = yy >> 5;
        if (col >= a_.tilesW || row >= a_.tilesH) return false;
        const int tile = a_.tiles[size_t(row) * a_.tilesW + col];
        return !c_.isBlocked(tile, x & 31, yy & 31);
    }
private:
    const AreaDef& a_;
    const CollisionSet& c_;
};

} // namespace gp
