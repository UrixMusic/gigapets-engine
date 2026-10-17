// Replays recorded NPC walk ticks against npc_walk.hpp.
// Usage: walk_replay <log>      (any text; lines containing "WALK," are parsed)
// Row: WALK,frame,actor,x,y,facing,style,phase,tx,ty,actionId,argTx,argTy   (state at ENTRY of TickActor_WalkWanderPath)
// Row k is simulated for one tick and compared with row k+1 of the same actor, but only when the two
// rows are exactly 2 video frames apart (one game tick).
#include "npc_walk.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace gp;

struct Row { int frame, actor, x, y, facing, style, phase, tx, ty, action, argTx, argTy, line; };

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: walk_replay <log>\n"); return 2; }
    std::ifstream f(argv[1]); std::string line; int ln = 0; std::vector<Row> rows;
    while (std::getline(f, line)) {
        ++ln; const size_t p = line.find("WALK,"); if (p == std::string::npos) continue;
        std::stringstream ss(line.substr(p + 5)); std::string tok; std::vector<long> v;
        while (std::getline(ss, tok, ',')) { char* e; long x = std::strtol(tok.c_str(), &e, 10); if (e == tok.c_str()) break; v.push_back(x); }
        if (v.size() < 12) continue;
        rows.push_back({int(v[0]), int(v[1]), int(int16_t(v[2])), int(int16_t(v[3])), int(v[4]), int(v[5]), int(v[6]),
                        int(int16_t(v[7])), int(int16_t(v[8])), int(v[9]), int(int16_t(v[10])), int(int16_t(v[11])), ln});
    }
    std::printf("%zu WALK rows read\n", rows.size());
    std::map<int, std::vector<size_t>> byActor; for (size_t i = 0; i < rows.size(); ++i) byActor[rows[i].actor].push_back(i);
    int pairs = 0, ok = 0, skipped = 0, shown = 0; std::map<int, int> phaseSeen; std::map<std::string, int> branches;
    for (auto& [actor, idx] : byActor)
        for (size_t k = 0; k + 1 < idx.size(); ++k) {
            const Row& a = rows[idx[k]]; const Row& b = rows[idx[k + 1]];
            if (b.frame - a.frame != 2) { ++skipped; continue; }
            WalkActor w{a.x, a.y, a.facing, a.style}; WalkState s{a.action, a.phase, a.tx, a.ty};
            tickWalk(w, s, a.argTx, a.argTy);
            ++pairs; ++phaseSeen[a.phase];
            if (a.phase == 2 && a.action == kActWanderPath) {
                const int dx = std::abs(a.tx - a.x), dy = std::abs(a.ty - a.y);
                std::string k = std::string("style") + (a.style ? "1 " : "0 ");
                if (a.facing >= 2) k += a.facing == 2 ? "up" : "down";
                else { k += a.facing == 0 ? "left " : "right "; k += dy == 0 ? "straight" : dy < dx ? "shallow" : dx < dy ? "steep" : "45deg"; }
                ++branches[k];
            }
            const bool match = w.x == b.x && w.y == b.y && w.facing == b.facing && s.phase == b.phase;
            ok += match;
            if (!match && shown++ < 15)
                std::printf("MISMATCH line %d actor %x phase %d: (%d,%d) face %d -> game (%d,%d) face %d phase %d, engine (%d,%d) face %d phase %d\n",
                            a.line, a.actor, a.phase, a.x, a.y, a.facing, b.x, b.y, b.facing, b.phase, w.x, w.y, w.facing, s.phase);
        }
    std::printf("\n%d/%d ticks match, %d pairs skipped (not consecutive ticks)\n", ok, pairs, skipped);
    std::printf("entry phases covered:"); for (auto& [p, n] : phaseSeen) std::printf(" %d:%d", p, n); std::printf("\n");
    std::printf("walking branches covered:\n"); for (auto& [k, n] : branches) std::printf("  %-22s %d\n", k.c_str(), n);
    return ok == pairs ? 0 : 1;
}