# Controllers, the mouse and input

The game is played with seven actions: **Up, Down, Left, Right, Select, Cancel and Menu**. The keyboard keys for them can be changed on the Options screen (the defaults are the arrow keys, Z, X and Enter). A game controller and the mouse are two more ways to give the same seven actions, built into the game. Nothing in a mod needs to know about them.

## What the player can use

| Device | Does |
|---|---|
| **Controller** (on by default) | D-pad and the left stick: directions. A: Select. B: Cancel. X or Start: Menu. Plug one in at any time. |
| **Mouse** (off by default) | Each button does what the Options say (below). The wheel is Up / Down in menus. |

The **Controller** and **Mouse** rows on the Options screen switch each one.

### What the mouse buttons do

With the mouse on, three more rows set the **Left**, **Right** and **Middle** button, and two switch the **Mouse Wheel** and **Menu Pointing**. Each button can be:

| Choice | Does |
|---|---|
| Nothing | nothing |
| Select / Cancel / Menu | that action (Z, X, the Menu key) while the button is held |
| Walk | holding it walks the pet toward the cursor |
| Cancel / Walk | a quick press is Cancel; held, the pet walks toward the cursor |

The defaults are Left = Select, Middle = Menu, Right = Cancel / Walk. A player who steers with the keyboard would set Right to Cancel. The choices are saved as `mouseLeft`, `mouseMiddle`, `mouseRight` (0 to 5, in the order above) and `mouseWheel` in `settings.txt`. The mouse starts off so that a click that only focuses the window cannot pick a menu entry.

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

## Pointing at menus

With **Menu Pointing** on (it is on by default whenever the mouse is), the mouse can use the game's menus directly:

- **Hover** highlights the entry under the cursor. Moving the mouse is what moves the highlight, so the keyboard still steers when the mouse is still.
- **Click** (the button that is Select) presses it. A click on nothing does nothing, instead of pressing whatever was highlighted.
- It works on the menu bar, the item grid, the category tabs, Yes / No / OK boxes, and on the title screens: the main menu, Load Game, the house choice, Options and the Mods screen (including the native code question).

Turn Menu Pointing off and the mouse only sends its buttons as actions: a click anywhere is Select, as before.

## For mods: `ui`

A mod can do its own pointing, with its own rules, or build something else on the same information: the menu bar, the item grid and its category tabs, the Yes / No / OK buttons, and the title screens' menus.

- **`ui.targets()`** says what can be pointed at right now, or `nil`. It returns `{ kind, selected, items }`: `kind` is `"bar"`, `"items"` (the cells that hold an item), `"tabs"` (while the category list is open) or `"dialogue"` (a box's buttons: Yes and No, or OK; a box with no button gets one target covering the screen). On the title screens it is `"menu"`, `"load"`, `"house"`, `"options"`, `"mods"` or `"mods_confirm"`. `items` is a list of `{ x, y, w, h }` in the picture's pixels, the same space as `input.mouse()`, and `selected` is the 1-based number of the highlighted one.
- **`ui.focus(i)`** moves the highlight to item `i` (1-based). It makes no sound and presses nothing. It returns false if there is no such item.
- **`ui.activate()`** presses Select on the highlighted item: the next step sees the press, the one after the release.
- **`input.block("select")`** (also `"cancel"`, `"menu"`): for that one step the mouse does not give the action. Call it in a tick hook while you handle a click yourself, so the game's own "left click is Select" does not press as well. The keyboard and the controller are not affected.

```lua
function G:onTickStart(ctx)
  local t, m = ui.targets(), input.mouse()
  if not t then return end                 -- nothing to point at: the mouse plays as the Options say
  if m.left then input.block("select") end -- the click is ours
  for i, r in ipairs(t.items) do
    if m.inside and m.x >= r.x and m.x < r.x + r.w and m.y >= r.y and m.y < r.y + r.h then
      if t.selected ~= i then ui.focus(i) end   -- hover
      if m.pressed.left then ui.activate() end  -- click
    end
  end
end
```

`modding/examples/mouse_ui/` is that, complete: hover to highlight, click to press. Once a script calls `ui.focus`, `ui.activate` or `input.block`, the game leaves its own pointing alone, so a mod like this replaces it rather than doubling it. Scripts see only the game's own screens this way: a scene of a mod reads `input.mouse()` itself. (To test a menu without a mouse, set `GP_WALK_KEYS="60:menu,200:right,260:select"` in the environment: it presses those actions at those steps.)

## How it works

Every key the game reads is read in one of two ways, and the bridge feeds both:

- **Key events** (menus, the title screen) go through `gp::pollEvent`, a stand-in for `SDL_PollEvent`. A controller or mouse event comes back as the key event of the key the action is bound to.
- **Held keys** (walking, Z and X in the world) are asked for through `KeyBinds::held`, which says yes for the keyboard *or* for a controller or mouse holding that action.

The stick presses a direction past about half its travel and lets go below a quarter, so it does not flicker at the edge. A direction held on the D-pad and on the stick at once is one press. The right button's tap-or-hold is decided by how long it was down when the game saw it (the time the system gave the click, not the time the game got round to it, so a slow moment does not turn a hold into a tap).
