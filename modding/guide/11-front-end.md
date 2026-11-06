# The front end (title screens)

The title menu, the intro, choosing a pet, naming it, picking a house, loading a game, Options and Mods are the **front end**. Mods can change much of it the same way they change the game.

## Scripts run there too

Your scripts are loaded when the front end starts, and `Game.hooks()` ticks (`onTickStart`, `onTickEnd`), `onHud` and `onPostDraw` run on its screens as well (the tick counts from the start of the front end). A **scene** can be opened over any screen: it gets the keys and draws the whole screen.

```lua
local G = Game.hooks()
function G:onTickStart(ctx)
  if ctx.tick == 3 then scene.open("mymod:scene/splash") end
end

local S = Scene.hooks("mymod:scene/splash")
function S:onTick(ctx)
  if ctx.input.z or ctx.input.x then scene.close() end
end
function S:onDraw(g)
  g.clear({ 10, 40, 90 })
  g.text("mymod:str/splash", 30, 40)
end
```

While a scene is open it has the keys; the front end's own screen waits behind it. Shaders and `onPostDraw` drawing work over the title screens too.

## Systems

| System | When |
|---|---|
| `frontend.boot` | the logos are about to show (once per run): change how long they hold, or skip them |
| `frontend.menu` | the player chose on the title menu (`choice` = `"new"`, `"load"`, `"options"` or `"mods"`) |
| `frontend.intro` | the intro ("Next you will...") is over |
| `frontend.starter` | Z on a pet in the starter room |
| `frontend.name` | the name is finished (change it, or cancel to stay on the screen) |
| `frontend.house` | Z on a house of the slot screen |
| `frontend.load` | a saved pet was picked on the load screen |
| `frontend.start` | the game is about to start from a slot |
| `frontend.options` | a row of the Options screen changed |

The **Systems** reference lists the `ctx` fields of each. Cancelling (`return false` in `before`) stops the front end doing what it was about to.

## Options rows

`options.add` puts your own settings on the Options screen; see the options page.

## Words

Every line of text the front end draws is a text record with the id `base:str/fe/<key>`. Reword one with a `replace` record:

```json
[ { "type": "text", "mode": "replace", "id": "base:str/fe/back", "text": "Return" } ]
```

The keys:

{{FE_STRINGS}}

(`{select}`, `{menu}` and `{cancel}` in the Mods screen's help lines are replaced by the player's key names.)

## The Mods screen

See the Mods screen page. It is part of the front end; a mod can reword it with the `mods_*` texts above.
