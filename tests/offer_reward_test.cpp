// tests/offer_reward_test.cpp - what the king and Gardenia give back for a hand-in (game/offerings.hpp).
#include <cstdio>
#include "../src/game/offerings.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    OfferSetDef s;
    s.id = "base:offers/test";
    for (int i = 0; i < 4; ++i)
        s.items.push_back(legacyItemId(0xaa + i));
    s.rewardItems = {0x10, 0x11, 0x12, 0x13};

    Player p;
    check("nothing handed over: count 0, the first reward", offeredCount(s, p) == 0 && rewardFor(s, 0) == 0x10);

    const Id first = tryOffer(s, p, [](const Id &) { return 1; });
    check("one is handed over, the next reward is the second", !first.empty() && offeredCount(s, p) == 1 && rewardFor(s, 1) == 0x11);

    // the count is read before the hand-in
    Player q;
    q.setFlag(offeredKey(s.items[0])); // the king's auto-marked first item counts
    check("an auto-marked first item shifts the reward along", offeredCount(s, q) == 1 && rewardFor(s, offeredCount(s, q)) == 0x11);

    check("out of range or an empty table gives nothing", rewardFor(s, 4) == -1 && rewardFor(s, -1) == -1 && rewardFor(OfferSetDef{}, 0) == -1);

    std::printf(fails ? "offer_reward_test: %d failure(s)\n" : "offer_reward_test: all passed\n", fails);
    return fails ? 1 : 0;
}
