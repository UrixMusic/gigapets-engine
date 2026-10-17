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

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}