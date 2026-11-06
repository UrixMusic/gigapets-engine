### rec:text
A line of text. `{ "type": "text", "mode": "add", "id": "mymod:str/hello", "text": "Hello!" }` adds one (scripts show it with `say("mymod:str/hello")`; NPC lines, names and item names are text ids too). Mode `replace` changes a text that exists: any base text, another mod's, and the front end's own words (`base:str/fe/back` and the others on the front end page).

### rec:anim
An animation made of pictures from the mod. `{ "type": "anim", "mode": "add", "id": "mymod:anim/bob", "palette": 479, "frames": [ { "png": "art/bob1.png", "anchorX": 16, "anchorY": 40, "dx": 0, "dy": 0 }, ... ] }`.
Each frame is a `png` (a path inside the mod) with where the picture hangs from (`anchorX`, `anchorY`) and an offset (`dx`, `dy`). `palette` is a game palette number (257 to 4095, like 479 = the dog's colours): the pictures then take the pet's colours instead of staying as drawn.
Modes: `add`, `replace` (starts over with the new frames), `remove` (references to it are not checked). NPC records point at an anim with `anim`.

### rec:palette
A colour palette, only mode `add`. Give exactly one of: `colors` (1 to 256 colours, each `[r, g, b]` 0..255) or `based_on` (a ROM palette number, 257 and up) with `set` (an object of changes: `{ "3": [255, 80, 20] }`, colour index 0..255 to a colour). `species` (`"dog"`, `"monkey"` or `"panda"`) says which pets the palette is for.
Other records point at a palette by its id: an NPC's `palette`, a species' `default_palette` and starter `colors`.

### rec:npc
An NPC. Modes: `add`, `replace` (unlisted fields keep their values) and `remove`; `patch` is not supported (use `replace`).
`name` (a text id), `area` (an area id, `base:...` or the mod's own), `x`, `y` (pixels), `anim` (an anim id; a new NPC needs `anim` or `species`), `lines` (what it says: a list of text ids; a stationary NPC needs at least 3), `stationary` (stands still and talks; otherwise it wanders), `palette` (a palette id).
`species` (a species id) draws a *roaming pet* with that species' art; it cannot be stationary. `based_on` (add only) starts as a copy of another NPC.
`quest_min` / `quest_max`: the NPC is there only while the pet's count of completed quests is from `quest_min` up to (not including) `quest_max`. `quest_pool` and `quest_tier` make it available to the generated quests (`quest_tier` is one of 0, 1, 2, 3, 5, 6, 7, 9: when quests may use it; 0 = always, 1 = after 3 quests, 2 / 3 = after 16, 9 = after 15, 6 = after 41, 7 = after 60, 5 = after 89).

### rec:minigame
Changes one of the game's minigames, only mode `patch`. `{ "type": "minigame", "mode": "patch", "id": "base:minigame/...", "num": { "cost": 30 } }`. `num` changes the minigame's numbers (an unknown name is an error that lists the ones it has), and `lists`, `list_add` and `list_remove` change its prize lists (entries are palette ids or offsets). At least one of the four is required.

### rec:species
A pet species. Modes `add` and `patch` (patch takes the same fields except `based_on`, and works on the base species too).
`based_on` starts as a copy of another species (like `base:species/0`); without it, `art` must give `walkUp`, `walkDown`, `walkSide` and `idleBase`.
`default_palette` (a palette number), `own_illness` (0 to 9), `start` (`{ health, hunger, happiness, cleanliness, tiredness }`, each 0..255), `money` (`[low, high]`, what a new pet starts with), `starter_items` (`[ { item, count } ]`), `starter_kit` (0 to 2: whose starter kit), and `art` (the sprites and animation roles, `facesRight`, `wideShadow`, `trickNames`, `trickRules`, ...).
`starter` offers it in the starter room: `{ room: true, colors: [palettes], rare_colors: [palettes], requires: [profile keys] }` (`requires` hides it until the player has those `profile` values).

### rec:sound
A sound effect. `{ "type": "sound", "mode": "add", "id": "mymod:sound/meow", "file": "audio/meow.ogg", "volume": 100, "pitch": 0 }`. `file` is a `.wav` or `.ogg` inside the mod's folder, `volume` a percent (0 to 400), `pitch` in semitones (-48 to 48).
Mode `replace` with `id: "base:sound/84"` plays the file wherever the game asks for the ROM's sound 84. (Instruments and drums for MIDI songs are described under `instrument`.)

### rec:music
A song. `{ "type": "music", "mode": "add", "id": "mymod:music/theme", "file": "audio/theme.ogg", "loop_start": 4.0, "loop_end": 60.5 }`. `file` is a `.wav` / `.ogg`, or a `.mid` / `.midi` played with the ROM's instruments (then `programs`, `drums`, `channels`, `slide_channels` and `numbers` shape how; see `instrument`).
Mode `replace` with a `base:music/...` id swaps a song the game has. `volume` and `pitch` as for sounds; `loop_start` / `loop_end` are seconds of the file.

### rec:drum
A drum sample a MIDI song can use: `{ "type": "drum", "mode": "add", "id": "mymod:drum/clap", "file": "audio/clap.wav", "volume": 100, "pitch": 0 }`.
