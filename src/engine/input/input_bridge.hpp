// input_bridge.hpp - a game controller and the mouse, as the actions the game is played with (Up, Down, Left, Right, Select, Cancel, Menu).
//
// The game reads the keyboard in two ways: key events (menus, the title screen) and the held keys it asks about every step (walking, Z and X in the
// world, KeyBinds::held). This bridge feeds both from other devices, so nothing else in the game has to know about them:
//   - pollEvent() is SDL_PollEvent, except that controller and mouse events come back as the key events of the keys the player has bound to the actions;
//   - held() (used by KeyBinds::held) says whether a controller or the mouse is holding an action down right now.
//
//   Controller: D-pad and the left stick = directions, A = Select, B = Cancel, X or Start = Menu. (Plug it in any time.)
//   Mouse:      left button = Select, middle button = Menu, wheel = Up / Down in menus.
//               Right button: a tap = Cancel; hold it and the pet walks toward the cursor (the pet stands in the middle of the window).
// Each device can be switched off in the options (Settings::controller / mouse); the mouse starts off, so a click that only focuses the window does nothing.
#pragma once
#include <SDL2/SDL.h>
#include <deque>
#include <functional>
#include "keybinds.hpp"
#include "mouse_state.hpp"

namespace gp
{
    // What a mouse button does while the mouse plays the game
    enum class MouseRole : int
    {
        None = 0,
        Select,     // the Select action (Z)
        Cancel,     // Cancel (X)
        Menu,       // Menu (C)
        Walk,       // held: the pet walks toward the cursor
        CancelWalk, // a quick press is Cancel; held, the pet walks toward the cursor
        kCount
    };
    constexpr int actionOfRole(MouseRole r)
    {
        return r == MouseRole::Select ? int(Action::Select) : r == MouseRole::Cancel ? int(Action::Cancel) : r == MouseRole::Menu ? int(Action::Menu) : -1;
    }

    class InputBridge
    {
    public:
        bool controllerOn = true, mouseOn = false;
        MouseRole role[3] = {MouseRole::Select, MouseRole::Menu, MouseRole::CancelWalk}; // left, middle, right button
        bool wheelOn = true;                                                           // the wheel is Up / Down
        // Pointing at menus (Settings::mousePointing). The screen that is showing sets these (the walking world, the title screens): onMove(x, y) as the cursor moves over the
        // picture (move the highlight to what is under it), and onClick(x, y) when the button that is Select goes down: false = it is not on anything, do not press Select.
        bool pointingOn = true;
        std::function<void(float, float)> onMove;
        std::function<bool(float, float)> onClick;
        static constexpr Uint32 kHoldMs = 200;                   // a right button held this long is a walk, shorter is a tap (Cancel)
        static constexpr int kStickOn = 16000, kStickOff = 8000; // the stick presses past the first and releases below the second

        // ---- polled: what the devices hold now ----
        bool held(Action a) const
        {
            const int i = int(a);
            if (injectSteps_[i] > 0)
                return true;
            if (controllerOn && pad_[i])
                return true;
            if (!mouseOn)
                return false;
            const bool direction = a != Action::Select && a != Action::Cancel && a != Action::Menu;
            for (int b = 0; b < 3; ++b)
            {
                if (!playDown_[b] || (direction ? false : (blocked_ & (1u << i)) != 0))
                    continue;
                if (!direction)
                {
                    if (actionOfRole(role[b]) == i)
                        return true;
                }
                else if ((role[b] == MouseRole::Walk || (role[b] == MouseRole::CancelWalk && SDL_GetTicks() - playSince_[b] >= kHoldMs)) && cursorWants(a))
                    return true;
            }
            return false;
        }

