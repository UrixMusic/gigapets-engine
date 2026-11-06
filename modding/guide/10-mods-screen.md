# The Mods screen

Title menu -> **Mods** (under Options). Lists every folder in `mods/` that has a manifest.

Manifest fields it shows (all optional, `name` falls back to the id):
- `name` - what the list shows
- `description` - the box on the right
- `version`, `trusted` - shown as before ("Trusted: runs freely" for trusted Lua)
- `icon.png` in the mod's folder - 32x32, drawn next to the name

Keys (as the player has bound them): Up / Down move, Select switches the mod on / off, Menu enters order mode
(Select picks a mod up, Up / Down move it, Select puts it down, Menu leaves), Cancel goes back.

What is chosen is kept in `mods.txt` in the data folder (`order <id>` lines, `off <id>` lines). It applies when mods load:
- a mod that is off is not loaded; a mod that requires an off mod is left out too (the screen says why)
- the order is the load order wherever `requires` / `load_after` allow it (a mod can not load before what it requires)
- the game loads mods when a game starts, so changes show then; the title's own mods (front end scenes, strings) change on the next start of the program
- `--validate-mods` ignores the state (it checks every mod)
- `frontend.menu` gets `choice = "mods"`; the words are the texts `base:str/fe/mods*`
