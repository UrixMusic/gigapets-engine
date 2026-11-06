// script_mouse_test.cpp - input.mouse() hands a script the mouse the game last gave it (position in picture pixels, buttons held, buttons pressed, wheel).
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
    // a system hook is the way to read a value back out in a test: whatever it puts in ctx comes back
    CHECK(h.runString("a",
                      "local L = System.hooks('room.leave')\n"
                      "function L:before(ctx)\n"
                      "  local m = input.mouse()\n"
                      "  ctx.x = m.x; ctx.y = m.y\n"
                      "  ctx.inside = m.inside and 1 or 0\n"
                      "  ctx.held = (m.left and 1 or 0) + (m.right and 2 or 0) + (m.middle and 4 or 0)\n"
                      "  ctx.pressed = (m.pressed.left and 1 or 0) + (m.pressed.right and 2 or 0) + (m.pressed.middle and 4 or 0)\n"
                      "  ctx.wheel = m.wheel\n"
                      "end\n"));
    auto read = [&](const gp::MouseState &m)
    {
        h.setMouse(m);
        gp::SystemCtx c;
        c.set("location", 0).set("x", -1).set("y", -1).set("inside", -1).set("held", -1).set("pressed", -1).set("wheel", -1);
        h.system("room.leave", c, [] {});
        return c;
    };
    {
        gp::MouseState m;
        m.inside = true;
        m.x = 160.5f;
        m.y = 120;
        m.left = true;
        m.pressedLeft = true;
        m.wheel = -2;
        gp::SystemCtx c = read(m);
        CHECK(c.num("x") == 160.5 && c.num("y") == 120 && c.num("inside") == 1);
        CHECK(c.num("held") == 1 && c.num("pressed") == 1 && c.num("wheel") == -2);
    }
    {
        gp::MouseState m; // outside the picture: the position can go past its ends
        m.x = -12;
        m.y = 300;
        m.right = true;
        m.middle = true;
        m.pressedRight = true;
        gp::SystemCtx c = read(m);
        CHECK(c.num("x") == -12 && c.num("y") == 300 && c.num("inside") == 0);
        CHECK(c.num("held") == 6 && c.num("pressed") == 2 && c.num("wheel") == 0);
    }
    CHECK(h.errors().empty());
    std::printf(fails ? "script_mouse_test: %d failure(s)\n" : "script_mouse_test: ok\n", fails);
    return fails ? 1 : 0;
}
