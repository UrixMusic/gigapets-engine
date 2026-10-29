// tests/trick_guess_test.cpp - the Trick Teacher's code game (RunTrickGuessingMinigame @ 0x02f1cd).
#include <cstdio>
#include "../src/game/player.hpp"
#include "../src/game/save_player.hpp"
#include "../src/game/trick_guess.hpp"
using namespace gp;
using TG = TrickGuess;
using Ev = TrickGuess::Event;

// Press button b (0 left, 1 right, 2 up, 3 down) for one tick.
static Ev press(TG &g, int b)
{
    return g.tick(b == 0, b == 1, b == 2, b == 3, false);
}
static Ev idle(TG &g) { return g.tick(false, false, false, false, false); }

// Ticks until the game leaves the result screen: the ROM shows it for > 3.5 s (105 sim ticks).
static int waitOut(TG &g, Ev &ev)
{
    int n = 0;
    while (g.open() && n < 1000)
    {
        ev = idle(g);
        ++n;
    }
    return n;
}

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    const std::array<int, 5> code{2, 0, 3, 1, 1};

    { // cracking a level-1 code: 3 right presses, text changes, waits 3.5 s, then finishes solved
        TG g;
        g.start(code, 3, 3);
        check("starts open with the intro text", g.open() && g.text() == TG::Text::Intro);
        check("first press is right", press(g, 2) == Ev::None && g.justRight() && !g.justWrong() && g.guessCount() == 1);
        press(g, 0);
        check("slot 0 marked right", g.markRight(0) && g.markRight(1));
        press(g, 3);
        check("cracked text after the last slot", g.text() == TG::Text::Cracked && g.solved());
        Ev ev = Ev::None;
        const int n = waitOut(g, ev);
        check("finishes solved", ev == Ev::Finished && g.solved());
        std::printf("      result screen lasted %d ticks (expect 105)\n", n);
        check("result screen is 105 ticks (3.5 s)", n == TG::kDelayTicks);
    }

    { // a wrong code costs an attempt, same code again, and the texts count down
        TG g;
        g.start(code, 3, 3);
        press(g, 1);
        check("wrong press plays the wrong sound", g.justWrong() && !g.justRight());
        press(g, 1);
        press(g, 1);
        check("wrong text after a failed attempt", g.text() == TG::Text::Wrong);
        check("not solved", !g.solved());
        Ev ev = Ev::None;
        for (int i = 0; i < TG::kDelayTicks + 1 && ev == Ev::None; ++i)
            ev = idle(g);
        check("keeps going after a failed attempt", ev == Ev::None && g.open());
        idle(g);
        check("attempt 2 shows '2 more tries'", g.text() == TG::Text::TriesLeft && g.triesLeft() == 2 && g.guessCount() == 0);
        // second attempt: wrong again, third: the last try text
        for (int a = 0; a < 2; ++a)
        {
            press(g, 1);
            press(g, 1);
            press(g, 1);
            ev = Ev::None;
            for (int i = 0; i < TG::kDelayTicks + 2 && ev == Ev::None; ++i)
                ev = idle(g);
            if (a == 0)
                check("attempt 3 shows the '1 more try' text", g.text() == TG::Text::LastTry && g.triesLeft() == 1);
        }
        check("out of attempts finishes unsolved", ev == Ev::Finished && !g.solved() && !g.open());
    }

    { // the code is the SAME on the next attempt, so it can be solved by elimination
        TG g;
        g.start(code, 4, 4);
        Ev ev = Ev::None;
        for (int b : {2, 0, 3, 3}) // last one wrong
            press(g, b);
        for (int i = 0; i < TG::kDelayTicks + 2; ++i)
            ev = idle(g);
        for (int b : {2, 0, 3, 1})
            press(g, b);
        check("second attempt can crack it", g.text() == TG::Text::Cracked);
        waitOut(g, ev);
        check("finished solved on attempt 2", ev == Ev::Finished && g.solved());
    }

    { // CANCEL leaves immediately and is not solved; so does CANCEL mid-attempt
        TG g;
        g.start(code, 5, 3);
        press(g, 2);
        const Ev ev = g.tick(false, false, false, false, true);
        check("cancel ends the game", ev == Ev::Cancelled && !g.open() && !g.solved());
        check("closed game ignores input", press(g, 2) == Ev::None);
    }

    { // the press on the very first tick already counts (the ROM handles state 0 and the press in one pass)
        TG g;
        g.start(code, 3, 3);
        press(g, 2);
        check("press on the first tick counts", g.guessCount() == 1);
    }

    { // cancel is ignored while the result is showing (the ROM only polls it in the guessing state)
        TG g;
        g.start(code, 3, 3);
        press(g, 2);
        press(g, 0);
        press(g, 3);
        const Ev ev = g.tick(false, false, false, false, true);
        check("cancel during the result is ignored", ev == Ev::None && g.open());
    }

    { // slot layout (DrawGuessResultSlots)
        int base = 0, pitch = 0;
        TG::slotLayout(3, base, pitch);
        check("3 slots: 0x5a, pitch 0x46", base == 0x5a && pitch == 0x46);
        TG::slotLayout(4, base, pitch);
        check("4 slots: 0x37, pitch 0x46", base == 0x37 && pitch == 0x46);
        TG::slotLayout(5, base, pitch);
        check("5 slots: 0x32, pitch 0x37", base == 0x32 && pitch == 0x37);
    }

    { // player: the code is rerolled 0..3 and survives a save
        Player p;
        int n = 0;
        p.rerollTrickCode([&](int lo, int hi)
                          { return lo + (n++ % (hi - lo)); });
        check("reroll fills 5 values in 0..3", p.trickCodeRolled && p.trickCode[0] == 0 && p.trickCode[3] == 3 && p.trickCode[4] == 0);
        SaveData s;
        savePlayer(p, s);
        Player q;
        loadPlayer(s, q);
        check("trick code round-trips through the save", q.trickCodeRolled && q.trickCode == p.trickCode);
        Player fresh;
        SaveData s2;
        savePlayer(fresh, s2);
        Player r;
        loadPlayer(s2, r);
        check("an unrolled code stays unrolled", !r.trickCodeRolled);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
