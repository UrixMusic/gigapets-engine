// tests/player_test.cpp
#include <cstdio>
#include "../src/game/player.hpp"
using namespace gp;

static int fails = 0;
static void check(const char* what, bool ok) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

int main() {
    // AdjustCareStat: negative deltas are ignored until quests > 3 (so 4 is the first that works)
    { Player p; p.care[Hunger] = 50; p.questsCompleted = 0; p.adjustCare(Hunger, -10);
      check("care: negative ignored at 0 quests", p.care[Hunger] == 50); }
    { Player p; p.care[Hunger] = 50; p.questsCompleted = 3; p.adjustCare(Hunger, -10);
      check("care: negative still ignored at 3 quests", p.care[Hunger] == 50); }
    { Player p; p.care[Hunger] = 50; p.questsCompleted = 4; p.adjustCare(Hunger, -10);
      check("care: negative applies at 4 quests", p.care[Hunger] == 40); }
    { Player p; p.questsCompleted = 0; p.adjustCare(Happiness, 5);
      check("care: positive always applies", p.care[Happiness] == 5); }
    { Player p; p.questsCompleted = 99; p.adjustCare(Cleanliness, 250);
      check("care: clamps to 100", p.care[Cleanliness] == 100); }
    { Player p; p.questsCompleted = 99; p.care[Tiredness] = 5; p.adjustCare(Tiredness, -50);
      check("care: clamps to 0", p.care[Tiredness] == 0); }

    // AdjustPersonalityStat: 0..9999
    { Player p; p.adjustPersonality(Stars, 20000);
      check("personality: clamps to 9999", p.personality[Stars] == 9999); }
    { Player p; p.adjustPersonality(Stars, -5);
      check("personality: clamps to 0", p.personality[Stars] == 0); }

    // halos/lightbulbs clear "encountered" for unfriended NPCs only; other stats don't
    { Player p; p.friended = {"base:npc/1"}; p.encountered = {"base:npc/1", "base:npc/2"};
      p.adjustPersonality(Stars, 1);
      check("encountered kept when stars change", p.encountered.size() == 2);
      p.adjustPersonality(Halos, 1);
      check("halos change clears unfriended only", p.encountered == std::set<std::string>{"base:npc/1"});
      p.encountered.insert("base:npc/3"); p.adjustPersonality(Lightbulbs, 1);
      check("lightbulbs change clears unfriended too", p.encountered == std::set<std::string>{"base:npc/1"}); }

    // money cap 99999 (0x1869f)
    { Player p; int got = p.adjustMoney(200000);
      check("money: caps at 99999", p.money == 99999 && got == 99999); }
    { Player p; p.money = 10; p.adjustMoney(-50);
      check("money: floors at 0", p.money == 0); }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}