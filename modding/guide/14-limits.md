# What mods can't do (yet)

Checked against the code on 2026-11-06. If something here is in your way, it is probably something that can be added: this list is also the list of what to ask for.

## Data records

| Record | Modes it takes |
|---|---|
| `text` | `add`, `replace`, `remove` |
| `anim` | `add`, `replace`, `remove` |
| `npc` | `add`, `replace`, `remove` |
| `item` | `add`, `replace`, `patch`, `remove` |
| `object` | `add`, `patch`, `remove` |
| `area` | `add`, `patch` |
| `species` | `add`, `patch` |
| `palette`, `shader`, `side_quest` | `add` |
| `minigame`, `room` | `patch` |

The others are described on their own entries in the Data records reference.

- **An item is withdrawn, not deleted.** `remove` takes an item out of every random find table, every shop's stock and the stock rules that put it in a shop, but the item itself stays (saves and other records may name it, and a script can still give it). `patch` is `replace`: the fields you give change, the rest stay. Items keep their numbers, so they cannot disappear from the list.
- **No new rooms.** `room` only patches the eight the game has: the three player homes (`base:room/0` to `2`) and the four hotels (`4` to `7`). Furnishing and the door's return point can change, the number of rooms cannot.
- **Minigames can be tuned, not made.** A `minigame` record changes the numbers and prize lists of one the game has. A new minigame is a **scene** in Lua (your own `onTick` / `onDraw`).
- **Areas are tile maps.** There is no image-based area format. An area is a grid of tile numbers from a tileset and a collision set.
- **No `interaction` records.** An object can show a sign text, a message, or open a scene; anything more is an `onInteract` hook.
- **No records for the pet's rules.** Tricks, care, personality and befriending are not data you can add. Change how they work with **systems** (`care.decay`, `trick.*`, `encounter.*`, `friend.*` ...) and hooks, or set the pet's numbers from a script (`player.adjustCare`, `player.setTricks` ...).
- **A mod NPC joins the generated quests only if you ask.** Give the NPC record `quest_pool: true` (and a `quest_tier`) to let the quest generator pick it as a giver or receiver. Without it the NPC is never drawn. A `side_quest` record can use any NPC without that.
- **No custom fonts or UI art.** Text is drawn with the game's fonts, and the title and menu pictures come from the ROM. A mod draws its own pictures and shapes with `g`, not new fonts.
- **Mod animations are recoloured only if you ask.** A mod's `anim` takes the pet's colours only when its record has a `palette` (the game palette the pictures were drawn with); otherwise the pictures are drawn as they are.
- **Objects have no animation frames.** An object type is a single `png`. To animate one, swap its type from a script (`object.setType`).
- **NPC walking ignores walls.** `npc.walkTo` walks with the NPC's own pixel-exact walking and does not look at walls; a script asks `area.walkable` itself.
- **MIDI songs: only some controls are read.** A `.mid` file is played with the ROM's instruments. Note on / off, program change, volume (CC 7), expression (CC 11), pan (CC 10), the sustain pedal, pitch bend and its range (RPN 0), and tempo changes are used; other controllers, aftertouch and sysex are skipped.

## Scripts

A script is sandboxed (see Scripting): no files, no `os`, no `require`, unless the mod is `trusted`.

| What | Limit |
|---|---|
| Time one call into a script may take | about a million instructions, then it is stopped with an error |
| Scripts waiting at once (`say`, `ask`, `wait`) | 8 |
| Values a mod keeps in saves (`save.set`) | 500 keys per mod; a saved string at most 1000 characters |
| Variables passed to `say` / `ask` | 16 |
| Rows a mod adds to the Options screen | 8 in all (every mod together) |
| Things shaded with `world.shade` at once | 64 |
| Animations queued with `player.play(... next)` | 16 |
| Draw calls a frame (all `g` calls of all mods) | 4000 |
| Canvases | up to 2048 x 2048 pixels each; the 8 used last are kept |
| Shader uniforms / extra textures | 16 / 4 |

Other limits:

- **Hooks and systems are the game's.** A mod cannot define a new hook or system of its own; it works with the ones listed in the references. (Mods can share state through flags, counters and `profile` / `save` keys.)
- **`math.random` does not exist.** Use `rand.int` and `rand.pick`, so randomness stays in one place.
- **Shaders need OpenGL.** They are GLSL 1.20. The game picks the OpenGL renderer by itself when any mod has a `shaders/*.frag` file; the setting `GP_RENDERER` can force a driver.
- **One script state.** Every mod's scripts run in one Lua state and share its global variables. Keep your state in `local` variables, `save` and `profile`, and give any global a name nobody else will use.

## Input

- **Scripts see the six actions, and the mouse.** `ctx.input` / `ctx.held` are the same for a keyboard, a controller and the mouse, and `input.mouse()` gives the cursor and its buttons. A script cannot read a second controller, the triggers or the right stick, or the keyboard keys other than the six. Controller and mouse are switched in the Options (and `settings.txt`), not by a mod.
- **One controller.** The first one plugged in plays.
- **Pointing works on part of the game.** `ui.targets()` covers the menu bar, the item grid and its tabs, and the boxes in the walking world. The title screens, the Options and Mods screens, the minigames, the pet's stats screen and shop and sell quantity numbers are not covered (the wheel changes a quantity).

## The front end

- **The title menu's entries are fixed** (New Game, Load Game, Options, Mods). A mod cannot add a button, but `frontend.menu` can intercept a choice, and a scene can open from any screen. (Mods has no button art in the ROM, so it is drawn as text.)
- **The pictures and fonts of the title screens come from the ROM.** Mods can draw over them (`onPostDraw`), reword them (`base:str/fe/...`), and shade them, not replace the originals.
- **A change in the Mods screen reloads the title screen when you leave it**, not the running game. A game that is already running keeps the mods it started with until it is started again.

## The engine itself

- **Scripts and records do not patch the program.** They work through the seams the engine offers (records, hooks, systems); a part with no seam cannot be changed from them. A **native mod** (C++, see Native mods) can replace any function by name, but only with the player's explicit say-so, and only in the game, not on the title screen.
- **`game_api` is a promise, not a guarantee of every detail.** A mod keeps working across versions with the same major number; details the references do not mention can still change.
