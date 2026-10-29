// tests/stat_notify_test.cpp - the stat / money change panel (game/stat_notify.hpp).
#include <cstdio>
#include "../src/game/stat_notify.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("keys: money 0, halos 2, pitchforks 6, stars 0xa, lightbulbs 0xe", kStatNoticeSlots[0].key == 0 && kStatNoticeSlots[1].key == 2 &&
                                                                              kStatNoticeSlots[2].key == 6 && kStatNoticeSlots[3].key == 0xa && kStatNoticeSlots[4].key == 0xe);
    check("the icon shifts: 4 digits 0, 3 digits 4, fewer 8", statNoticeShift(1000) == 0 && statNoticeShift(999) == 4 && statNoticeShift(100) == 4 && statNoticeShift(99) == 8);

    {
        StatNotice n;
        n.add(0xa, 3); // three stars
        long stars = 53;
        auto cur = [&](int i) -> long { return i == 3 ? stars : 0; };
        int steps = 0, sounds = 0;
        bool sawDown = false;
        int maxOff = 0;
        // run until it is idle again
        for (; steps < 400; ++steps)
        {
            if (!n.visible() && steps > 20)
                break;
            const int s = n.tick(cur);
            if (s >= 0)
                ++sounds;
            maxOff = std::max(maxOff, n.offset);
            sawDown = sawDown || n.state == 3;
        }
        check("three stars: three counting sounds, panel dropped to 0x26 and went away", sounds == 3 && maxOff == kStatNoticeDrop && sawDown && !n.visible());
        check("the number ended on the new value (53) and nothing is pending", n.shown == 53 && n.pending[3] == 0);
    }
    {
        StatNotice n;
        n.add(0, 12); // 12 Bucks
        auto cur = [&](int) -> long { return 112; };
        int sounds = 0;
        long first = -1;
        for (int i = 0; i < 100; ++i)
        {
            const int s = n.tick(cur);
            if (first < 0 && n.state == 2)
                first = n.shown;
            if (s >= 0)
                ++sounds;
        }
        check("money counts 5, 5, 2 (3 steps) from 100", first == 100 && sounds == 3 && n.shown == 112);
    }
    {
        StatNotice n;
        n.add(2, -2); // lose two halos
        auto cur = [&](int) -> long { return 8; };
        int down = 0;
        for (int i = 0; i < 200; ++i)
            if (n.tick(cur) >= 0)
                ++down;
        check("a loss counts down from the old value: 10 -> 8 in two steps", down == 2 && n.shown == 8 && n.pending[1] == 0);
    }
    {
        StatNotice n;
        n.add(5, 9); // not a counter
        check("an unknown key is ignored", n.pending == std::array<int, 5>{});
    }
    {
        StatNotice n;
        n.add(2, 1);
        n.add(0xe, 1);
        long v = 5;
        auto cur = [&](int) -> long { return v; };
        int notices = 0;
        bool was = false;
        for (int i = 0; i < 600; ++i)
        {
            n.tick(cur);
            if (n.visible() && !was)
                ++notices;
            was = n.visible();
        }
        check("two changes show one after the other", notices == 2);
    }

    std::printf(fails ? "stat_notify_test: %d failure(s)\n" : "stat_notify_test: all passed\n", fails);
    return fails ? 1 : 0;
}
