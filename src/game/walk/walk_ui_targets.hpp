// walk_ui_targets.hpp - what the walking world shows that can be pointed at (ui.targets(), ui.focus()): the menu bar, the item grid and its tabs, the buttons of a box.
// The rectangles come from the same numbers the drawing code uses (the ROM's cursor and tab tables, the dialogue layouts), and the sizes of the pictures drawn there.
// A script uses them to let the mouse point at things: hover = ui.focus(i), click = ui.activate().
#pragma once
#include <algorithm>
#include <functional>
#include "../../engine/ui_targets.hpp"
#include "../npc/dialogue.hpp"
#include "../ui/menu_bar.hpp"
#include "../ui/menu_draw.hpp"
#include "walk_draw.hpp"
#include "walk_state.hpp"

struct UiTargetsEnv
{
    // The rectangle a tile is drawn in at (x, y): { x + dx, y + dy, w, h }. w is 0 when the game has no such picture.
    std::function<gp::UiRect(int sprite, int palette, int x, int y)> tileRect;
    menubar::MenuBar &bar;
    bool &barInHouse;
    const gp::Rom &rom;
    ItemMenu &im;
    gp::Dialogue &dialogue;
    bool &statsOpen; // the pet's stats screen is up: nothing to point at
};

class UiTargets
{
public:
    explicit UiTargets(UiTargetsEnv e) : e_(std::move(e)) {}

    gp::UiScreen screen() const
    {
        gp::UiScreen s;
        if (e_.dialogue.open())
            return dialogue(s);
        if (e_.statsOpen)
            return s;
        if (e_.im.open)
        {
            if (e_.im.tabs.inTabs())
                return tabs(s);
            if (e_.im.tabs.inGrid())
                return grid(s);
            return s; // the tab list is sliding
        }
        if (e_.bar.isOpen())
            return bar(s);
        return s;
    }

    // Moves the highlight to item i of screen(). Nothing is pressed.
    bool focus(int i)
    {
        const gp::UiScreen s = screen();
        if (!s.any() || i < 0 || i >= int(s.items.size()))
            return false;
        if (s.kind == "dialogue")
        {
            if (e_.dialogue.box && e_.dialogue.box->kind == gp::BoxKind::Confirm)
                e_.dialogue.box->yesSelected = i == 0;
            return true;
        }
        if (s.kind == "tabs")
        {
            e_.im.tabs.tab = i;
            return true;
        }
        if (s.kind == "items")
        {
            const Cell c = cells()[size_t(i)];
            e_.im.selRow = e_.im.firstRow + c.row;
            e_.im.selCol = c.col + 1;
            return true;
        }
        if (s.kind == "bar")
        {
            e_.bar.select(i);
            return true;
        }
        return false;
    }

private:
    UiTargetsEnv e_;

    struct Cell
    {
        int row, col; // on screen, 0 based
    };

    gp::UiScreen bar(gp::UiScreen &s) const
    {
        s.kind = "bar";
        const int base = e_.barInHouse ? gp::kBarCursorHouse : gp::kBarCursorOutside;
        for (int i = 0; i < e_.bar.buttonCount(); ++i)
        {
            const int b = base + i * 8;
            int pal = e_.rom.u16(b + 1);
            if (pal == 0xffff)
                pal = gp::kMenuPalette;
            s.items.push_back(e_.tileRect(e_.rom.u16(b), pal, e_.rom.u16(b + 2), e_.rom.u16(b + 3)));
        }
        s.selected = e_.bar.selected();
        return s;
    }

    // The cells of the item grid that hold an item, left to right, top to bottom (3 rows show at a time)
    std::vector<Cell> cells() const
    {
        std::vector<Cell> v;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 6; ++c)
                if ((e_.im.firstRow - 1 + r) * 6 + c < int(e_.im.filtered.size()))
                    v.push_back({r, c});
        return v;
    }
    gp::UiScreen grid(gp::UiScreen &s) const
    {
        s.kind = "items";
        const std::vector<Cell> cs = cells();
        for (size_t i = 0; i < cs.size(); ++i)
        {
            const int x = 0x0f + cs[i].col * 0x32, y = 0x2f + cs[i].row * 0x32;
            gp::UiRect r = e_.tileRect(0x40a, gp::kMenuPalette, x, y); // the empty cell's picture: the size of a cell
            if (r.w <= 0)
                r = {x, y, 0x32, 0x32};
            s.items.push_back(r);
            if (cs[i].row == e_.im.selRow - e_.im.firstRow && cs[i].col == e_.im.selCol - 1)
                s.selected = int(i);
        }
        return s;
    }
    gp::UiScreen tabs(gp::UiScreen &s) const
    {
        s.kind = "tabs";
        for (int t = 0; t < gp::kTabCount; ++t)
        {
            const gp::TabRec r = gp::tabRec(e_.rom, t, 1); // the highlight of that row, as drawn while the list is open
            s.items.push_back(e_.tileRect(r.sprite, r.palette == 0xffff ? gp::kMenuPalette : r.palette, r.x, r.y));
        }
        s.selected = e_.im.tabs.tab;
        return s;
    }
    gp::UiScreen dialogue(gp::UiScreen &s) const
    {
        s.kind = "dialogue";
        const gp::DialogueBox &b = *e_.dialogue.box;
        const bool stats = b.kind == gp::BoxKind::Stats || b.kind == gp::BoxKind::StatsLow;
        const Layout &L = WorldDraw::kLayouts[stats ? 2 : std::clamp(b.layout, 0, 2)];
        if (b.kind == gp::BoxKind::Picture || b.kind == gp::BoxKind::Sign || stats)
        {
            s.items.push_back({0, 0, 320, 240}); // no button to point at: a click anywhere closes it
            s.selected = 0;
            return s;
        }
        const int y = L.y + L.btnY;
        if (b.kind == gp::BoxKind::Confirm)
        {
            s.items.push_back(e_.tileRect(b.yesSelected ? 0x4f : 0x4e, gp::kDialoguePalette, L.x + L.btnX - 0x2b, y)); // Yes
            s.items.push_back(e_.tileRect(b.yesSelected ? 0x4b : 0x4c, gp::kDialoguePalette, L.x + L.btnX, y));        // No
            s.selected = b.yesSelected ? 0 : 1;
            return s;
        }
        s.items.push_back(e_.tileRect(0x27, gp::kDialoguePalette, L.x + L.btnX, y)); // OK (a message, a quest, a quantity)
        s.selected = 0;
        return s;
    }
};