        // ---- events ----
        // Turns one event into a key event if it is controller or mouse input. Returns false if the event is used up (deliver nothing); true if `e` (possibly
        // rewritten) is for the game. The releases of taps wait in takePending().
        bool translate(SDL_Event &e)
        {
            trackMouse(e); // for scripts (input.mouse()): the mouse as it is, whether or not it plays the game
            switch (e.type)
            {
            case SDL_CONTROLLERDEVICEADDED:
                openPad(e.cdevice.which);
                return false;
            case SDL_CONTROLLERDEVICEREMOVED:
                closePad(e.cdevice.which);
                return false;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
            {
                if (!controllerOn)
                    return false;
                const bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
                int a = -1;
                switch (e.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    a = int(Action::Up);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    a = int(Action::Down);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    a = int(Action::Left);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    a = int(Action::Right);
                    break;
                case SDL_CONTROLLER_BUTTON_A:
                    a = int(Action::Select);
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    a = int(Action::Cancel);
                    break;
                case SDL_CONTROLLER_BUTTON_X:
                case SDL_CONTROLLER_BUTTON_START:
                    a = int(Action::Menu);
                    break;
                default:
                    return false;
                }
                return setPad(a, down, e);
            }
            case SDL_CONTROLLERAXISMOTION:
            {
                if (!controllerOn || (e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTX && e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTY))
                    return false;
                const bool horiz = e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX;
                const int neg = int(horiz ? Action::Left : Action::Up), pos = int(horiz ? Action::Right : Action::Down);
                const int v = e.caxis.value;
                // at most one edge per event; a stick that swings from one side to the other takes two events, the first releases
                for (int a : {neg, pos})
                {
                    const int limit = stick_[a] ? kStickOff : kStickOn;
                    const bool want = a == neg ? v < -limit : v > limit;
                    if (want != stick_[a])
                    {
                        stick_[a] = want;
                        return setPad(a, want, e, true);
                    }
                }
                return false;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                if (!mouseOn)
                    return false;
                const bool down = e.type == SDL_MOUSEBUTTONDOWN;
                const int b = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_MIDDLE ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : -1;
                if (b < 0)
                    return false;
                const MouseRole r = role[b];
                const bool wasDown = playDown_[b];
                const Uint32 since = playSince_[b];
                if (!down && swallowed_[b]) // its press was not on anything: the release is not either
                {
                    swallowed_[b] = false;
                    return false;
                }
                if (down && r == MouseRole::Select && pointingOn && onClick && renderer)
                {
                    float px = 0, py = 0;
                    pictureFromWindow(renderer, e.button.x, e.button.y, px, py);
                    if (!onClick(px, py))
                    {
                        swallowed_[b] = true;
                        return false;
                    }
                }
                playDown_[b] = down && r != MouseRole::None;
                if (down)
                    playSince_[b] = e.common.timestamp; // when it happened, not when we got round to it
                if (const int a = actionOfRole(r); a >= 0)
                {
                    makeKey(e, Action(a), down);
                    return true;
                }
                if (r == MouseRole::CancelWalk && !down && wasDown && e.common.timestamp - since < kHoldMs)
                {
                    tapKey(e, Action::Cancel); // a quick press was a tap
                    return true;
                }
                return false; // Walk and the start of a CancelWalk: the held state is all that matters
            }
            case SDL_MOUSEMOTION:
            {
                if (mouseOn && pointingOn && onMove && renderer)
                {
                    float px = 0, py = 0;
                    pictureFromWindow(renderer, e.motion.x, e.motion.y, px, py);
                    onMove(px, py);
                }
                return true;
            }
            case SDL_MOUSEWHEEL:
            {
                if (!mouseOn || !wheelOn || e.wheel.y == 0)
                    return false;
                tapKey(e, e.wheel.y > 0 ? Action::Up : Action::Down);
                return true;
            }
            default:
                return true;
            }
        }

        // The events that are waiting (the releases of taps)
        bool takePending(SDL_Event &e)
        {
            if (pending_.empty())
                return false;
            e = pending_.front();
            pending_.pop_front();
            return true;
        }

