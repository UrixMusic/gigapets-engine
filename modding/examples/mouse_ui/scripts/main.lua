-- Point and click the game's own menus. (The game does this itself when the Mouse and Menu Pointing options are on; this is how a mod would do it with its own rules,
-- and once a mod calls ui.focus / ui.activate / input.block the game leaves its own pointing alone.) ui.targets() says what can be pointed at (with rectangles in the picture's pixels, like input.mouse()),
-- ui.focus(i) moves the highlight and ui.activate() presses Select on it. While one of those screens is up this mod handles the left button itself,
-- so input.block("select") stops the game's own "left click = Select" from pressing as well (it would press whatever is highlighted, even on a click in empty space).
local G = Game.hooks()

local lastX, lastY = -1, -1

-- the 1-based number of the target under the cursor, or nil
local function under(t, m)
  if not m.inside then return nil end
  for i, r in ipairs(t.items) do
    if m.x >= r.x and m.x < r.x + r.w and m.y >= r.y and m.y < r.y + r.h then return i end
  end
  return nil
end

function G:onTickStart(ctx)
  local t = ui.targets()
  local m = input.mouse()
  if not t then
    lastX, lastY = -1, -1
    return                                   -- nothing to point at: the mouse plays as the Mouse option says
  end
  if m.left then input.block("select") end   -- a click here is ours, not the game's Select
  local i = under(t, m)
  if i then
    -- hover moves the highlight, but only when the mouse moved (or clicked): so the keyboard can still steer
    if (m.x ~= lastX or m.y ~= lastY or m.pressed.left) and t.selected ~= i then ui.focus(i) end
    if m.pressed.left then ui.activate() end
  end
  lastX, lastY = m.x, m.y
end
