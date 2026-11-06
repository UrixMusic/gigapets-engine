# Systems: changing how the engine works

A **system** is one thing the engine does: a stat wearing down, the pet taking a step, an NPC choosing what to do, the Options screen changing a value. Hooks react to what happens; a system lets you *change how it happens*.

`System.hooks(name)` gives your mod's table for a system, with up to three functions:

```lua
local S = System.hooks("care.decay")

function S:before(ctx)      -- runs first; may change ctx, or return false to cancel everything
  if ctx.stat == "hunger" then ctx.amount = ctx.amount * 0.5 end
end

function S:replace(ctx)     -- does the job instead of the engine (the first mod to define one wins)
end

function S:after(ctx)       -- runs once it is done
end
```

- `ctx` holds named numbers and strings (the **Systems** reference lists them for each system). The engine reads them back after `before`, so changing `ctx.amount` changes what the engine then does.
- `before` returning `false` cancels the whole thing: no default, no `replace`, no `after`.
- If a mod has a `replace`, the engine's own code is skipped and the replacement does the job; with none, the default runs with the values in `ctx`.
- A system nobody hooks costs nothing and changes nothing.
- `System.list()` returns every system with its fields, from inside a script.

## An example: slower hunger

```lua
local S = System.hooks("care.decay")
function S:before(ctx)
  if ctx.stat == "hunger" then ctx.amount = ctx.amount * 0.5 end
end
```

## Systems or hooks?

Use a **hook** to add something when an event happens (give a reward when a quest ends). Use a **system** to alter what the engine itself does (change how much a stat wears down, take over a step of the pet's movement). Many things are both: a hook tells you a quest was completed, the `quest.reward` system lets you change or cancel a step of its rewards.

The front end has systems too (`frontend.menu`, `frontend.start`, `frontend.starter`, `frontend.name`, `frontend.options`, `frontend.boot`, `frontend.load`, `frontend.house`, `frontend.intro`), listed in the same reference.
