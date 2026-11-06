### hook:G:onQuestComplete
The receiver took a quest's hand-in (the objectives are already removed), before the completion dialogue. Global: it fires for table quests and generated ones alike (`ctx.quest.id` is nil for a generated one).
`ctx.quest = { id, number, generic, type }`, `ctx.giver = { id }`, `ctx.receiver = { id }`, `ctx.player`. `return false` skips the quest's built-in bonus rewards (the thank-you text and the next quest still follow).

### hook:G:onQuestDecline
The player said no to a quest offer. `ctx.quest`, `ctx.giver`, `ctx.receiver` as in `onQuestComplete`. The result is ignored. (`onQuestAccept` is the yes.)

### hook:G:onSceneClose
A scene a mod opened has closed. `ctx.scene = { id }`, `ctx.result` = what `scene.close(result)` was given (a number, string or boolean; nil if nothing).

### hook:G:onMigrate
Saved values of this mod are older than its `data_version` in manifest.json. Runs once while a game starts, before `onLoad`. `ctx.from` = the version the save was made with (1 if it has values and no version), `ctx.to` = the mod's `data_version`.
`save.get` and `save.set` work here: read the old values, write the new ones. Only the mod that raised its `data_version` is called. See the guide page on saves.

### hook:G:onHud
Every frame, over the walking world and the game's own screens (dialogue, menus; before `onPostDraw`): draw with `g.rect`, `g.sprite`, `g.text`, `g.num` and the shapes (not `g.clear`). `Area.hooks(id).onHud(g)` is the same for one area, drawn after the game-wide ones.
At most 4000 draw calls a frame. See the graphics page.

### hook:A:onHud
Like `Game.hooks():onHud(g)`, but only while the pet is in this area; drawn after the game-wide HUD.

### hook:G:onTickStart
The game-wide tick: runs first in every simulation step (30 a second), whatever is open. `ctx = { tick, dt = 1/30, area = { id }, pet = { x, y }, state = { dialogue, menu, room, encounter, scene }, input, held }`
(`state` says what is open: true means the world is not running; `input` / `held` are the keys, at the start those of the previous step). Cheap work only: it runs 30 times a second.

### hook:G:onTickEnd
Runs last in every simulation step (also when the step stopped early, for a menu or a screen change). Same `ctx` as `onTickStart`.

### hook:G:onPostWorld
Draws over the finished world (tiles, objects, NPCs and the pet) but under the HUD, menus and dialogue. The `g` is the whole drawing object (shapes, canvases, `"@frame"`, meshes, `g.shader`): this is where world-only effects go. See the graphics page.

### hook:G:onPostDraw
Draws over the finished frame, the whole window (world, HUD, menus, dialogue, other mods' drawing; `g.w` / `g.h` = 320 x 240, 426 x 240 in 16:9). `g.shader(id, uniforms)` here runs a full-screen shader over everything. See the graphics page.

### hook:S:onOpen
The scene has just opened (`scene.open(id)` from any script, or an object whose action is `scene`). If `scene.open` was given an argument it is `ctx.arg`. Set the scene up here; `wait`, `say` and `ask` work.

### hook:S:onTick
Every simulation step (30 a second) while the scene is open and the last `onTick` has finished. `ctx = { input = { z, x, left, right, up, down } (pressed this step), held = { the same, true while held }, tick, dt = 1/30 }`. Close the scene with `scene.close(result)`.

### hook:S:onDraw
Every frame while the scene is open: the whole screen is the scene's. Draw with `g` (`g.clear(c)` first). A scene that fails closes itself.
