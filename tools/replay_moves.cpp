// Replay recorded moves against the engine's resolver using REAL room data from the ROM.
//
// Usage: replay_moves <rom> <log> [--cf N] [--verbose]
//   <log>  MAME debug log (or any text): every line containing "MOVE," is parsed, the rest is ignored.
//   --cf N override the collision frame (for areas missing from the room table)
//   --deltas   also print how often each requested step (reqX-lastX, reqY-lastY) occurs
//
// Row format (decimal):
//   MOVE,area,camX,camY,yoff,lastX,lastY,reqX,reqY,count,list0..list15[,outX,outY]
// The result of a move is the NEXT row's last position (same area), unless outX,outY are given.
//
// Build: g++ -std=c++17 -Wall -Wextra -Isrc/loaders -Isrc/engine tools/replay_moves.cpp -o build/replay_moves.exe
#include "collision.hpp"
#include "objects.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

using namespace gp;

struct Row {
    int area, camX, camY, yoff, lastX, lastY, reqX, reqY, count;
    int list[16];
    bool hasOut = false; int outX = 0, outY = 0;
    int line = 0;
};

struct Ctx {   // everything loaded for one area
    Area area; RoomLink link{}; bool hasLink = false;
    ProgressFlags flags = ProgressFlags::allSet();
    GroundCollision ground; ObjectCollision objects;
    Ctx(const Rom& rom, Area a, RoomLink l, bool has, int cf, int yoff)
        : area(std::move(a)), link(l), hasLink(has), ground(rom, area, cf, yoff), objects(rom, area, flags, yoff) {}
};

struct World : IWorld {
    Ctx& c; Room room;
    explicit World(Ctx& ctx) : c(ctx), room{ctx.area.tilesW * 32, ctx.area.tilesH * 32, kMarginLeft, kMarginRight, kMarginTop, kMarginBottom} {}
    bool tileWalkable(int x, int y) const override { return c.ground.walkable(x, y); }
    bool inRoom(int x, int y) const override { return gp::inRoom(room, x, y); }
    bool objectBlocks(int x, int y) const override { return c.objects.blocks(x, y); }
};

