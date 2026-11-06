// tests/objective_test.cpp - the HUD objectives scripts put up (objective.set): the Player's list and its place in the save.
#include "../src/game/data/save_player.hpp"
#include <cstdio>

using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    Player p;
    check("starts empty", p.objectives.empty() && p.objective("m:obj/a") == nullptr);
    check("adds in order", p.setObjective({"m:obj/a", "m:str/a", "m:item/pie", 3}) && p.setObjective({"m:obj/b", "m:str/b", "", 0}) &&
                               p.objectives.size() == 2 && p.objectives[0].id == "m:obj/a" && p.objectives[1].id == "m:obj/b");
    check("setting the same id replaces it in place", p.setObjective({"m:obj/a", "m:str/a2", "", 0}) && p.objectives.size() == 2 &&
                                                         p.objectives[0].text == "m:str/a2" && p.objectives[0].need == 0);
    check("clearing removes it, once", p.clearObjective("m:obj/b") && !p.clearObjective("m:obj/b") && p.objectives.size() == 1);
    for (int i = 0; i < 20; ++i)
        p.setObjective({"m:obj/n" + std::to_string(i), "m:str/n", "", 0});
    check("no more than eight at once", p.objectives.size() == Player::kMaxObjectives && !p.setObjective({"m:obj/extra", "m:str/n", "", 0}));

    // the save
    Player q;
    q.setObjective({"m:obj/pies", "m:str/find", "m:item/pie", 3});
    q.setObjective({"m:obj/talk", "m:str/talk", "", 0});
    q.setObjective({"m:obj/count", "", "m:item/pie", 2});
    SaveData s;
    savePlayer(q, s);
    Player r;
    loadPlayer(s, r);
    check("they come back in order, complete", r.objectives.size() == 3 && r.objectives[0].id == "m:obj/pies" && r.objectives[0].text == "m:str/find" &&
                                                   r.objectives[0].item == "m:item/pie" && r.objectives[0].need == 3 && r.objectives[1].id == "m:obj/talk" &&
                                                   r.objectives[1].item.empty() && r.objectives[1].need == 0 && r.objectives[2].text.empty() &&
                                                   r.objectives[2].item == "m:item/pie" && r.objectives[2].need == 2);
    q.clearObjective("m:obj/talk");
    savePlayer(q, s); // over the same save: the cleared one must not come back
    Player r2;
    loadPlayer(s, r2);
    check("a cleared one is gone from the save", r2.objectives.size() == 2 && !s.has("objective.m:obj/talk"));
    q.objectives.clear();
    savePlayer(q, s);
    Player r3;
    r3.setObjective({"m:obj/stale", "m:str/x", "", 0});
    loadPlayer(s, r3);
    check("an empty list saves nothing and loads empty", !s.has("objectives") && r3.objectives.empty());
    SaveData old; // a save from before objectives existed
    Player r4;
    loadPlayer(old, r4);
    check("an old save has none", r4.objectives.empty());

    std::printf(fails ? "FAILED: %d check(s)\n" : "objective_test: all passed\n", fails);
    return fails ? 1 : 0;
}