        // Which directions the cursor asks for, for a window of this size and a cursor here: the pet stands in the middle, 145 rows down of 240.
        static bool walkWants(Action a, int mx, int my, int ww, int wh)
        {
            const int dx = mx - ww / 2, dy = my - wh * 145 / 240, dead = wh / 14;
            switch (a)
            {
            case Action::Left:
                return dx < -dead;
            case Action::Right:
                return dx > dead;
            case Action::Up:
                return dy < -dead;
            case Action::Down:
                return dy > dead;
            default:
                return false;
            }
        }

        // ---- scripts steering the input (ui.activate(), input.block()) ----
        // A script pressed Select on something it can see (the menu bar, a box): the next step sees a press, and the step after a release.
        void injectSelect() { injectStage_ = 2; }
        struct Inject
        {
            bool press = false, release = false;
        };
        Inject stepInject() // once a step
        {
            Inject r;
            if (injectStage_ == 2)
            {
                r.press = true;
                injectStage_ = 1;
            }
            else if (injectStage_ == 1)
            {
                r.release = true;
                injectStage_ = 0;
            }
            return r;
        }
        // A tap of this action's key for the screens that read key events (the title screens): ui.activate() there
        void pushTap(Action a)
        {
            SDL_Event dn;
            SDL_zero(dn);
            makeKey(dn, a, true);
            SDL_Event up = dn;
            makeKey(up, a, false);
            pending_.push_back(dn);
            pending_.push_back(up);
        }
        // For this step the mouse does not give this action (a script is handling the click itself). The controller and keyboard are not affected.
        void blockMouse(Action a) { blocked_ |= 1u << int(a); }
        void clearBlocks() { blocked_ = 0; }
        // Holds an action for this many steps (the debug harness GP_WALK_KEYS uses it to press buttons in a test run)
        void injectAction(Action a, int steps = 2) { injectSteps_[int(a)] = steps; }
        void tickInject()
        {
            for (int &n : injectSteps_)
                if (n > 0)
                    --n;
        }

        // ---- the mouse for scripts ----
        SDL_Renderer *renderer = nullptr; // the game's, to turn a window position into the picture's
        // A position in the window (pixels) as a position in the game picture (320 x 240), however the window is scaled and bordered.
        static void pictureFromWindow(SDL_Renderer *ren, int wx, int wy, float &x, float &y)
        {
            int lw = 0, lh = 0;
            SDL_RenderGetLogicalSize(ren, &lw, &lh);
            SDL_RenderSetLogicalSize(ren, 320, 240); // the game picture, whatever the 16:9 mode has set for the border
            SDL_RenderWindowToLogical(ren, wx, wy, &x, &y);
            SDL_RenderSetLogicalSize(ren, lw, lh);
        }
        // The mouse now, in the picture's pixels (320 x 240), and what happened since the last call (a press, the wheel); the "since" part starts over.
        MouseState takeMouse()
        {
            MouseState m;
            m.left = rawDown_[0];
            m.middle = rawDown_[1];
            m.right = rawDown_[2];
            m.pressedLeft = rawPressed_[0];
            m.pressedMiddle = rawPressed_[1];
            m.pressedRight = rawPressed_[2];
            m.wheel = rawWheel_;
            rawPressed_[0] = rawPressed_[1] = rawPressed_[2] = false;
            rawWheel_ = 0;
            SDL_Window *w = SDL_GetMouseFocus();
            if (!w || !renderer)
                return m;
            int wx = 0, wy = 0;
            SDL_GetMouseState(&wx, &wy);
            pictureFromWindow(renderer, wx, wy, m.x, m.y);
            m.inside = m.x >= 0 && m.x < 320 && m.y >= 0 && m.y < 240;
            return m;
        }

