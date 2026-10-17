#include "../src/game/frontend.hpp"
#include <cstdio>
#include <cstdlib>

using namespace gp;
using namespace gp::fe;

static int fails = 0;
#define CHECK(c)                                                  \
    do                                                            \
    {                                                             \
        if (!(c))                                                 \
        {                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);      \
            ++fails;                                              \
        }                                                         \
    } while (0)

static SlotInfo full(const char *n) { SlotInfo s; s.valid = true; s.name = n; return s; }

int main()
{
    // ---- main menu
    {
        MainMenu m;
        CHECK(m.cursor == 1 && !m.ready);
        // a press during the intro only reveals the buttons
        CHECK(m.onPress(Btn::Down) == 0 && m.ready && m.cursor == 1);
        CHECK(m.onPress(Btn::Down) == kSfxMove && m.cursor == 2);
        CHECK(m.onPress(Btn::Down) == kSfxMove && m.cursor == 2); // clamps, still beeps
        CHECK(m.onPress(Btn::Up) == kSfxMove && m.cursor == 1);
        CHECK(!m.onRelease(Btn::Confirm));                          // released without a press
        CHECK(m.onPress(Btn::Confirm) == kSfxConfirm);
        CHECK(m.onRelease(Btn::Confirm) == State::LoadSlot);
        m.cursor = 0;
        CHECK(m.onRelease(Btn::Confirm) == State::NewIntro);
        m.cursor = 2;
        CHECK(m.onRelease(Btn::Confirm) == State::Options);
    }
    {
        // the slide-in finishes and the buttons appear by themselves
        MainMenu m;
        int ticks = 0;
        while (!m.ready && ticks < 1000)
            m.tick(), ++ticks;
        CHECK(m.a == 0 && m.b == 0 && m.c == 8 && ticks < 300);
        std::printf("main menu intro: %d ticks (%.1f s at 30 Hz)\n", ticks, ticks / 30.0);
        MainMenu back(true);
        CHECK(back.a == 0 && back.b == 0 && back.c == 8);
        back.tick();
        CHECK(back.ready);
    }

    // ---- load screen
    {
        SlotInfo none[3];
        LoadScreen l(none);
        CHECK(l.cursor == 3);
        CHECK(l.onButton(Btn::Confirm)->next == State::MainMenu); // Back
        CHECK(l.label(0) == "Empty" && l.label(3) == "Back");
    }
    {
        SlotInfo s[3] = {{}, full("doogie"), full("rex")};
        LoadScreen l(s);
        CHECK(l.cursor == 1);                  // first valid
        l.onButton(Btn::Up);
        CHECK(l.cursor == 1);                  // slot 0 is empty: stays
        l.onButton(Btn::Down);
        CHECK(l.cursor == 2);
        l.onButton(Btn::Down);
        CHECK(l.cursor == 3);
        l.onButton(Btn::Up);
        CHECK(l.cursor == 2);
        auto r = l.onButton(Btn::Confirm);
        CHECK(r && r->slot == 2 && r->next == State::Gameplay);
        CHECK(l.onButton(Btn::Back)->next == State::MainMenu);
    }
    {
        SlotInfo s[3] = {full("a"), {}, full("c")};
        LoadScreen l(s);
        l.onButton(Btn::Down);
        CHECK(l.cursor == 2);                  // skipped the empty slot 1
        l.onButton(Btn::Up);
        CHECK(l.cursor == 0);
    }

    // ---- house select
    {
        SlotInfo s[3] = {full("doogie"), {}, full("rex")};
        HouseSelect h(s);
        CHECK(h.cursor == 1);                  // first empty
        auto r = h.onButton(Btn::Confirm);
        CHECK(r && r->slot == 1);              // empty: creates straight away
        h.cursor = 0;
        CHECK(!h.onButton(Btn::Confirm) && h.confirming);
        CHECK(h.confirmText() ==
              "This will remove doogie from the game.  Are you sure you want this?");
        CHECK(!h.onButton(Btn::Down) && h.cursor == 0); // frozen while the box is open
        CHECK(!h.onAnswer(false) && !h.confirming);
        h.onButton(Btn::Confirm);
        auto y = h.onAnswer(true);
        CHECK(y && y->slot == 0 && y->next == State::Gameplay);
        h.cursor = 3;
        CHECK(h.onButton(Btn::Confirm)->slot == -1);
    }
    {
        SlotInfo s[3] = {full("a"), full("b"), full("c")};
        HouseSelect h(s);
        CHECK(h.cursor == 3);                  // all full -> Back
    }

    // ---- newGame
    {
        StarterPick p;
        p.species = 1;
        p.appearance = 0x29e;
        p.personality[0] = 5; p.personality[1] = 5; p.personality[2] = 4; p.personality[3] = 6;
        p.sick = true;
        NewGameEnv env;
        env.randRange = [](int lo, int) { return lo; };
        env.rollIllness = [](int) { return 7; };
        SaveData s = newGame(p, "doogie", 2, env);
        CHECK(s.getInt("version") == 2 && s.getInt("species") == 1 && s.getInt("house") == 2);
        CHECK(s.getStr("name") == "doogie");
        CHECK(s.getInt("money") == 0x28);
        CHECK((s.getList("care") == std::vector<int>{0x34, 0x41, 0, 0x50, 0x4b, 0x4b}));
        CHECK((s.getList("personality") == std::vector<int>{5, 5, 4, 6}));
        CHECK(s.getInt("illness") == 7);
        const auto inv = s.getMap("inventory");
        CHECK(inv.size() == 8 && inv.at(0x21) == 2 && inv.at(0x6e) == 1 && !inv.count(0x6b));
        CHECK(s.getMap("equip").at(0xc) == 0x89);
        p.sick = false;
        CHECK(newGame(p, "x", 0, env).getInt("illness") == kNoIllness);
        // all three kits: 8 distinct grants, 6 equip slots
        for (int sp = 0; sp < 3; ++sp)
        {
            p.species = sp;
            SaveData t = newGame(p, "x", 0, env);
            CHECK(t.getMap("inventory").size() == 8 && t.getMap("equip").size() == 6);
        }
        // round trip through the file format
        CHECK(saveToFile("fe_test_save.txt", s));
        SaveData back;
        CHECK(loadFromFile("fe_test_save.txt", back) && back.has("version") && back.getStr("name") == "doogie");
        std::remove("fe_test_save.txt");
    }

    std::printf(fails ? "%d FAILED\n" : "frontend_test: all passed\n", fails);
    return fails ? 1 : 0;
}