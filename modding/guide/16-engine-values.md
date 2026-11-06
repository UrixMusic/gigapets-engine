# Engine values and the game's clock

`engine.get(name)` and `engine.set(name, value)` read and change numbers the game exposes: the pet's position, the camera, and the **tuning** numbers that were fixed in the code before (how far a dig reaches, how fast the stats wear down, how long an NPC stands still). `engine.list()` gives every name, and `engine.describe(name)` its note. Set them in `Game.hooks():onLoad`, or any time; a change takes effect where the value is used. They are not saved: a mod that wants a value kept saves it itself (`save.set`).

```lua
function G:onLoad(ctx)
  engine.set("tuning.digReach", 60)   -- dig twice as far
  engine.set("tuning.tickHz", 45)     -- the game runs one and a half times as fast
end
```

## The clock: `tuning.tickHz`

`tuning.tickHz` is how many steps the game takes a second (30 in the original). The game runs faster or slower with it, and every duration follows one rule:

- **Counted in steps**: walking speed (pixels a step), an NPC's idle time (`tuning.idleMinTicks`), animations, how long the pet tries to reach an NPC. These speed up with `tickHz`, so the whole game runs faster.
- **Counted in seconds or ROM ticks**: how fast hunger and sleepiness wear down, the thought bubbles (hungry, sick, tired, unhappy), the smell check beside a smelly NPC, the shops restocking, the quest reminder, quest time limits and the game clock. These **keep their length in real seconds**, whatever `tickHz` is. A hunger drop that takes 67 s still takes 67 s at 60 steps a second.

So raising `tickHz` makes the pet walk and the world move faster without making it starve faster. Scripts' 30 Hz hooks (`onTick`) are called once per step, so they follow `tickHz` too.

## Every exposed value

{{ENGINE_VALUES}}
