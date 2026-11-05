#include <cassert>
#include <cstdio>
#include <random>
#include <set>
#include "../game/quest_flow.hpp"
#include "../game/quest_types.hpp"
#include "quest_import.hpp"
using namespace gp;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main()
{
    std::mt19937 g(1);
    RandFn rnd = [&](int a, int b) { return a + int(g() % unsigned(b - a)); };

    // ---- generator invariants
    QuestWorld w;
    w.types = baseQuestTypes();
    w.areaTier = [](int id) { return id % 10 == 4 || id % 10 == 8 ? 0 : id % 4; }; // tiers 0..3
    w.stationary = [](int id) { return id % 7 == 0; };
    w.speciesOf = [](int id) { return id; };
    w.modeItem = [&](int m, int s) { return 0x300 + m * 16 + int(g() % 8) + (s & 0); };
    for (auto &r : w.rom.distance) for (int &v : r) v = 2;
    for (int i = 0; i < 0x43; ++i) w.rom.itemWeights.push_back({legacyItemId(0x40 + i), 1 + i % 3});
    { // mod npcs in the pool: no extras = the base draw exactly; with extras they are drawn, and their tier gates them
        QuestWorld b = w;
        b.extraPool = {1000, 1001};
        b.areaTier = [&](int id) { return id >= 1000 ? (id == 1000 ? 0 : 5) : w.areaTier(id); }; // 1001 needs 90 quests
        std::mt19937 g1(7), g2(7);
        RandFn r1 = [&](int x, int y) { return x + int(g1() % unsigned(y - x)); };
        RandFn r2 = [&](int x, int y) { return x + int(g2() % unsigned(y - x)); };
        for (int i = 0; i < 500; ++i) // no extras: the same draws as ever (the ids are those of the base range)
        {
            const int id = pickNpc(0x30, w, r1);
            CHECK(id >= 0 && id < 0x4e);
        }
        int mod0 = 0, mod1 = 0;
        for (int i = 0; i < 4000; ++i)
        {
            const int id = pickNpc(0x30, b, r2);
            mod0 += id == 1000;
            mod1 += id == 1001; // tier 5 is locked below 90 quests
        }
        CHECK(mod0 > 20 && mod1 == 0);
        int late = 0;
        for (int i = 0; i < 4000; ++i) late += pickNpc(0x5a, b, r2) == 1001;
        CHECK(late > 20);
    }
    for (int total : {1, 5, 20, 0x30, 0x50, 0x5a})
    {
        int prevT = 0x4e, prevM = 8;
        for (int it = 0; it < 2000; ++it)
        {
            QuestDef d = rollNextQuest(total, prevT, prevM, {}, w, rnd);
            CHECK(d.giver != prevT && d.typeMode != prevM);
            CHECK(d.typeMode == 0 || d.typeMode == 1 || d.typeMode == 2 || d.typeMode == 3 || d.typeMode == 6 || d.typeMode == 7);
            if (d.typeMode == 1 || d.typeMode == 6) CHECK(d.receiver == d.giver);
            if (d.typeMode == 7) CHECK(d.giver == 0x48 && d.receiver == 0x48);
            if (d.typeMode == 2 || d.typeMode == 3) CHECK(!w.stationary(d.receiver) && d.receiver != d.giver);
            if (d.typeMode == 2 || d.typeMode == 6) CHECK(!d.spawn.empty() && d.areaTier == 1);
            if (d.typeMode == 2 || d.typeMode == 3) CHECK(total > 3 || d.typeMode == 3);
            CHECK(!d.objectives.empty() && d.objectives.size() <= 3);
            std::set<Id> s;
            for (auto &o : d.objectives) { CHECK(s.insert(o.item).second); CHECK(o.count >= 1 && o.count <= 6); }
            CHECK(!d.bonus.empty() && d.bonus.size() <= 3);
            if (total < 0x23) CHECK(d.bonus.size() == 1);
            if (d.typeMode == 1) CHECK(d.timeLimit > 0 && d.areaTier != 5 && d.areaTier != 6);
            prevT = d.receiver; prevM = d.typeMode;
        }
    }
    std::puts("generator ok");

    // ---- importer on the three records from the dump
    std::vector<uint16_t> rom(0x10000, 0);
    auto put = [&](uint32_t a, std::initializer_list<uint16_t> v) { for (auto x : v) rom[a++] = x; };
    put(0xc8ea, {1, 0, 0x42, 0x42, 0xc797, 0xc79d, 0, 0xc794, 0xc54b, 0xc554, 0});
    put(0xc8f5, {3, 4, 0x42, 0x42, 0xc7a2, 0xc7a6, 0, 0xc55a, 0xc560, 0xc566, 0});
    put(0xc794, {0xffff, 0, 0});
    put(0xc797, {0x10, 0x11, 0xffff});
    put(0xc79d, {0x12, 0xfffe, 0xffff});
    put(0xc54b, {2, 5, 0, 0xffff, 0, 0});
    put(0xc554, {1, 9, 0, 0xffff, 0, 0});
    put(0xc7a2, {0x20, 0xffff}); put(0xc7a6, {0x21, 0xfff9, 0xffff});
    put(0xc55a, {1, 0x77, 0, 0xffff, 0, 0});
    put(0xc560, {1, 0x78, 0, 0xffff, 0, 0});
    put(0xc566, {1, 0x79, 0, 0xffff, 0, 0});
    auto word = [&](uint32_t a) { return rom[a & 0xffff]; };
    auto tbl = importQuestTable(word, 0xc8ea, 2);
    CHECK(tbl.size() == 2 && tbl[0].id == 1 && tbl[1].id == 3 && tbl[1].typeMode == 4);
    CHECK((tbl[0].offerText == std::vector<int>{0x10, 0x11}));
    CHECK((tbl[0].completionText == std::vector<int>{0x12, -2}));
    CHECK(tbl[0].objectives.size() == 1 && tbl[0].objectives[0].count == 2 && tbl[0].objectives[0].item == legacyItemId(5));
    CHECK(tbl[0].spawn.empty() && tbl[1].spawn.size() == 1 && tbl[1].spawn[0].item == legacyItemId(0x77));
    CHECK((tbl[1].completionText == std::vector<int>{0x21, -7}));
    std::puts("import ok");

    // ---- fillTemplate / fillTemplateVars: placeholders from a run-time list
    CHECK(fillTemplateVars("Got {n} {item}s, {unknown}!", {{"item", "pie"}, {"n", "3"}}) == "Got 3 pies, {unknown}!");
    CHECK(fillTemplateVars("{a}{a}", {{"a", "{b}"}, {"b", "no"}}) == "{b}{b}");   // values are not scanned again
    CHECK(fillTemplateVars("no vars {", {}) == "no vars {" && fillTemplateVars("{x", {{"x", "1"}}) == "{x");
    CHECK(fillTemplate("{npc} in {area}", {{"npc", "Rex"}, {"area", "town"}}) == "Rex in town");   // the old form still works
    std::puts("template ok");

    // ---- flow
    Dialogue dlg;
    QuestRuntime qr;
    qr.dlg = &dlg;
    qr.table = tbl;
    int inv5 = 0, spawned = 0, placed = 0, cleared = 0, flaggedPlaced = 0;
    auto &h = qr.h;
    h.text = [](int id) { return "t" + std::to_string(id); };
    const Id item5 = legacyItemId(5);
    h.held = [&](const Id &item) { return item == item5 ? inv5 : 0; };
    h.remove = [&](const Id &item, int n) { if (item == item5) inv5 -= n; };
    h.spawn = [&](int, const Id &, int, bool f) { if (f) ++spawned; };
    h.place = [&](const Id &, int, int, bool f) { ++placed; flaggedPlaced += f; };
    h.randomEvent = [] {};
    h.clearFlagged = [&] { ++cleared; };
    h.sfx = [](int) {};
    h.camX = [] { return 100; };
    h.camY = [] { return 200; };
    h.receiverY = [] { return 150; };
    h.nowTick = [] { return 5; };
    h.rnd = rnd;
    qr.load(1);
    CHECK(qr.q.def.id == 1);
    CHECK(!qr.talk(0x10));                   // someone else
    CHECK(qr.talk(0x42) && dlg.open());      // offer text 0
    CHECK(dlg.box->kind == BoxKind::Message && dlg.box->body == "t16");
    dlg.choose(1);
    CHECK(dlg.open() && dlg.box->kind == BoxKind::Confirm && dlg.box->body == "t17");
    dlg.choose(1);
    CHECK(qr.q.phase == 1 && qr.q.hudActive && spawned == 4);
    CHECK(qr.talk(0x42) && dlg.open());      // receiver, missing items
    dlg.choose(1);
    inv5 = 2;
    CHECK(qr.talk(0x42) && dlg.open() && cleared == 1 && inv5 == 0);
    CHECK(dlg.box->body == "t18");
    dlg.choose(1);                           // -2 reward runs, then next quest loads
    CHECK(placed == 1 && flaggedPlaced == 0);
    CHECK(qr.counter == 2 && qr.q.def.id == -1 && qr.q.phase == 0);   // no world: generic stays inert
    CHECK(qr.q.prevReceiver == 0x42 && qr.q.prevTypeMode == 0);
    // generic with world
    qr.world = w;
    qr.load(2);
    CHECK(qr.q.def.id == 2 && qr.q.def.generic);
    int g0 = qr.q.def.giver;
    CHECK(qr.talk(g0) && dlg.open());
    dlg.choose(1);                           // intro text -> call 1 text -> call 2 confirm
    CHECK(dlg.open() && dlg.box->kind == BoxKind::Message);
    dlg.choose(1);
    CHECK(dlg.open() && dlg.box->kind == BoxKind::Confirm);
    dlg.choose(1);
    CHECK(qr.q.phase == 1);
    // clamp
    qr.load(500);
    CHECK(qr.counter == 4);
    // ---- onComplete: true skips the bonus rewards (the -2 / -7 activities); the quest still completes and advances
    for (int skip = 0; skip < 2; ++skip)
    {
        Dialogue d2;
        QuestRuntime r2;
        r2.dlg = &d2;
        r2.table = tbl;
        int held5 = 2, placed2 = 0, sfxes = 0, completes = 0;
        auto &k = r2.h;
        k.text = [](int id) { return "t" + std::to_string(id); };
        k.held = [&](const Id &it) { return it == legacyItemId(5) ? held5 : 0; };
        k.remove = [&](const Id &it, int n) { if (it == legacyItemId(5)) held5 -= n; };
        k.spawn = [](int, const Id &, int, bool) {};
        k.place = [&](const Id &, int, int, bool) { ++placed2; };
        k.randomEvent = [] {};
        k.clearFlagged = [] {};
        k.sfx = [&](int) { ++sfxes; };
        k.camX = [] { return 100; };
        k.camY = [] { return 200; };
        k.receiverY = [] { return 150; };
        k.nowTick = [] { return 5; };
        k.rnd = rnd;
        k.onComplete = [&] { ++completes; return skip == 1; };
        r2.load(1);
        CHECK(r2.talk(0x42));
        d2.choose(1);                      // offer text -> the confirm box
        d2.choose(1);                      // accept
        CHECK(r2.q.phase == 1 && completes == 0);
        CHECK(r2.talk(0x42) && d2.open()); // the hand-in
        CHECK(completes == 1 && held5 == 0 && d2.box->body == "t18"); // fired once, objectives already taken, text still shown
        d2.choose(1);                      // thank-you text -> the reward activity -> next quest
        CHECK(completes == 1);
        CHECK(placed2 == (skip ? 0 : 1) && sfxes == (skip ? 0 : 1)); // bonus items scattered only when not taken over
        CHECK(r2.counter == 2 && r2.q.phase == 0);                   // the chain moved on either way
    }
    // ---- rewardSystem: sees every reward activity's code; false cancels it, the dialogue and the chain carry on
    for (int mode = 0; mode < 2; ++mode)
    {
        Dialogue d4;
        QuestRuntime r4;
        r4.dlg = &d4;
        r4.table = tbl;
        int held6 = 2, placed4 = 0, sfx4 = 0;
        std::vector<int> codes;
        auto &k = r4.h;
        k.text = [](int id) { return "t" + std::to_string(id); };
        k.held = [&](const Id &it) { return it == legacyItemId(5) ? held6 : 0; };
        k.remove = [&](const Id &it, int n) { if (it == legacyItemId(5)) held6 -= n; };
        k.spawn = [](int, const Id &, int, bool) {};
        k.place = [&](const Id &, int, int, bool) { ++placed4; };
        k.randomEvent = [] {};
        k.clearFlagged = [] {};
        k.sfx = [&](int) { ++sfx4; };
        k.camX = [] { return 100; };
        k.camY = [] { return 200; };
        k.receiverY = [] { return 150; };
        k.nowTick = [] { return 5; };
        k.rnd = rnd;
        k.rewardSystem = [&](int &code) { codes.push_back(code); return mode == 0; };
        r4.load(1);
        r4.talk(0x42); d4.choose(1); d4.choose(1);
        r4.talk(0x42);
        d4.choose(1);
        CHECK(!codes.empty() && codes.back() == -2);
        CHECK(placed4 == (mode == 0 ? 1 : 0) && sfx4 == (mode == 0 ? 1 : 0));
        CHECK(r4.counter == 2 && r4.q.phase == 0);
    }
    // ---- entries as text, and shapeQuest: the host sees the quest being set up and may change it
    {
        std::vector<QuestEntry> v = parseQuestEntries("base:item/5*2, m:item/x*3*1,nope*1,base:item/9*0,", [](const Id &i) { return i != "nope"; });
        CHECK(v.size() == 2 && v[0].item == "base:item/5" && v[0].count == 2 && v[1].item == "m:item/x" && v[1].count == 3 && v[1].w2 == 1);
        CHECK(formatQuestEntries(v) == "base:item/5*2,m:item/x*3*1");
        CHECK(parseQuestEntries("a", nullptr).size() == 1 && parseQuestEntries("a", nullptr)[0].count == 1);
        Dialogue d5;
        QuestRuntime r5;
        r5.dlg = &d5;
        r5.table = tbl;
        int seen = -1;
        bool rolled = false;
        r5.h.shapeQuest = [&](QuestDef &d, int counter, bool scripted, const std::function<void()> &roll) { seen = counter; if (scripted) d.giver = 0x33; (void)roll; };
        r5.load(1);
        CHECK(seen == 1 && r5.q.def.giver == 0x33); // a scripted quest, changed
        r5.h.shapeQuest = [&](QuestDef &d, int counter, bool scripted, const std::function<void()> &roll) { rolled = !scripted; roll(); };
        r5.load(r5.lastId() + 1);
        CHECK(rolled && r5.q.def.giver >= 0); // a generated one: the roll ran, nothing is left at the placeholder -1
    }
    // ---- textSystem: a generated quest's reminder goes through the host
    {
        Dialogue d6;
        QuestRuntime r6;
        r6.dlg = &d6;
        r6.table = tbl;
        std::vector<std::string> kinds;
        auto &k = r6.h;
        k.text = [](int id) { return "t" + std::to_string(id); };
        k.held = [](const Id &) { return 0; };
        k.remove = [](const Id &, int) {};
        k.spawn = [](int, const Id &, int, bool) {};
        k.place = [](const Id &, int, int, bool) {};
        k.randomEvent = [] {};
        k.clearFlagged = [] {};
        k.sfx = [](int) {};
        k.camX = [] { return 0; };
        k.camY = [] { return 0; };
        k.receiverY = [] { return 0; };
        k.nowTick = [] { return 5; };
        k.rnd = rnd;
        k.textSystem = [&](const char *kind, std::string &t) { kinds.push_back(kind); t += "!"; };
        r6.txt.str = [](const std::string &key) { return "S:" + key; };
        r6.txt.npcName = [](int) { return std::string("N"); };
        r6.txt.areaName = [](int) { return std::string("A"); };
        r6.txt.itemName = [](const Id &, int) { return std::string("I"); };
        r6.txt.rnd = rnd;
        r6.load(1);
        r6.talk(0x42); d6.choose(1); d6.choose(1); // accepted (scripted quest: its lines come from the text table, untouched)
        r6.talk(0x42); // the receiver, objectives missing: the reminder
        CHECK(!kinds.empty() && kinds.back() == "reminder");
        CHECK(d6.open() && d6.box->body.size() > 1 && d6.box->body.back() == '!');
    }
    // no onComplete wired: rewards as before
    {
        Dialogue d3;
        QuestRuntime r3;
        r3.dlg = &d3;
        r3.table = tbl;
        int placed3 = 0;
        auto &k = r3.h;
        k.text = [](int id) { return "t" + std::to_string(id); };
        k.held = [](const Id &) { return 9; };
        k.remove = [](const Id &, int) {};
        k.spawn = [](int, const Id &, int, bool) {};
        k.place = [&](const Id &, int, int, bool) { ++placed3; };
        k.randomEvent = [] {};
        k.clearFlagged = [] {};
        k.sfx = [](int) {};
        k.camX = [] { return 100; };
        k.camY = [] { return 200; };
        k.receiverY = [] { return 150; };
        k.nowTick = [] { return 5; };
        k.rnd = rnd;
        r3.load(1);
        r3.talk(0x42); d3.choose(1); d3.choose(1);
        r3.talk(0x42); d3.choose(1);
        CHECK(placed3 == 1);
    }
    std::puts("quest complete ok");
    std::puts("flow ok");
    return 0;
}