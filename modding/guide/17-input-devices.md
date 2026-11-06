# Controllers, the mouse and input

The game is played with seven actions: **Up, Down, Left, Right, Select, Cancel and Menu**. The keyboard keys for them can be changed on the Options screen (the defaults are the arrow keys, Z, X and Enter). A game controller and the mouse are two more ways to give the same seven actions, built into the game. Nothing in a mod needs to know about them.

## What the player can use

| Device | Does |
|---|---|
| **Controller** (on by default) | D-pad and the left stick: directions. A: Select. B: Cancel. X or Start: Menu. Plug one in at any time. |
| **Mouse** (off by default) | Left button: Select. Middle button: Menu. Wheel: Up / Down in menus. **Right button**: a quick tap is Cancel; hold it and the pet walks toward the cursor (the pet stands in the middle of the window). |

The **Controller** and **Mouse** rows on the Options screen switch each one. The mouse starts off so that a click that only focuses the window cannot pick a menu entry.

If the player has rebound a key (Select on `J`, say), the controller and the mouse still work: they press whatever key the action is bound to.

## For mods

- **Scripts get the same actions.** A scene's `ctx.input` and `ctx.held` (and a game tick hook's) hold `z`, `x`, `left`, `right`, `up` and `down`. They are true for a controller or a mouse the same as for a keyboard, so a mod written for keys works with the other devices as it is.
- **Scripts can read the mouse itself.** `input.mouse()` (and `ctx.mouse` in a scene's `onTick` and the tick hooks) gives the cursor and the buttons:

```lua
local m = input.mouse()
-- m.x, m.y      the cursor in the picture's pixels: 0..320 across, 0..240 down
-- m.inside      true while the cursor is over the picture (not the border bars or outside the window)
-- m.left, m.right, m.middle        true while the button is held
-- m.pressed.left / .right / .middle  true in the one step the button went down
-- m.wheel       notches since the last step (up is positive)
```

  It works whatever the **Mouse** option says: that option is only whether the mouse also plays the game. The position is already corrected for the window size, the 4:3 or 16:9 mode and the border bars, so a button drawn at (40, 90) is at `m.x` 40..100 whatever size the window is. `modding/examples/mouse_scene/` is a screen with three buttons that light up under the cursor and count clicks (middle click opens it in a game).
- **Reading the settings.** The two switches are `controller` and `mouse` in the settings file (`settings.txt` in the data folder, `1` or `0`). A script cannot change them.
- **A native mod can add more.** A device the game does not handle (a touch screen, a different button layout) can be added by replacing `gp::KeyBinds::held`, which the game asks about every held key. See the Native mods page for a mouse example built that way before it was part of the game.

## How it works

Every key the game reads is read in one of two ways, and the bridge feeds both:

- **Key events** (menus, the title screen) go through `gp::pollEvent`, a stand-in for `SDL_PollEvent`. A controller or mouse event comes back as the key event of the key the action is bound to.
- **Held keys** (walking, Z and X in the world) are asked for through `KeyBinds::held`, which says yes for the keyboard *or* for a controller or mouse holding that action.

The stick presses a direction past about half its travel and lets go below a quarter, so it does not flicker at the edge. A direction held on the D-pad and on the stick at once is one press. The right button's tap-or-hold is decided by how long it was down when the game saw it (the time the system gave the click, not the time the game got round to it, so a slow moment does not turn a hold into a tap).
