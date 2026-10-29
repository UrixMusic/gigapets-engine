// tests/quest_nudge_test.cpp - the "Go see Mayor Peter!" reminder timer (game/quest_nudge.hpp).
#include <cstdio>
#include "../src/game/quest_nudge.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    QuestNudge n;
    n.restart(0);
    check("not due before 30 s", !n.due(30 * 60, 1, 0, -1));
    check("due just after 30 s", n.due(30 * 60 + 1, 1, 0, -1));
    check("then the limit is 180 s", !n.due(30 * 60 + 1 + 179 * 60, 1, 0, -1) && n.due(30 * 60 + 1 + 180 * 60 + 1, 1, 0, -1));

    QuestNudge m;
    m.restart(0);
    check("never while Mayor Peter is the pet's neighbour", !m.due(1000 * 60, 1, 0, 0x42) && m.due(1000 * 60, 1, 0, 0x10));
    QuestNudge a;
    a.restart(0);
    check("never with a quest active", !a.due(1000 * 60, 1, 1, -1));
    check("never after the second quest is done (or none)", !a.due(1000 * 60, 2, 0, -1) && !a.due(1000 * 60, 0, 0, -1));
    check("restarting goes back to 30 s", [&]
          {
              QuestNudge r;
              r.limit = 180;
              r.restart(100);
              return r.limit == 30.0 && r.start == 100;
          }());
    check("it applies only with exactly one quest done and none active", QuestNudge::applies(1, 0) && !QuestNudge::applies(1, 2) && !QuestNudge::applies(2, 0));

    std::printf(fails ? "quest_nudge_test: %d failure(s)\n" : "quest_nudge_test: all passed\n", fails);
    return fails ? 1 : 0;
}
