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
#include "keybinds.hpp"

namespace gp
{
    class InputBridge
    {
    public:
        bool controllerOn = true, mouseOn = false;
        static constexpr Uint32 kHoldMs = 200;                   // a right button held this long is a walk, shorter is a tap (Cancel)
        static constexpr int kStickOn = 16000, kStickOff = 8000; // the stick presses past the first and releases below the second

        // ---- polled: what the devices hold now ----
        bool held(Action a) const
        {
            const int i = int(a);
            if (controllerOn && pad_[i])
                return true;
            if (!mouseOn)
                return false;
            if (mouse_[i])
                return true;
            return rightDown_ && a != Action::Select && a != Action::Cancel && a != Action::Menu && walking(a);
        }

        // ---- events ----
        // Turns one event into a key event if it is controller or mouse input. Returns false if the event is used up (deliver nothing); true if `e` (possibly
        // rewritten) is for the game. The releases of taps wait in takePending().
        bool translate(SDL_Event &e)
        {
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
                if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    if (down)
                    {
                        rightDown_ = true;
                        rightSince_ = e.common.timestamp; // when it happened, not when we got round to it
                        return false; // not yet known to be a tap or a walk
                    }
                    const bool tap = rightDown_ && e.common.timestamp - rightSince_ < kHoldMs;
                    rightDown_ = false;
                    if (!tap)
                        return false;
                    tapKey(e, Action::Cancel);
                    return true;
                }
                int a = -1;
                if (e.button.button == SDL_BUTTON_LEFT)
                    a = int(Action::Select);
                else if (e.button.button == SDL_BUTTON_MIDDLE)
                    a = int(Action::Menu);
                if (a < 0)
                    return false;
                mouse_[a] = down;
                makeKey(e, Action(a), down);
                return true;
            }
            case SDL_MOUSEWHEEL:
            {
                if (!mouseOn || e.wheel.y == 0)
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
        bool pad_[kActionCount]{}, mouse_[kActionCount]{}, stick_[kActionCount]{}, dpad_[kActionCount]{};
        bool rightDown_ = false;
        Uint32 rightSince_ = 0;
        std::deque<SDL_Event> pending_;
        SDL_GameController *padHandle_ = nullptr;
        SDL_JoystickID padId_ = -1;

        bool walking(Action a) const
        {
            if (SDL_GetTicks() - rightSince_ < kHoldMs)
                return false;
            SDL_Window *w = SDL_GetMouseFocus();
            if (!w)
                return false;
            int mx = 0, my = 0, ww = 1, wh = 1;
            SDL_GetMouseState(&mx, &my);
            SDL_GetWindowSize(w, &ww, &wh);
            return walkWants(a, mx, my, ww, wh);
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
