#pragma once
#include <cstdlib>
#include <string>
#include <vector>

namespace gp {

enum class Align { Left = 0, Center = 1, Right = 2 };

struct LaidLine { std::string text; int x, y; };
struct TextLayout { std::vector<LaidLine> lines; int height = 0; };   // height = CalculateWrappedTextHeight

// Port of DrawFormattedText / MeasureTextWordFit. adv(c) = advance of one character (10 for a space).
// The first `indentH` pixels of height use a line `abs(indent)` narrower, shifted right when indent > 0.
template <class Adv>
TextLayout layoutText(const std::string& s, int x, int y, int width, Align align,
                      int lineH, Adv adv, int indent = 0, int indentH = 0) {
    TextLayout out;
    const int y0 = y;
    const size_t n = s.size();
    auto at = [&](size_t i) -> unsigned char { return i < n ? (unsigned char)s[i] : 0; };
    size_t p = 0;
    for (;;) {
        while (at(p) == ' ') p++;               // leading spaces are free
        if (at(p) == 0) break;
        const size_t lineStart = p;
        const bool inIndent = y < y0 + indentH;
        int remaining = inIndent ? width - std::abs(indent) : width;

        for (int word = 0;; word++) {           // fit words until the line ends
            while (at(p) == ' ') { remaining -= 10; p++; }
            int used = 0;
            size_t q = p;
            bool ended = false, overflow = false;
            for (;;) {                          // MeasureTextWordFit
                const unsigned char c = at(q);
                if (c == '\n') { ended = true; q++; break; }
                if (c == 0)    { ended = true; break; }
                if (c == ' ')  break;
                const int a = adv(c);
                if (used + a < remaining) { used += a; q++; }
                else { overflow = true; ended = true; break; }
            }
            if (!overflow || word == 0) { remaining -= used; p = q; }   // a first word is hard-broken, not moved
            if (ended) break;
        }

        size_t end = p;                         // trailing spaces/newlines are not drawn
        while (end > lineStart && (at(end - 1) == ' ' || at(end - 1) == '\n')) {
            if (at(end - 1) == ' ') remaining += 10;
            end--;
        }
        if (remaining < width) {                // the original compares against the full width
            int lx = x;
            if (align == Align::Right) lx += remaining;
            else if (align == Align::Center) lx += remaining / 2;
            if (inIndent && indent > 0) lx += indent;
            out.lines.push_back({s.substr(lineStart, end - lineStart), lx, y});
        }
        y += lineH;
        if (p == lineStart) p++;                // safety: a glyph wider than the line would loop forever
    }
    out.height = y - y0;
    return out;
}

} // namespace gp