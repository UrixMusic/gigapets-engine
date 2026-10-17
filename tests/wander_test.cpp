// g++ -std=c++17 -Wall -Wextra wander_test.cpp -o wander_test && ./wander_test
#include "wander.hpp"
#include <cstdio>
using namespace gp;

static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-44s %s\n", n, ok ? "ok" : "FAIL"); if (!ok) ++fails; }

static std::vector<int> run(WanderState s, int len, bool pingPong, int steps) {
    std::vector<int> seq;
    for (int i = 0; i < steps; ++i) { advanceWaypoint(s, len, pingPong); seq.push_back(s.index); }
    return seq;
}

int main() {
    check("ping-pong 3 points",       run({0, false}, 3, true, 8)  == std::vector<int>{1, 2, 1, 0, 1, 2, 1, 0});
    check("loop, descending (flag clear)", run({0, false}, 3, false, 7) == std::vector<int>{2, 1, 0, 2, 1, 0, 2});
    check("loop, ascending (flag set)",    run({0, true}, 3, false, 7)  == std::vector<int>{1, 2, 0, 1, 2, 0, 1});
    check("ping-pong 2 points",       run({0, false}, 2, true, 6)  == std::vector<int>{1, 0, 1, 0, 1, 0});
    check("empty path is a no-op",    run({0, false}, 0, true, 3)  == std::vector<int>{0, 0, 0});

    std::vector<PathPoint> path = {{0, 0}, {50, 0}, {50, 30}};
    WanderState s{0, true};
    PathPoint t = wanderTarget(s, path, true, 100, 200, 100, 200);   // standing on waypoint 0, heading forward
    check("target = next point", t.dx == 150 && t.dy == 200 && s.index == 1);
    t = wanderTarget(s, path, true, 100, 200, 120, 200);             // not at the waypoint: keep going
    check("not at waypoint: unchanged", t.dx == 150 && t.dy == 200 && s.index == 1);
    return fails ? 1 : 0;
}
