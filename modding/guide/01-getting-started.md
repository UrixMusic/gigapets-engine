# Getting started

A mod is a folder in `mods/` next to the game. Everything in it is plain text and pictures: there is nothing to compile.

```
mods/
  mymod/
    manifest.json        the mod's name and version (required)
    icon.png             32x32 picture for the Mods screen (optional)
    data/                JSON files: new and changed things (items, NPCs, areas, texts ...)
      items.json
    scripts/             Lua files: what the things do
      main.lua
    shaders/             .frag files (graphics effects, optional)
    audio/               sounds and music (optional)
```

The folder name must be the mod's id, and the id is lowercase letters, digits and `_` (`mymod`, `glow_pack`). `base` is the game itself and cannot be used.

## A first mod

`mods/mymod/manifest.json`:

```json
{
  "id": "mymod",
  "name": "My First Mod",
  "version": "1.0",
  "game_api": "1",
  "description": "Says hello when the game starts."
}
```

`mods/mymod/data/texts.json`: a list of records. This one adds a line of text:

```json
[
  { "type": "text", "mode": "add", "id": "mymod:str/hello", "text": "Hello from my mod!" }
]
```

`mods/mymod/scripts/main.lua`: a hook that shows it when a game starts:

```lua
local G = Game.hooks()

function G:onLoad(ctx)
  say("mymod:str/hello")
end
```

Start the game. The text appears when you load or start a game, and `mymod` is listed under **Mods** on the title menu.

## Checking your work

```
gigapets.exe rom.u7 --validate-mods
```

loads the game and every mod (data and scripts), prints each error and warning with the file it came from, and exits with 1 if anything is wrong. Run it after every change: most mistakes (a misspelt field, an unknown id, a script that does not load) are found there before you start playing.

## Where to go next

- **Manifest**: every field of `manifest.json`.
- **Data records**: how the JSON files work. The **Data records** reference lists every record type with its fields.
- **Scripting**: hooks, `ctx`, the waiting functions, and what a script is allowed to do. The **Lua API** and **Hooks** references list everything.
- **Systems**: change how the engine itself behaves (stats wearing down, the pet's steps ...) without touching its code.
- **Graphics**: draw on the screen, add shaders, zoom the camera.
