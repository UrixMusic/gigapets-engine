# Scripting

Scripts are Lua 5.4 files in `mods/<id>/scripts/`. They are loaded in file name order when the game starts. The top level of a script only **registers things**: hooks, options rows, a scene. Nothing in the game happens until the game calls a hook.

## Hooks

A hook is a function on a table you get from the game. The game calls it when something happens and gives it one argument, `ctx`, describing the moment.

```lua
local G = Game.hooks()                  -- game-wide events
function G:onEnterArea(ctx)
  log("entered " .. ctx.area.id)
end

local bob = Npc.hooks("mymod:npc/bob")  -- one NPC
function bob:onTalk(ctx)
  say("mymod:str/bob_hi")
end
```

`Game.hooks()`, `Npc.hooks(id)`, `Area.hooks(id)`, `Item.hooks(id)` and `Scene.hooks(id)` each give *your mod's own* table, so two mods hooking the same thing never overwrite each other: both run, in load order. The **Hooks** reference lists every hook name and what its `ctx` holds. A name the game does not know is reported by `--validate-mods` (a typo like `onEnterAera` is found there, not by silence in the game).

### Cancelling

Some hooks are "before" hooks (`onBeforeDig`, `onBeforeBuy`, `onBeforeExit`, `onBeforeItemUse` ...): `return false` cancels what the game was about to do. Hooks that only tell you something happened ignore what they return.

## Waiting

Every hook runs as a coroutine, so it can wait without freezing the game:

- `say(textId [, vars [, opts]])`: a dialogue box; the script carries on when it is closed.
- `ask(textId ...)`: a yes / no box; gives `true` or `false`.
- `wait(ticks)`: pause for some game ticks (30 a second).

A hook that waits has taken over from the game's own behaviour (as if it returned `false`) and carries on later. At most 8 scripts can be waiting at once; more are refused and reported.

## What a script may use

Scripts run in a sandbox: Lua's `string`, `table`, `math` and `utf8` libraries, and the game's own API (see **Lua API**). There is no `io`, `os`, `debug`, `package`, `load` or `dofile`, and `math.random` is not there: use `rand.int(lo, hi)` and `rand.pick(list)` so the game's randomness stays in one place. A script that runs too long (a million instructions in one call) is stopped with an error, and a script that uses too much memory is stopped too.

`print(...)` and `log(...)` write to the game's log (the console), with the mod's id in front.

## Trusted mods

A mod can say `"trusted": true` in its manifest. Its scripts then also get `io`, `os`, `debug`, `coroutine`, `load`, `loadfile`, `dofile`, `collectgarbage` and `require("name")` (which loads `scripts/name.lua` of the same mod; dots are folders). Nothing is sandboxed any more: the mod can do whatever the player's account can. The Mods screen shows a trusted mod as trusted, so players know. Use it only when you need it (a mod that reads a file from disk, say).

## Errors

An error in a script is logged with the mod, the hook and the line, and the hook is skipped; the game goes on. `--validate-mods` loads every script and checks hook names, so run it often.

## Keeping values

- `save.get(key)` / `save.set(key, value)`: kept in the player's save slot.
- `profile.get(key)` / `profile.set(key, value)`: kept for the whole game, across slots.
- `player.setFlag` / `counter.*`: flags and counters that belong to the pet's story (quests can watch them).

See the saves page for how they are stored and versioned.

## Drawing and scenes

`onHud`, `onPostWorld`, `onPostDraw` and a scene's `onDraw` get a drawing object `g`. The graphics page covers it, and `scene.open(id)` / `scene.close(result)` open and close a screen of your own (a scene's `onTick` gets the keys). A scene can be opened from the front end too (see the front end page).
