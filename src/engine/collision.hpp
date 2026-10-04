// Gigapets Explorer engine: movement/collision resolver (C++17, header-only)
// Port of the collision part of PlacePlayerAndInitCameraOnRoomEntry (Ghidra 0x01c5xx).
// Coordinates are raw world pixels. The 0x1afa y offset (0 so far) belongs INSIDE
// the IWorld queries, never in this code, to avoid applying it twice.
#pragma once
#include <cstdint>
#include <vector>

namespace gp {

struct Point { int x = 0, y = 0; };

// Room bounds. width bounds x (RAM 0x1aab), height bounds y (RAM 0x1aaa); margins are the
// constants in LoadArea (see kMargin* in room.hpp), the same for every room.
struct Room { int width, height, left, right, top, bottom; };

// Inclusive upper bounds, matching IsPositionWithinRoomBounds.
inline bool inRoom(const Room& r, int x, int y) {
    return x >= r.left && x <= r.width - r.right &&
           y >= r.top  && y <= r.height - r.bottom;
}

// The three queries the resolver needs. Back them with decoded room data and
// sprite index arrays.
struct IWorld {
    virtual ~IWorld() = default;
    virtual bool tileWalkable(int x, int y) const = 0;  // ground sheet pixel == 0
    virtual bool inRoom(int x, int y) const = 0;
    virtual bool objectBlocks(int x, int y) const = 0;  // any overlay pixel index > 0
};

// Slide priority tables, dumped from RAM 0x26..0x9c. Index = row*w + col.
struct SlideTable { const uint8_t* idx; int n; };
inline SlideTable slideTable(int id) {
    static constexpr uint8_t t26[] = {11,17,5,14,8,10,16,4,13,7,9,15,3,12,6,20,2,19,1,18,0};
    static constexpr uint8_t t3b[] = {9,15,3,12,6,10,16,4,13,7,11,17,5,14,8,18,0,19,1,20,2};
    static constexpr uint8_t t50[] = {3,1,5,2,4,10,8,12,9,11,0,6,7,13};
    static constexpr uint8_t t5e[] = {10,8,12,9,11,3,1,5,2,4,7,13,0,6};
    static constexpr uint8_t t6c[] = {2,3,1,7,6,11,5,0,10,4,9,8};
    static constexpr uint8_t t78[] = {10,11,9,7,6,3,5,8,2,4,1,0};
    static constexpr uint8_t t84[] = {1,0,2,4,5,8,6,3,9,7,10,11};
    static constexpr uint8_t t90[] = {9,8,10,4,5,0,6,11,1,7,2,3};
    static constexpr uint8_t t9c[] = {0};
    switch (id) {
        case 0x26: return {t26, 21};
        case 0x3b: return {t3b, 21};
        case 0x50: return {t50, 14};
        case 0x5e: return {t5e, 14};
        case 0x6c: return {t6c, 12};
        case 0x78: return {t78, 12};
        case 0x84: return {t84, 12};
        case 0x90: return {t90, 12};
        default:   return {t9c, 1};
    }
}

struct Window { int w, h, ox, oy, table; };

// Direction mapping derived from the decompile; still needs the
// 8-direction MAME confirmation.
inline Window pickWindow(Point req, Point last) {
    const int dx = req.x - last.x, dy = req.y - last.y;
    const bool right = dx > 0, down = dy > 0;
    if (dx && dy) {
        const int table = right ? (down ? 0x78 : 0x6c) : (down ? 0x90 : 0x84);
        return {4, 3, req.x - (right ? 2 : 1), req.y - (down ? 2 : 0), table};
    }
    if (dy) return {7, 2, req.x - 3, req.y - (down ? 1 : 0), down ? 0x5e : 0x50};
    if (dx) return {3, 7, req.x - (right ? 2 : 0), req.y - 3, right ? 0x26 : 0x3b};
    return {1, 1, req.x, req.y, 0x9c};
}

inline bool isFree(const IWorld& w, int x, int y) {
    return !w.objectBlocks(x, y) && w.tileWalkable(x, y) && w.inRoom(x, y);
}

struct MoveResult {
    Point pos;
    bool slid;  // mirrors RAM[0x25]: a non-first candidate was used (original writes 0 there)
};

inline MoveResult resolveMove(const IWorld& world, Point req, Point last) {
    if (isFree(world, req.x, req.y)) return {req, false};

    const Window win = pickWindow(req, last);
    std::vector<uint8_t> cells(win.w * win.h, 1);
    for (int r = 0; r < win.h; ++r)
        for (int c = 0; c < win.w; ++c) {
            const int x = win.ox + c, y = win.oy + r;
            if (!world.tileWalkable(x, y) || !world.inRoom(x, y)) cells[r * win.w + c] = 0;
        }
    // ClipWalkableMaskAgainstSpritePixels: same object test on the remaining cells
    for (int r = 0; r < win.h; ++r)
        for (int c = 0; c < win.w; ++c)
            if (cells[r * win.w + c] && world.objectBlocks(win.ox + c, win.oy + r))
                cells[r * win.w + c] = 0;

    const SlideTable st = slideTable(win.table);
    for (int i = 0; i < st.n; ++i) {
        const int idx = st.idx[i];
        if (cells[idx]) return {{win.ox + idx % win.w, win.oy + idx / win.w}, i != 0};
    }
    return {last, false};  // nothing free: stay put
}

// Replay harness for MAME-recorded moves.
struct Record { Point last, req, out; };
struct ReplayResult { int total = 0; std::vector<int> failed; };

inline ReplayResult replay(const IWorld& world, const std::vector<Record>& recs) {
    ReplayResult res; res.total = (int)recs.size();
    for (int i = 0; i < res.total; ++i) {
        const MoveResult g = resolveMove(world, recs[i].req, recs[i].last);
        if (g.pos.x != recs[i].out.x || g.pos.y != recs[i].out.y) res.failed.push_back(i);
    }
    return res;
}

} // namespace gp