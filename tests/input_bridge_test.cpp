// input_bridge_test.cpp - a controller and the mouse as the game's actions: the key events they turn into, what they hold, the rules of the stick, the mouse's right button.
#define SDL_MAIN_HANDLED
#include "../src/engine/input/input_bridge.hpp"
#include <iostream>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                   \
    do                                                             \
    {                                                              \
        if (!(c))                                                  \
        {                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                              \
        }                                                          \
    } while (0)

static SDL_Event button(Uint32 type, Uint8 b)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.cbutton.button = b;
    return e;
}
static SDL_Event axis(Uint8 ax, Sint16 v)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis = ax;
    e.caxis.value = v;
    return e;
}
static SDL_Event mouse(Uint32 type, Uint8 b, Uint32 at = 0)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.button.timestamp = at;
    e.button.button = b;
    return e;
}
static bool isKey(const SDL_Event &e, bool down, Action a) { return e.type == (down ? SDL_KEYDOWN : SDL_KEYUP) && e.key.keysym.scancode == SDL_Scancode(keys().key[size_t(a)]); }

int main()
{
    InputBridge b;
    keys().reset();

    // controller buttons: the key of the action, and held while down
    SDL_Event e = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A);
    CHECK(b.translate(e) && isKey(e, true, Action::Select) && b.held(Action::Select));
    e = button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_A);
    CHECK(b.translate(e) && isKey(e, false, Action::Select) && !b.held(Action::Select));
    e = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B);
    CHECK(b.translate(e) && isKey(e, true, Action::Cancel));
    e = button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_B);
    b.translate(e);
    e = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_START);
    CHECK(b.translate(e) && isKey(e, true, Action::Menu));
    e = button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_START);
    b.translate(e);
    e = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_LEFTSTICK); // a button the game has no use for
    CHECK(!b.translate(e));

    // a rebound key: the event carries the key the player chose
    keys().bind(int(Action::Select), SDL_SCANCODE_J);
    e = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A);
    CHECK(b.translate(e) && e.key.keysym.scancode == SDL_SCANCODE_J);
    e = button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_A);
    b.translate(e);
    keys().reset();

    // the stick: presses past 16000, stays down until it falls below 8000, and shares the action with the D-pad
    e = axis(SDL_CONTROLLER_AXIS_LEFTX, 12000);
    CHECK(!b.translate(e) && !b.held(Action::Right)); // not far enough
    e = axis(SDL_CONTROLLER_AXIS_LEFTX, 20000);
    CHECK(b.translate(e) && isKey(e, true, Action::Right) && b.held(Action::Right));
    e = axis(SDL_CONTROLLER_AXIS_LEFTX, 12000);
    CHECK(!b.translate(e) && b.held(Action::Right)); // between the two: still held
    e = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    CHECK(!b.translate(e)); // already held by the stick: no second press
    e = axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
    CHECK(!b.translate(e) && b.held(Action::Right)); // the D-pad still holds it
    e = button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    CHECK(b.translate(e) && isKey(e, false, Action::Right) && !b.held(Action::Right));
    e = axis(SDL_CONTROLLER_AXIS_LEFTY, -30000);
    CHECK(b.translate(e) && isKey(e, true, Action::Up));
    e = axis(SDL_CONTROLLER_AXIS_LEFTY, 30000); // swung to the other side: the first event releases Up
    CHECK(b.translate(e) && isKey(e, false, Action::Up));
    e = axis(SDL_CONTROLLER_AXIS_RIGHTX, 30000); // the right stick is not used
    CHECK(!b.translate(e));

    // KeyBinds::held asks the bridge too (the polled keys the world reads)
    {
        Uint8 none[SDL_NUM_SCANCODES] = {};
        InputBridge &g = inputBridge();
        g.controllerOn = true;
        SDL_Event d = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        CHECK(g.translate(d));
        CHECK(keys().held(none, Action::Left));
        d = button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        g.translate(d);
        CHECK(!keys().held(none, Action::Left));
        none[keys().key[size_t(Action::Left)]] = 1; // the keyboard still works
        CHECK(keys().held(none, Action::Left));
    }

    // switched off: nothing gets through
    b.controllerOn = false;
    e = button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A);
    CHECK(!b.translate(e) && !b.held(Action::Select));
    b.controllerOn = true;

    // the mouse starts off
    e = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
    CHECK(!b.translate(e) && !b.held(Action::Select));
    b.mouseOn = true;
    e = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
    CHECK(b.translate(e) && isKey(e, true, Action::Select) && b.held(Action::Select));
    e = mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT);
    CHECK(b.translate(e) && isKey(e, false, Action::Select) && !b.held(Action::Select));
    e = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_MIDDLE);
    CHECK(b.translate(e) && isKey(e, true, Action::Menu));
    e = mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_MIDDLE);
    b.translate(e);

    // the wheel: a press and its release
    e = SDL_Event{};
    e.type = SDL_MOUSEWHEEL;
    e.wheel.y = 1;
    CHECK(b.translate(e) && isKey(e, true, Action::Up));
    SDL_Event up;
    CHECK(b.takePending(up) && isKey(up, false, Action::Up) && !b.takePending(up));
    e = SDL_Event{};
    e.type = SDL_MOUSEWHEEL;
    e.wheel.y = -1;
    CHECK(b.translate(e) && isKey(e, true, Action::Down) && b.takePending(up));

    // the right button: a quick tap is Cancel; held longer it is a walk, and no Cancel
    e = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, 1000);
    CHECK(!b.translate(e));
    e = mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, 1100);
    CHECK(b.translate(e) && isKey(e, true, Action::Cancel) && b.takePending(up) && isKey(up, false, Action::Cancel));
    e = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, 2000);
    b.translate(e);
    e = mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, 2500);
    CHECK(!b.translate(e) && !b.takePending(up));

    // walking toward the cursor: the pet is in the middle of the window, 145 rows down of 240
    CHECK(InputBridge::walkWants(Action::Right, 600, 290, 640, 480) && !InputBridge::walkWants(Action::Left, 600, 290, 640, 480));
    CHECK(InputBridge::walkWants(Action::Left, 20, 290, 640, 480));
    CHECK(InputBridge::walkWants(Action::Up, 320, 20, 640, 480) && InputBridge::walkWants(Action::Down, 320, 470, 640, 480));
    CHECK(!InputBridge::walkWants(Action::Left, 320, 290, 640, 480) && !InputBridge::walkWants(Action::Up, 320, 290, 640, 480)); // on the pet: none
    CHECK(!InputBridge::walkWants(Action::Select, 600, 290, 640, 480));

    // the mouse for scripts: held buttons, a press and the wheel since the last look, whether or not the mouse plays the game
    {
        InputBridge s;
        s.mouseOn = false; // (off: it does not play, but scripts still see it)
        SDL_Event d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
        CHECK(!s.translate(d));
        d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        s.translate(d);
        SDL_Event w;
        SDL_zero(w);
        w.type = SDL_MOUSEWHEEL;
        w.wheel.y = 2;
        s.translate(w);
        w.wheel.y = -1;
        s.translate(w);
        MouseState m = s.takeMouse();
        CHECK(m.left && m.right && !m.middle && m.pressedLeft && m.pressedRight && !m.pressedMiddle && m.wheel == 1);
        CHECK(!m.inside); // (no window or renderer here)
        d = mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT);
        s.translate(d);
        m = s.takeMouse();
        CHECK(!m.left && m.right && !m.pressedLeft && !m.pressedRight && m.wheel == 0); // still held, but no longer new
    }

    // window position -> picture position (a window of the dummy video driver with a software renderer stands in for the real one)
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) == 0)
    {
        auto near = [](float a, float b) { return a > b - 0.6f && a < b + 0.6f; };
        SDL_Window *w1 = SDL_CreateWindow("t", 0, 0, 640, 480, 0);
        SDL_Renderer *r1 = w1 ? SDL_CreateRenderer(w1, -1, SDL_RENDERER_SOFTWARE) : nullptr;
        if (r1)
            SDL_RenderSetLogicalSize(r1, 320, 240);
        CHECK(r1 != nullptr);
        if (r1)
        {
            float x = 0, y = 0;
            InputBridge::pictureFromWindow(r1, 320, 240, x, y);
            CHECK(near(x, 160) && near(y, 120));
            InputBridge::pictureFromWindow(r1, 0, 0, x, y);
            CHECK(near(x, 0) && near(y, 0));
            InputBridge::pictureFromWindow(r1, 639, 479, x, y);
            CHECK(near(x, 320) && near(y, 240));
            SDL_DestroyRenderer(r1);
        }
        if (w1)
            SDL_DestroyWindow(w1);
        SDL_Window *w2 = SDL_CreateWindow("t", 0, 0, 852, 480, 0); // 16:9: bars either side of the picture
        SDL_Renderer *r2 = w2 ? SDL_CreateRenderer(w2, -1, SDL_RENDERER_SOFTWARE) : nullptr;
        CHECK(r2 != nullptr);
        if (r2)
        {
            SDL_RenderSetLogicalSize(r2, 427, 240); // what the wide mode leaves set
            float x = 0, y = 0;
            InputBridge::pictureFromWindow(r2, 426, 240, x, y);
            CHECK(near(x, 160) && near(y, 120));
            InputBridge::pictureFromWindow(r2, 106 + 640 / 2, 120, x, y); // in the middle of the picture, a quarter of the way down
            CHECK(near(x, 160) && near(y, 60));
            InputBridge::pictureFromWindow(r2, 0, 240, x, y); // over the left bar: left of the picture
            CHECK(x < 0);
            int lw = 0, lh = 0;
            SDL_RenderGetLogicalSize(r2, &lw, &lh);
            CHECK(lw == 427 && lh == 240); // the border setting is put back
            SDL_DestroyRenderer(r2);
        }
        if (w2)
            SDL_DestroyWindow(w2);
        SDL_Quit();
    }

    // the buttons can be given other roles: left = Select, right = Cancel (not a walk), middle = Menu; or a button that only walks, or does nothing
    {
        InputBridge r;
        r.mouseOn = true;
        r.role[0] = MouseRole::Select;
        r.role[1] = MouseRole::Menu;
        r.role[2] = MouseRole::Cancel;
        SDL_Event d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, 100);
        CHECK(r.translate(d) && isKey(d, true, Action::Cancel) && r.held(Action::Cancel)); // at once, no waiting to tell a tap from a hold
        d = mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, 900);
        CHECK(r.translate(d) && isKey(d, false, Action::Cancel) && !r.held(Action::Cancel));
        r.role[1] = MouseRole::None;
        d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_MIDDLE);
        CHECK(!r.translate(d) && !r.held(Action::Menu));
        r.role[0] = MouseRole::Walk; // a walking button gives no key events and holds nothing but directions (which need a cursor over a window: none here)
        d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
        CHECK(!r.translate(d) && !r.held(Action::Select));
        d = mouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT);
        CHECK(!r.translate(d) && !r.takePending(up));
        r.role[0] = MouseRole::Menu; // two actions on two buttons at once
        r.role[2] = MouseRole::Select;
        d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
        CHECK(r.translate(d) && isKey(d, true, Action::Menu));
        d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        CHECK(r.translate(d) && isKey(d, true, Action::Select) && r.held(Action::Menu) && r.held(Action::Select));
        // the wheel can be turned off
        r.wheelOn = false;
        SDL_Event w;
        SDL_zero(w);
        w.type = SDL_MOUSEWHEEL;
        w.wheel.y = 1;
        CHECK(!r.translate(w));
    }

    // scripts steering the input: a Select press from a script (then its release), actions held for a few steps, the mouse blocked for a step
    {
        InputBridge s;
        s.mouseOn = true;
        auto st = s.stepInject();
        CHECK(!st.press && !st.release);
        s.injectSelect();
        st = s.stepInject();
        CHECK(st.press && !st.release);
        st = s.stepInject();
        CHECK(!st.press && st.release);
        st = s.stepInject();
        CHECK(!st.press && !st.release);
        CHECK(!s.held(Action::Menu));
        s.injectAction(Action::Menu, 2);
        CHECK(s.held(Action::Menu));
        s.tickInject();
        CHECK(s.held(Action::Menu));
        s.tickInject();
        CHECK(!s.held(Action::Menu));
        // blocking: the mouse's left button gives no Select for the step, the keyboard-side sources are untouched
        SDL_Event d = mouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
        s.translate(d);
        CHECK(s.held(Action::Select));
        s.blockMouse(Action::Select);
        CHECK(!s.held(Action::Select));
        s.clearBlocks();
        CHECK(s.held(Action::Select));
    }

    // other events pass through untouched
    e = SDL_Event{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = SDL_SCANCODE_Q;
    CHECK(b.translate(e) && e.key.keysym.scancode == SDL_SCANCODE_Q);

    if (g_fail)
        return 1;
    std::cout << "input_bridge_test: ok\n";
    return 0;
}
