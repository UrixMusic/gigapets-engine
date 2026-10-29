// tests/befriend_test.cpp  (needs Rng from npc_brain.hpp)
#include <cstdio>
#include "../src/game/befriend.hpp"
using namespace gp;

int main() {
    int fails = 0;
    auto check = [&](const char* w, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    // dead zone: -9..9 is 0, 10 and beyond pass through
    check("delta 9 -> 0",    personalityStatDelta(9, 0) == 0);
    check("delta -9 -> 0",   personalityStatDelta(0, 9) == 0);
    check("delta 10 -> 10",  personalityStatDelta(10, 0) == 10);
    check("delta -10 -> -10",personalityStatDelta(0, 10) == -10);

    // chance 50 should succeed 49 times in 100 (roll 1..100, success when roll < chance)
    Rng rng; rng.state = 12345u;
    int hits = 0; const int N = 200000;
    for (int i = 0; i < N; i++) if (befriendRoll(rng, 50)) hits++;
    const double pct = 100.0 * hits / N;
    std::printf("      chance 50 -> %.2f%% (expect about 49)\n", pct);
    check("chance 50 is ~49%", pct > 48.0 && pct < 50.0);
    check("chance 0 never succeeds", [&]{ for (int i=0;i<10000;i++) if (befriendRoll(rng,0)) return false; return true; }());
    check("chance 100 hits 99%", [&]{ int h=0; for (int i=0;i<N;i++) if (befriendRoll(rng,100)) h++; double p=100.0*h/N; return p>98.0&&p<100.0; }());

    // greetings are roughly even thirds
    int c[4] = {0, 0, 0, 0};
    for (int i = 0; i < 90000; i++) c[int(rollFriendGreeting(rng))]++;
    check("greeting thirds", c[1] > 28000 && c[2] > 28000 && c[3] > 28000 && c[0] == 0);

    // friend gifts: 39 entries, every roll lands in the table, and all three repeated groups are reachable
    check("gift table has 39 entries", kFriendGifts.size() == 0x27);
    {
        int seen[0x120] = {};
        bool inTable = true;
        for (int i = 0; i < 20000; i++) {
            const int g = rollFriendGift(rng);
            if (g < 0 || g >= 0x120) { inTable = false; break; }
            seen[g]++;
        }
        check("gift rolls stay in the table", inTable);
        check("every table entry shows up", [&]{ for (int g : kFriendGifts) if (!seen[g]) return false; return true; }());
        check("0x2a is three times as likely as 0x17", seen[0x2a] > seen[0x17] * 2);
    }
    // advice lines: text ids 0x2e7..0x315 inclusive
    check("advice ids stay in 0x2e7..0x315", [&]{
        for (int i = 0; i < 20000; i++) {
            const int id = std::stoi(friendAdviceId(rng).substr(10)); // after "base:text/"
            if (id < 0x2e7 || id > 0x315) return false;
        }
        return true; }());

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}