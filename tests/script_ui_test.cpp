// script_ui_test.cpp - ui.targets(), ui.focus(), ui.activate() and input.block() as scripts see them, against a fake screen.
#include "../src/mods/script_host.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                             \
    do                                                       \
    {                                                        \
        if (!(c))                                            \
        {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while (0)

int main()
{
    gp::ScriptHost h;
    gp::UiScreen screen;
    int focused = -1, activated = 0;
    std::string blocked;
    gp::UiHooks hooks;
    hooks.screen = [&] { return screen; };
    hooks.focus = [&](int i)
    {
        if (i < 0 || i >= int(screen.items.size()))
            return false;
        focused = i;
        screen.selected = i;
        return true;
    };
    hooks.activate = [&] { ++activated; };
    hooks.block = [&](const std::string &n)
    {
        if (n != "select" && n != "cancel" && n != "menu")
            return false;
        blocked += n + ";";
        return true;
    };
    h.setUi(hooks);
    // a system hook hands values back to the test
    CHECK(h.runString("a",
                      "local L = System.hooks('room.leave')\n"
                      "function L:before(ctx)\n"
                      "  local t = ui.targets()\n"
                      "  ctx.has = t and 1 or 0\n"
                      "  if t then\n"
                      "    ctx.n = #t.items; ctx.sel = t.selected; ctx.x = t.items[2].x; ctx.w = t.items[2].w\n"
                      "    ctx.kindIsBar = t.kind == 'bar' and 1 or 0\n"
                      "    ctx.focus3 = ui.focus(3) and 1 or 0; ctx.focus9 = ui.focus(9) and 1 or 0; ctx.focus0 = ui.focus(0) and 1 or 0\n"
                      "    ui.activate(); ui.activate()\n"
                      "    ctx.b1 = input.block('select') and 1 or 0; input.block('menu'); ctx.b2 = input.block('walk') and 1 or 0\n"
                      "  end\n"
                      "end\n"));
    auto run = [&]
    {
        gp::SystemCtx c;
        c.set("location", 0).set("has", -1).set("n", -1).set("sel", -1).set("x", -1).set("w", -1).set("kindIsBar", -1).set("focus3", -1).set("focus9", -1).set("focus0", -1).set("b1", -1).set("b2", -1);
        h.system("room.leave", c, [] {});
        return c;
    };
    { // nothing to point at
        gp::SystemCtx c = run();
        CHECK(c.num("has") == 0);
        CHECK(focused == -1 && activated == 0);
    }
    screen.kind = "bar";
    screen.selected = 0;
    screen.items = {{15, 193, 32, 40}, {56, 193, 32, 40}, {97, 193, 32, 40}};
    {
        gp::SystemCtx c = run();
        CHECK(c.num("has") == 1 && c.num("n") == 3 && c.num("sel") == 1); // 1-based for Lua
        CHECK(c.num("x") == 56 && c.num("w") == 32 && c.num("kindIsBar") == 1);
        CHECK(c.num("focus3") == 1 && focused == 2); // item 3 (1-based) is index 2
        CHECK(c.num("focus9") == 0 && c.num("focus0") == 0);
        CHECK(activated == 2);
        CHECK(c.num("b1") == 1 && c.num("b2") == 0 && blocked == "select;menu;");
    }
    CHECK(h.errors().empty());
    std::printf(fails ? "script_ui_test: %d failure(s)\n" : "script_ui_test: ok\n", fails);
    return fails ? 1 : 0;
}
