#define SDL_MAIN_HANDLED
#include "../src/game/item_tabs.hpp"
#include <cstdio>

static int fails = 0;
static void check(const char *n, bool ok)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", n);
    fails += !ok;
}

int main()
{
    using gp::ItemTabs;
    using gp::MenuSub;
    ItemTabs t;
    int cat = 0;
    check("opens from the grid on the current category", t.openList(3) && t.sub == MenuSub::ToTabs && t.tab == 3);
    t.press(ItemTabs::Up, cat);
    check("input is ignored while sliding", t.tab == 3);
    for (int i = 0; i < 100 && t.sliding(); ++i)
        t.tick(6);
    check("the slide ends on the tab list", t.inTabs());
    t.press(ItemTabs::Up, 3);
    t.press(ItemTabs::Up, 3);
    check("Up moves up through the tabs", t.tab == 1);
    for (int i = 0; i < 6; ++i)
        t.press(ItemTabs::Up, 3);
    check("and stops at the first", t.tab == 0);
    for (int i = 0; i < 9; ++i)
        t.press(ItemTabs::Down, 3);
    check("Down stops at the last", t.tab == gp::kTabCount - 1);
    check("releasing without a press does nothing", !t.release(cat));
    t.press(ItemTabs::Select, 3);
    check("Select shows pressed", t.pressed);
    check("release picks the tab's category", t.release(cat) && cat == 4 && t.sub == MenuSub::ToGrid);
    for (int i = 0; i < 100 && t.sliding(); ++i)
        t.tick(6);
    check("and slides back to the grid", t.inGrid());
    t.openList(2);
    for (int i = 0; i < 100 && t.sliding(); ++i)
        t.tick(6);
    t.press(ItemTabs::Down, 2);
    t.press(ItemTabs::Cancel, 2);
    check("Cancel gives the list up and restores the category's tab", t.sub == MenuSub::ToGrid && t.tab == 2);
    ItemTabs g;
    check("a connected mini-pet blocks the list", !g.openList(0, true));
    check("the icon shows for the first frames of a slide", [&]
          { ItemTabs s; s.openList(0); return s.slideShowsIcon(); }());
    std::printf(fails ? "FAILED\n" : "ALL PASS\n");
    return fails;
}
