#include <cassert>
#include <cstdio>
#include <random>
#include <set>
#include "../game/quest_flow.hpp"
#include "quest_import.hpp"
using namespace gp;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main()
{
    std::mt19937 g(1);
    RandFn rnd = [&](int a, int b) { return a + int(g() % unsigned(b - a)); };

    // ---- generator invariants
    QuestWorld w;
    w.areaTier = [](int id) { return id % 10 == 4 || id % 10 == 8 ? 0 : id % 4; }; // tiers 0..3
    w.stationary = [](int id) { return id % 7 == 0; };
    w.speciesOf = [](int id) { return id; };
    w.modeItem = [&](int m, int s) { return 0x300 + m * 16 + int(g() % 8) + (s & 0); };
    for (auto &r : w.rom.distance) for (int &v : r) v = 2;
    for (int i = 0; i < 0x43; ++i) w.rom.itemWeights.push_back({0x40 + i, 1 + i % 3});
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
            std::set<int> s;
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
    CHECK(tbl[0].objectives.size() == 1 && tbl[0].objectives[0].count == 2 && tbl[0].objectives[0].item == 5);
    CHECK(tbl[0].spawn.empty() && tbl[1].spawn.size() == 1 && tbl[1].spawn[0].item == 0x77);
    CHECK((tbl[1].completionText == std::vector<int>{0x21, -7}));
    std::puts("import ok");

    // ---- flow
    Dialogue dlg;
    QuestRuntime qr;
    qr.dlg = &dlg;
    qr.table = tbl;
    int inv5 = 0, spawned = 0, placed = 0, cleared = 0, flaggedPlaced = 0;
    auto &h = qr.h;
    h.text = [](int id) { return "t" + std::to_string(id); };
    h.held = [&](int item) { return item == 5 ? inv5 : 0; };
    h.remove = [&](int item, int n) { if (item == 5) inv5 -= n; };
    h.spawn = [&](int, int, int, bool f) { if (f) ++spawned; };
    h.place = [&](int, int, int, bool f) { ++placed; flaggedPlaced += f; };
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
    std::puts("flow ok");
    return 0;
}