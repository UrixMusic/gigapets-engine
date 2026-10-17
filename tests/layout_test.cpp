// tests/layout_test.cpp   (needs -Isrc/engine)
#include <cstdio>
#include "text_layout.hpp"

static int fails = 0;
static void check(const char* what, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fails++; }

int main() {
    using namespace gp;
    auto adv = [](unsigned char) { return 8; };   // every glyph 8 px wide; a space costs 10
    const int lh = 14;

    { auto L = layoutText("aaa bbb", 0, 0, 100, Align::Left, lh, adv);
      check("fits on one line", L.lines.size() == 1 && L.lines[0].text == "aaa bbb"); }
    { auto L = layoutText("aaa bbb", 0, 0, 40, Align::Left, lh, adv);
      check("wraps at the space", L.lines.size() == 2 && L.lines[0].text == "aaa" && L.lines[1].text == "bbb");
      check("second line is one line height down", L.lines.size() == 2 && L.lines[1].y == lh); }
    { auto L = layoutText("aaa", 0, 0, 24, Align::Left, lh, adv);
      check("a word exactly as wide as the line does not fit (strict <): hard break",
            L.lines.size() == 2 && L.lines[0].text == "aa" && L.lines[1].text == "a"); }
    { auto L = layoutText("aa", 0, 0, 100, Align::Center, lh, adv);
      check("centre: x = (100 - 16) / 2", L.lines.size() == 1 && L.lines[0].x == 42); }
    { auto L = layoutText("aa", 0, 0, 100, Align::Right, lh, adv);
      check("right: x = 100 - 16", L.lines.size() == 1 && L.lines[0].x == 84); }
    { auto L = layoutText("a\n\nb", 0, 0, 100, Align::Left, lh, adv);
      check("a blank line keeps its height", L.lines.size() == 2 && L.lines[1].y == 2 * lh && L.height == 3 * lh); }
    { auto L = layoutText("  hi", 5, 7, 100, Align::Left, lh, adv);
      check("leading spaces are skipped", L.lines.size() == 1 && L.lines[0].text == "hi" &&
            L.lines[0].x == 5 && L.lines[0].y == 7); }
    { auto L = layoutText("aa bb", 0, 0, 100, Align::Left, lh, adv, 30, 14);
      check("indent shifts lines in the first indentH pixels", L.lines.size() == 1 && L.lines[0].x == 30); }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}