# Options screen rows

A mod adds its own settings to the title's Options screen, above Done, with `options.add` at the top level of one of its scripts (only while the scripts load).

```lua
local function num(key, default) local v = profile.get(key); return v and tonumber(v) or default end

options.add("mymod:option/glow", {           -- the id starts with the mod's prefix
  label = "mymod:str/glow",                  -- a text record: the row's name
  kind  = "toggle",                          -- "toggle" (0 / 1), "choice" (an index from 0) or "slider" (a number)
  get = function() return num("glow", 0) end, -- the value now
  set = function(v) profile.set("glow", tostring(v)) end, -- the new value, after the player changed the row
})
options.add("mymod:option/style", { label = "mymod:str/style", kind = "choice", choices = { "mymod:str/soft", "mymod:str/strong" }, get = ..., set = ... })
options.add("mymod:option/power", { label = "mymod:str/power", kind = "slider", min = 0, max = 100, step = 10, get = ..., set = ... })
```
- Left / Right (and Z on a switch or a choice) change a row: a switch flips, a choice cycles through `choices` (2 to 8 text ids), a slider steps by `step` within `min`..`max`.
- Keep the value with `profile.get` / `profile.set` (saved for the whole game when the player leaves the screen), or anywhere else a script can: the game only calls `get` and `set`.
- At most 8 rows in all mods. A row whose `get` or `set` fails (a script error) is logged and drops out of the screen.
- The front end's own words (intro, house and load screens, Options labels, On / Off ...) are texts `base:str/fe/<key>` (list: `src/game/frontend/fe_strings.hpp`); reword one with a text record `{"type":"text","mode":"replace","id":"base:str/fe/back","text":"Return"}`.
- `System.hooks("frontend.options")` (before / replace / after) sees every change of every row, the game's own too: `ctx.row` is "music", "sfx", "volume", "tutorials", "interpolate", "scale",
  "scale_mode", "window_mode", "aspect", "retro", "live", a mod row's id, or "" for a key row; `ctx.forward` (1 = Right or Z) and `ctx.by_z`. Cancelling it stops the change.
- The same values are what the mod's game scripts read with `profile.get`: a setting set on the title screen is there in the game.
