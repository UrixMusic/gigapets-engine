#include "../src/game/ui/test_mode.hpp"
#include <cstdio>

using namespace gp::testmode;

static int fails = 0;
static void check(const char *n, bool ok)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", n);
    fails += !ok;
}

static void press(ButtonTest &t, int ev) { t.tick(ev, false); }
static void idle(ButtonTest &t, int ticks)
{
    for (int i = 0; i < ticks; ++i)
        t.tick(-1, false);
}

int main()
{
    // the menu wraps both ways
    MenuList m;
    m.move(-1);
    check("Up from the first row goes to the last", m.sel == kRows - 1);
    m.move(1);
    check("Down from the last goes to the first", m.sel == 0);

    // entry: hold, chime, Up Down Menu Cancel
    {
        EntrySequence e;
        bool chimed = false;
        for (int i = 0; i < EntrySequence::kHoldTicks - 1; ++i)
            chimed |= e.tick(true) == EntrySequence::Result::Chime;
        check("no chime before 3 s", !chimed && !e.armed());
        check("the chime at 3 s", e.tick(true) == EntrySequence::Result::Chime && e.armed());
        e.press(Up);
        e.press(Down);
        e.press(Menu);
        check("the sequence enters on the last button", e.press(Cancel) == EntrySequence::Result::Entered);
    }
    {
        EntrySequence e;
        for (int i = 0; i < 40; ++i)
            e.tick(true);
        e.tick(false); // let go: starts over
        bool chimed = false;
        for (int i = 0; i < EntrySequence::kHoldTicks - 1; ++i)
            chimed |= e.tick(true) == EntrySequence::Result::Chime;
        check("letting go starts the hold over", !chimed);
    }
    {
        EntrySequence e;
        for (int i = 0; i < EntrySequence::kHoldTicks; ++i)
            e.tick(true);
        e.press(Up);
        e.press(Menu); // wrong
        check("a wrong button gives it up", e.failed() && e.press(Cancel) == EntrySequence::Result::None);
    }
    {
        EntrySequence e;
        for (int i = 0; i < EntrySequence::kHoldTicks; ++i)
            e.tick(true);
        for (int i = 0; i < EntrySequence::kSequenceTicks + 2; ++i)
            e.tick(false);
        check("too slow gives it up", e.failed());
    }

    // button test
    {
        ButtonTest t;
        press(t, Left);
        check("a right press: SUCCESS and on to the next", t.verdict == 1 && t.step == 1 && t.lit == Left);
        idle(t, kVerdictTicks + 1);
        check("the verdict goes after 0.5 s", t.verdict == 0 && t.lit == -1);
        press(t, Down);
        check("a wrong press: FAIL with the sound", t.verdict == 2 && t.failSound && t.step == 1);
        idle(t, kVerdictTicks + 1);
        check("and the test starts over", t.step == 0 && !t.failed);
    }
    {
        ButtonTest t;
        press(t, Left);
        press(t, Right); // while the verdict shows: it ends the verdict and counts as the next press
        check("a press during the verdict is taken as the next", t.step == 2 && t.verdict == 1);
    }
    {
        ButtonTest t;
        for (int ev = 0; ev < 7; ++ev)
        {
            press(t, ev);
            idle(t, kVerdictTicks + 1);
        }
        check("seven in order: done", t.done());
        press(t, Left);
        check("after the end a press says nothing", t.verdict == 0);
    }
    {
        ButtonTest t;
        for (int i = 0; i < kExitHoldTicks - 1; ++i)
            t.tick(-1, true);
        check("not out before 2 s of Cancel", !t.exit);
        t.tick(-1, true);
        check("holding Cancel for 2 s leaves", t.exit);
        ButtonTest u;
        for (int i = 0; i < 100; ++i)
            u.tick(-1, i % 50 != 0);
        check("letting go of Cancel starts the hold over", !u.exit);
    }
    check("the lit button on the controller: Select is the long one", buttonIconSprite(Select) == 0x9ad && buttonIconSprite(Up) == 0x9ae && buttonIconSprite(-1) == -1);
    check("and its place", buttonIconX(Cancel) == 0x5b && buttonIconY(Up) == 8 && buttonIconY(Select) == 4);

    // sound test
    {
        SoundTest s;
        s.prev();
        check("Left from sound 0 goes to the last song", s.id == 0x68 && s.isSong() && s.songIndex() == 8);
        s.next();
        check("and Right wraps to 0", s.id == 0 && !s.isSong());
        s.id = 0x5f;
        s.next();
        check("0x60 is the first song", s.isSong() && s.songIndex() == 0);
        s.togglePlayAll();
        s.started();
        bool early = false;
        for (int i = 0; i < kPlayAllGapTicks; ++i)
            early |= s.tick(false);
        check("play all waits a second", !early);
        check("then moves on once the sound is over", s.tick(false));
        s.started();
        bool whilePlaying = false;
        for (int i = 0; i < 100; ++i)
            whilePlaying |= s.tick(true);
        check("but not while it is still playing", !whilePlaying);
    }

    std::printf(fails ? "FAILED\n" : "ALL PASS\n");
    return fails;
}