        // ---- controllers ----
        void openPad(int index)
        {
            if (padHandle_ || !SDL_IsGameController(index))
                return;
            padHandle_ = SDL_GameControllerOpen(index);
            if (padHandle_)
                padId_ = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(padHandle_));
        }
        void closePad(SDL_JoystickID id)
        {
            if (padHandle_ && id == padId_)
            {
                SDL_GameControllerClose(padHandle_);
                padHandle_ = nullptr;
                for (int a = 0; a < kActionCount; ++a)
                    pad_[a] = stick_[a] = dpad_[a] = false;
            }
        }
        void openAll() // controllers that are already plugged in
        {
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                openPad(i);
        }

    private:
        bool pad_[kActionCount]{}, stick_[kActionCount]{}, dpad_[kActionCount]{};
        bool rawDown_[3]{}, rawPressed_[3]{}; // left, middle, right
        int rawWheel_ = 0;
        int injectSteps_[kActionCount]{};
        unsigned blocked_ = 0;    // actions the mouse does not give this step
        int injectStage_ = 0;     // 2: a Select press is due, 1: its release
        bool swallowed_[3]{};     // the press of this button was not on anything (pointing): no key events for it
        bool playDown_[3]{};      // the buttons held, while the mouse plays (left, middle, right)
        Uint32 playSince_[3]{};   // when each went down
        std::deque<SDL_Event> pending_;
        SDL_GameController *padHandle_ = nullptr;
        SDL_JoystickID padId_ = -1;

        bool cursorWants(Action a) const
        {
            SDL_Window *w = SDL_GetMouseFocus();
            if (!w)
                return false;
            int mx = 0, my = 0, ww = 1, wh = 1;
            SDL_GetMouseState(&mx, &my);
            SDL_GetWindowSize(w, &ww, &wh);
            return walkWants(a, mx, my, ww, wh);
        }
        void trackMouse(const SDL_Event &e)
        {
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
            {
                const int i = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_MIDDLE ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : -1;
                if (i < 0)
                    return;
                rawDown_[i] = e.type == SDL_MOUSEBUTTONDOWN;
                if (rawDown_[i])
                    rawPressed_[i] = true;
            }
            else if (e.type == SDL_MOUSEWHEEL)
                rawWheel_ += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
        }
        // The key event of an action: the key the player has bound to it
        static void makeKey(SDL_Event &e, Action a, bool down)
        {
            const Uint32 ts = e.common.timestamp;
            SDL_zero(e);
            e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
            e.key.timestamp = ts;
            e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
            e.key.repeat = 0;
            e.key.keysym.scancode = SDL_Scancode(keys().key[size_t(a)]);
            e.key.keysym.sym = SDL_GetKeyFromScancode(e.key.keysym.scancode);
        }
        // a press now and its release next
        void tapKey(SDL_Event &e, Action a)
        {
            makeKey(e, a, true);
            SDL_Event up = e;
            makeKey(up, a, false);
            pending_.push_back(up);
        }
        // fromStick: the change is the stick's; the action is held while the D-pad or the stick holds it
        bool setPad(int a, bool down, SDL_Event &e, bool fromStick = false)
        {
            if (!fromStick)
                dpad_[a] = down;
            const bool was = pad_[a];
            pad_[a] = dpad_[a] || stick_[a];
            if (pad_[a] == was)
                return false; // the other still holds it: no new event
            makeKey(e, Action(a), pad_[a]);
            return true;
        }
    };

    inline InputBridge &inputBridge()
    {
        static InputBridge b;
        static const bool installed = (extraHeld() = [](Action a)
                                       { return inputBridge().held(a); },
                                       true);
        (void)installed;
        return b;
    }

    // SDL_PollEvent, with controller and mouse input turned into key events (see the top of this file)
    inline int pollEvent(SDL_Event *e)
    {
        if (inputBridge().takePending(*e))
            return 1;
        while (SDL_PollEvent(e))
            if (inputBridge().translate(*e))
                return 1;
        return 0;
    }
} // namespace gp
