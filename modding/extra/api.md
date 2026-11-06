### api:say
say(textId [, vars [, { portrait = npcId }]])
Shows a dialogue box with the text `textId` (a text record's id, e.g. `mymod:str/hello`). `vars` fills the text's placeholders. `portrait` shows that NPC's face in the box. Inside a hook the script waits until the player has closed the box. Returns false if the box could not be shown.

### api:ask
ask(textId [, vars [, { portrait = npcId }]]) -> true | false
A yes / no box. Only inside a hook (it waits for the answer): `if ask("mymod:str/swim") then ... end`. Same `vars` and `portrait` as `say`. Returns false at once if no box could be queued.

### api:Game.hooks
Game.hooks() -> table
The calling mod's table for the game-wide hooks. Put functions on it: `local G = Game.hooks(); function G:onLoad(ctx) ... end`. Every mod gets its own table, so mods never overwrite each other's hooks. Only at the top level of a script.

### api:Npc.hooks
Npc.hooks(npcId) -> table
The calling mod's table for one NPC (`function T:onInteract(ctx)`, `onTick`, ...). See the Hooks page.

### api:Area.hooks
Area.hooks(areaId) -> table
The calling mod's table for one area (`onEnter`, `onLeave`, `onTick`, `onHud`).

### api:Item.hooks
Item.hooks(itemId) -> table
The calling mod's table for one item (`onItemUse`).

### api:Scene.hooks
Scene.hooks(sceneId) -> table
The calling mod's table for a scene: a screen the mod draws itself (`onOpen`, `onTick`, `onDraw`). Open it with `scene.open(id)` or an object record whose action is `scene`.

### api:player.unlock
player.unlock()
Gives the pet back its movement and actions after `player.lock`.

### api:quest.exists
quest.exists(id) -> true | false
True if a side quest with this id is known (a `side_quest` record).

### api:quest.start
quest.start(id) -> true | false
Starts a side quest. False for an unknown id or one that cannot start now.

### api:quest.complete
quest.complete(id) -> true | false
Completes a side quest. False if that was not possible (an unknown id, or it is not active).

### api:quest.abandon
quest.abandon(id) -> true | false
Abandons an active side quest. False if it is not active.

### api:quest.active
quest.active() -> { id, ... }
The ids of the side quests that are active now.

### api:counter.get
counter.get(name) -> number
The counter's value (0 if it was never set). Any counter can be read.

### api:counter.set
counter.set(name, n) -> the new value
Sets a counter (kept between 0 and 99999). A script may only write counters under its own mod prefix (`mymod:slimes`). Counters are saved with the pet. A side quest's `counter` objective watches one.

### api:counter.add
counter.add(name, n = 1) -> the new value
Adds to a counter (the result is kept between 0 and 99999). Same prefix rule as `counter.set`.

### api:scene.isOpen
scene.isOpen() -> true | false
True while a mod's scene is open.

### api:world.shade
world.shade(target, shaderId | nil [, uniforms])
Puts a fragment shader on one thing in the world: `target` is `"player"`, an NPC id or an object type id; `nil` takes it off. See the graphics page (the section on layers).
