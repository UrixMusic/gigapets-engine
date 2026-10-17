// name_entry.hpp -- state 0x12 (ShowPetNameEntryScreen / HandleNameEntryInput), see front-end notes section 3.
// Depends only on gp::Gfx (menu_draw.hpp). NameEntry itself is pure logic.
#pragma once
#include <string>

#include "menu_draw.hpp"

namespace gp
{

    struct NameEntry
    {
        enum class In
        {
            Left,
            Right,
            Up,
            Down,
            Confirm
        };
        struct Result
        {
            bool typed = false; // a key was pressed -> show pressed cursor frame for 0.2 s
            bool done = false;  // Done accepted, name is final
        };

        static constexpr int kCols = 9, kRows = 3, kMax = 9;

        bool onGrid = true;
        int row = 0, col = 0;
        std::string name;
        bool lowercaseAfterFirst = true; // decompiled rule (+0x20 unless first char / after a space); unverified in-game

        static bool nonBlank(const std::string &s) { return s.find_first_not_of(' ') != std::string::npos; }
        static int rowLen(int r) { return r == 2 ? 8 : kCols; }

        Result step(In in)
        {
            Result res;
            switch (in)
            {
            case In::Left:
            case In::Right:
            {
                const int d = in == In::Right ? 1 : -1;
                if (onGrid)
                {
                    const int n = rowLen(row);
                    col = ((col + d) % n + n) % n;
                }
                else
                    col = ((col + d) % 3 + 3) % 3;
                break;
            }
            case In::Up:
                if (onGrid)
                {
                    if (row == 0)
                    {
                        onGrid = false;
                        col = col / 3;
                    }
                    else
                        --row;
                }
                else
                {
                    onGrid = true;
                    row = 2;
                    col = col * 3 + 1;
                }
                break;
            case In::Down:
                if (onGrid)
                {
                    if (row == 2)
                    {
                        onGrid = false;
                        col = col / 3;
                    }
                    else
                    {
                        ++row;
                        if (col >= rowLen(row))
                            col = rowLen(row) - 1;
                    }
                }
                else
                {
                    onGrid = true;
                    row = 0;
                    col = col * 3 + 1;
                }
                break;
            case In::Confirm:
                if (onGrid)
                {
                    if (int(name.size()) < kMax)
                    {
                        char ch = char('A' + row * kCols + col);
                        if (lowercaseAfterFirst && !name.empty() && name.back() != ' ')
                            ch = char(ch + 0x20);
                        name.push_back(ch);
                        res.typed = true;
                    }
                }
                else if (col == 0) // Back
                {
                    if (!name.empty())
                    {
                        name.pop_back();
                        res.typed = true;
                    }
                }
                else if (col == 1) // Space
                {
                    if (!name.empty() && int(name.size()) < kMax)
                    {
                        name.push_back(' ');
                        res.typed = true;
                    }
                }
                else // Done: needs a non-blank name (vanilla quirk with all-space names is refused)
                {
                    if (nonBlank(name))
                    {
                        res.typed = true;
                        res.done = true;
                    }
                }
                break;
            }
            return res;
        }
    };

    // Draw list from notes section 3. layer 0x3000.
    // ASSUMPTION: Gfx::drawTextBox(x, y, w, h, text, style, pal, mode) -- adjust to the real signature.
    inline void drawNameEntry(Gfx &g, const NameEntry &n, int species, bool pressed)
    {
        constexpr int L = 0x3000, BG = 0x5dd, KP = 0x2ce;
        static const int colX[9] = {0x1f, 0x3a, 0x57, 0x72, 0x8e, 0xaa, 0xc6, 0xe1, 0xff};
        static const int rowY[3] = {0x6a, 0x88, 0xa5};
        static const int specX[3] = {0x21, 0x76, 0xc9};

        if (n.onGrid)
            g.drawTile(pressed ? 0x663 : 0x662, KP, colX[n.col], rowY[n.row], L);
        else
            g.drawTile(pressed ? 0x661 : 0x660, KP, specX[n.col], 0xc4, L);

        g.drawTile(0x65f, BG, 0x26, 0x72, L);
        if (!NameEntry::nonBlank(n.name))
            g.drawTile(0x65e, BG, 0xd7, 0xcc, L);

        // portrait: species 3 ("none") falls into the species-2 branch like the game
        if (species == 0)
            g.drawTile(0x65d, BG, 0x22, 0x16, L);
        else if (species == 1)
            g.drawTile(0x65c, BG, 0x0e, 0x19, L);
        else
            g.drawTile(0x65b, BG, 0x0d, 0x05, L);

        g.drawTextBox(3, 0x104, 0, 0x36, n.name, 0x140, 1, L);
    }

} // namespace gp