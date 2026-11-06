# Data records

Every `.json` file in `mods/<id>/data/` is a list of records, read in file name order (so `01_items.json` comes before `02_npcs.json`). A record is an object:

```json
{ "type": "item", "mode": "add", "id": "mymod:item/cake", "...": "fields of that type" }
```

- **`type`** says what kind of thing it is. The **Data records** reference lists the types, with their fields.
- **`mode`** says what to do with it. `add` makes something new. `patch` changes fields of one that exists (the game's or another mod's); fields you leave out stay as they were. `replace` swaps a thing (for an NPC or item, unlisted fields keep their values; for a text or sound it swaps the whole thing). `remove` takes one away. Each type says which of these it takes: for example `npc` takes `add`, `replace` and `remove` (not `patch`), `minigame` only `patch`, `side_quest` only `add`.
- **`id`** names the thing. The ids a mod *adds* start with its own id and a colon: `mymod:item/cake`, `mymod:npc/bob`, `mymod:str/hello`. The game's own things are `base:...`, like `base:area/3`.

## Rules the loader enforces

- Only the fields a type lists are allowed. A misspelt field is an error naming the file and the record, not something quietly ignored.
- A new id must not already exist and must use the mod's own prefix. An id may not contain spaces or control characters, or `,` `=` `|`.
- One bad record does not stop the others from loading; every problem is reported (the game prints them, `--validate-mods` lists them).
- Records can refer to each other across files and mods (an NPC to an area, an object to a scene). The loader applies the records in three passes, so a thing may be mentioned before the file that defines it: first texts, anims, tilesets, collision sets, object types, palettes and audio; then areas (they need a tileset and a collision set); then everything else (NPCs, items, objects, exits, quests ...). Inside a pass, files go in name order.

## Texts and pictures

Texts are records too (`type: "text"`) and are referred to by id everywhere: an NPC's `lines`, `say("mymod:str/hello")`, an item's name. A mod's PNG files are referred to by a path inside the mod's folder (`"png": "art/cake.png"`). A path cannot leave the mod's folder.

## Changing the game's own things

Patch records change what the game has: `base:area/3`, `base:room/4`, `base:item/...`. Several mods may patch the same thing; they apply in load order, so the last one wins on a field they both set.

## Seeing how the game's own things are made

The game can write its content out for you to look at and copy from:

- `gigapets.exe rom.u7 --export-base <folder>`: the base game's areas, tiles and objects as JSON and PNG.
- `--export-music <folder>`: the songs as MIDI files, and where each plays.
- `--export-instruments <folder>`: what each instrument number sounds like.
- `--export-pet-frames <folder>`: the pets' animation frames as pictures.

Each writes its files and quits.