static std::vector<Row> parse(const std::string& path) {
    std::ifstream f(path); std::vector<Row> rows; std::string line; int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        const size_t p = line.find("MOVE,"); if (p == std::string::npos) continue;
        std::vector<int> v; std::stringstream ss(line.substr(p + 5)); std::string tok;
        while (std::getline(ss, tok, ',')) { char* e; const long x = std::strtol(tok.c_str(), &e, 10); if (e == tok.c_str()) break; v.push_back(int(x)); }
        if (v.size() < 25) { std::printf("line %d: only %zu fields, skipped\n", ln, v.size()); continue; }
        Row r; r.area = v[0]; r.camX = int16_t(v[1]); r.camY = int16_t(v[2]); r.yoff = int16_t(v[3]);   // yoff is signed (0xFFFC = -4)
        r.lastX = v[4]; r.lastY = v[5];
        r.reqX = v[6]; r.reqY = v[7]; r.count = v[8]; for (int i = 0; i < 16; ++i) r.list[i] = v[9 + i];
        if (v.size() >= 27) { r.hasOut = true; r.outX = v[25]; r.outY = v[26]; }
        r.line = ln; rows.push_back(r);
    }
    return rows;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: replay_moves <rom> <log> [--cf N] [--verbose]\n"); return 2; }
    int cfOverride = -1; bool verbose = false, deltas = false;
    for (int i = 3; i < argc; ++i) { if (!std::strcmp(argv[i], "--cf") && i + 1 < argc) cfOverride = std::atoi(argv[++i]); else if (!std::strcmp(argv[i], "--verbose")) verbose = true; else if (!std::strcmp(argv[i], "--deltas")) deltas = true; }
    try {
        Rom rom(argv[1]);
        const std::vector<Row> rows = parse(argv[2]);
        std::printf("%zu MOVE rows read\n", rows.size());
        if (rows.empty()) return 2;
        {   // capture sanity: a walking player requests positions within a few px of the current one
            int far = 0, idle = 0, used = 0;
            for (size_t k = 0; k < rows.size(); ++k) {
                if (k && rows[k - 1].area != rows[k].area) continue;      // room entry may place the player anywhere
                ++used; const Row& r = rows[k];
                if (std::abs(r.reqX - r.lastX) > 16 || std::abs(r.reqY - r.lastY) > 16) ++far;
                if (r.reqX == r.lastX && r.reqY == r.lastY) ++idle;
            }
            std::printf("capture check: %d of %d rows idle (req == last), %d with a request more than 16 px away\n", idle, used, far);
            if (used && far * 5 > used)
                std::printf("WARNING: many requests are far from the current position. The stack offsets in the MAME command (sp+3, sp+4) are probably wrong.\n");
        }

        if (deltas) {   // histogram of requested steps: shows walking speeds directly
            std::map<std::pair<int, int>, int> h;
            for (size_t k = 0; k < rows.size(); ++k) { if (k && rows[k - 1].area != rows[k].area) continue; ++h[{rows[k].reqX - rows[k].lastX, rows[k].reqY - rows[k].lastY}]; }
            std::vector<std::pair<int, std::pair<int, int>>> v; for (auto& e : h) v.push_back({e.second, e.first});
            std::sort(v.rbegin(), v.rend());
            std::printf("requested steps (dx,dy): count\n");
            for (size_t i = 0; i < v.size() && i < 24; ++i) std::printf("  (%3d,%3d): %d\n", v[i].second.first, v[i].second.second, v[i].first);
            std::printf("\n");
        }
        std::map<long, std::unique_ptr<Ctx>> ctxs;
        auto get = [&](const Row& r) -> Ctx* {
            const long key = long(r.area) * 65536 + (r.yoff & 0xffff);
            auto it = ctxs.find(key); if (it != ctxs.end()) return it->second.get();
            Area a; if (!readArea(rom, r.area, a)) { std::printf("area %d cannot be read\n", r.area); return nullptr; }
            RoomLink l{}; const bool has = readRoomLink(rom, r.area, l);
            const int cf = cfOverride >= 0 ? cfOverride : (has ? l.collisionFrame : -1);
            if (cf < 0) { std::printf("area %d is not in the room table: pass --cf N\n", r.area); return nullptr; }
            auto c = std::make_unique<Ctx>(rom, std::move(a), l, has, cf, r.yoff);
            Ctx* p = c.get(); ctxs[key] = std::move(c);
            std::printf("area %d: %dx%d tiles, collision frame %d, %zu objects, y offset %d\n", r.area, p->area.tilesW, p->area.tilesH, cf, p->area.objects.size(), r.yoff);
            return p;
        };

        int pairs = 0, ok = 0, skipped = 0;
        int freeN = 0, freeOk = 0, slideN = 0, slideOk = 0, stuckN = 0, stuckOk = 0, shown = 0;
        std::map<int, int> tableHits; int objBlocked = 0, objBlockedOk = 0;
        int cullChecked = 0, cullAgree = 0, cullShown = 0, truncated = 0;
        for (size_t k = 0; k < rows.size(); ++k) {
            const Row& r = rows[k]; Ctx* c = get(r); if (!c) { ++skipped; continue; }
            int ex, ey;
            if (r.hasOut) { ex = r.outX; ey = r.outY; }
            else if (k + 1 < rows.size() && rows[k + 1].area == r.area) { ex = rows[k + 1].lastX; ey = rows[k + 1].lastY; }
            else { ++skipped; continue; }

            // informational: does our culling reproduce the game's list? (list at row k was built during row k-1)
            if (k > 0 && rows[k - 1].area == r.area && r.count <= 16) {
                c->objects.updateVisible(rows[k - 1].camX, rows[k - 1].camY);
                std::vector<int> mine = c->objects.visible(), theirs(r.list, r.list + r.count);
                std::sort(mine.begin(), mine.end()); std::sort(theirs.begin(), theirs.end());
                ++cullChecked; if (mine == theirs) ++cullAgree;
                else if (cullShown++ < 3) std::printf("  cull differs at line %d: game %zu objects, engine %zu (flags assumed all set)\n", r.line, theirs.size(), mine.size());
            }
            if (r.count > 16) ++truncated;
            c->objects.setVisible(std::vector<int>(r.list, r.list + std::min(r.count, 16)));

            World w(*c);
            const MoveResult got = resolveMove(w, {r.reqX, r.reqY}, {r.lastX, r.lastY});
            const bool match = got.pos.x == ex && got.pos.y == ey;
            const bool isFree = ex == r.reqX && ey == r.reqY, isStuck = ex == r.lastX && ey == r.lastY && !isFree;
            if (!isFree) ++tableHits[pickWindow({r.reqX, r.reqY}, {r.lastX, r.lastY}).table];
            if (w.objectBlocks(r.reqX, r.reqY)) { ++objBlocked; objBlockedOk += match; }
            ++pairs; ok += match;
            (isFree ? freeN : isStuck ? stuckN : slideN)++;
            (isFree ? freeOk : isStuck ? stuckOk : slideOk) += match;
            if (!match && shown++ < 20) {
                std::printf("MISMATCH line %d (area %d): last (%d,%d) req (%d,%d) -> game (%d,%d), engine (%d,%d)%s\n", r.line, r.area,
                            r.lastX, r.lastY, r.reqX, r.reqY, ex, ey, got.pos.x, got.pos.y, got.slid ? " [slid]" : "");
                std::printf("    at req: object-blocked=%d tile-walkable=%d in-room=%d\n", w.objectBlocks(r.reqX, r.reqY), w.tileWalkable(r.reqX, r.reqY), w.inRoom(r.reqX, r.reqY));
            } else if (verbose && match) std::printf("ok   line %d: (%d,%d)->(%d,%d)\n", r.line, r.lastX, r.lastY, ex, ey);
        }
        std::printf("\n%d/%d moves match  (free %d/%d, slides %d/%d, blocked-stay %d/%d), %d unpaired\n", ok, pairs, freeOk, freeN, slideOk, slideN, stuckOk, stuckN, skipped);
        std::printf("coverage: slide/stay windows used:");
        for (int t : {0x26, 0x3b, 0x50, 0x5e, 0x6c, 0x78, 0x84, 0x90, 0x9c}) std::printf(" %02x:%d", t, tableHits[t]);
        std::printf("\n          requests blocked by an object overlay: %d (matched %d)\n", objBlocked, objBlockedOk);
        if (cullChecked) std::printf("culling check (informational): %d/%d lists identical\n", cullAgree, cullChecked);
        if (truncated) std::printf("note: %d rows had more than 16 visible objects; only the first 16 were logged\n", truncated);
        if (slideN < 10) std::printf("note: only %d slide cases; record more wall and corner contacts for a meaningful test\n", slideN);
        return ok == pairs ? 0 : 1;
    } catch (const std::exception& e) { std::printf("error: %s\n", e.what()); return 2; }
}