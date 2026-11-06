# Example: the Berry Pie mod

A small complete mod, in `modding/examples/berry_pie/`. Copy the folder into `mods/` to try it: it adds a food item with its own picture, gives the player one when a game starts, and makes hunger fall half as fast. Every file is shown here.

```
berry_pie/
  manifest.json
  icon.png
  art/pie.png
  data/texts.json
  data/anims.json
  data/items.json
  scripts/main.lua
```

## manifest.json

```json
{
  "id": "berry_pie",
  "name": "Berry Pie",
  "version": "1.0",
  "game_api": "1",
  "description": "A tiny example mod: a new food item with its own picture, a welcome gift when a game loads, and slower hunger."
}
```

## data/texts.json

Everything the player reads is a text record, found by id. The item's name and plural, and the line the script will say:

```json
[
  { "type": "text", "mode": "add", "id": "berry_pie:str/pie", "text": "Berry Pie" },
  { "type": "text", "mode": "add", "id": "berry_pie:str/pies", "text": "Berry Pies" },
  { "type": "text", "mode": "add", "id": "berry_pie:str/welcome", "text": "Welcome back! Here is a pie." }
]
```

## data/anims.json

A picture is an *anim*: a list of frames, each a PNG from the mod. This one has a single frame. `anchorX` / `anchorY` is the point of the picture that sits at the position it is drawn at (0, 0 = its top-left corner, which is what an inventory icon wants).

```json
[
  { "type": "anim", "mode": "add", "id": "berry_pie:anim/pie", "frames": [ { "png": "art/pie.png", "anchorX": 0, "anchorY": 0 } ] }
]
```

## data/items.json

The item. `category` puts it under the Food tab; `use` says what eating it does (`food` raises stats by the amounts in `num`: here hunger by 20 and happiness by 5); `icon` is the anim to draw. The picture on the ground defaults to the icon.

```json
[
  { "type": "item", "mode": "add", "id": "berry_pie:item/pie", "category": "food",
    "name": "berry_pie:str/pie", "plural": "berry_pie:str/pies", "maxQty": 9, "price": 25,
    "icon": "berry_pie:anim/pie", "use": { "action": "food", "num": { "hunger": 20, "happiness": 5 } } }
]
```

The `price` also makes it something a shop can sell; the shop records on the **Data records** page show how to stock it.

## scripts/main.lua

```lua
local G = Game.hooks()

-- When a game starts: give the player a pie if they have none, and say so.
function G:onLoad(ctx)
  if player.count("berry_pie:item/pie") == 0 then
    player.give("berry_pie:item/pie", 1)
    say("berry_pie:str/welcome")
  end
end

-- Hunger falls half as fast: change the amount before the engine applies it.
local S = System.hooks("care.decay")
function S:before(ctx)
  if ctx.stat == "hunger" then ctx.amount = ctx.amount * 0.5 end
end
```

Two ideas to notice:

- `Game.hooks()` gives your mod's table; a function on it named after a hook (`onLoad`) is called when that happens. The **Hooks** reference lists them all.
- `System.hooks("care.decay")` changes how the engine itself works. `before` can edit `ctx` (the engine reads it back) or `return false` to cancel.

## Try changing it

- Make eating the pie cure illness too: look at the `use` actions on the **Data records** page (`medicine`).
- Put a text on the item with a `description`-like second line: add more `text` records and say them from an `onItemUse` hook (`Item.hooks("berry_pie:item/pie")`).
- Give the pie only once: `save.set("gifted", true)` and `save.get("gifted")` keep a value in the save (see the saves page).
