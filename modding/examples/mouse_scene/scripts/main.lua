-- input.mouse() gives the cursor in the picture's pixels (0..320 x 0..240) and the buttons. This opens a small screen from the world (middle click)
-- with three buttons: one lights up under the cursor, a left click on it counts.
local G = Game.hooks()
local S = Scene.hooks("mouse_scene:scene/main")

local buttons = {
  { x = 40,  y = 90, w = 60, h = 40, text = "mouse_scene:str/a" },
  { x = 130, y = 90, w = 60, h = 40, text = "mouse_scene:str/b" },
  { x = 220, y = 90, w = 60, h = 40, text = "mouse_scene:str/c" },
}
local counts = { 0, 0, 0 }
local clicks = 0
local mx, my = 0, 0

local function over(b, m)
  return m.inside and m.x >= b.x and m.x < b.x + b.w and m.y >= b.y and m.y < b.y + b.h
end

-- in the world: a middle click opens the screen
function G:onTickStart(ctx)
  if not scene.isOpen() and input.mouse().pressed.middle then scene.open("mouse_scene:scene/main") end
end

function S:onTick(ctx)
  local m = ctx.mouse            -- the same table input.mouse() returns
  mx, my = math.floor(m.x), math.floor(m.y)
  if m.pressed.left then
    for i, b in ipairs(buttons) do
      if over(b, m) then counts[i] = counts[i] + 1; clicks = clicks + 1 end
    end
  end
  if ctx.input.x or m.pressed.right then scene.close() end
end

function S:onDraw(g)
  g.clear({ 16, 32, 72 })
  g.text("mouse_scene:str/title", 160, 30, { align = "center" })
  local m = input.mouse()
  for i, b in ipairs(buttons) do
    local lit = over(b, m)
    g.rect(b.x, b.y, b.w, b.h, lit and { 240, 200, 60 } or { 80, 110, 200 })
    g.text(b.text, b.x + b.w / 2, b.y + 14, { align = "center" })
    g.text("mouse_scene:str/count", b.x + b.w / 2, b.y + 50, { align = "center", vars = { n = counts[i] } })
  end
  g.text("mouse_scene:str/pos", 160, 180, { align = "center", vars = { x = mx, y = my } })
  if m.inside then g.rect(m.x - 2, m.y - 2, 5, 5, { 255, 255, 255 }) end   -- a cursor of our own
end
