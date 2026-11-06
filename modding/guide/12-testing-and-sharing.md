# Testing and sharing a mod

## While you make it

1. Put the folder in `mods/` and run `gigapets.exe rom.u7 --validate-mods`. It prints every problem (the file, the record or hook, the reason) and a count at the end; it exits with 1 if anything is wrong, so you can run it from a script.
2. Warnings are not failures: the common ones are a missing `name`, `description` or `icon.png` in the manifest, or a song using an instrument the game does not have. They are worth fixing, but the mod loads.
3. Start the game and look at the **Mods** screen: the mod should be listed with its name, version, description and icon, and "N problems" in its info box if the loader found any when the game started.
4. The game prints its log to the console; `print` and `log` from scripts end up there with `[yourmod]` in front.

## Things that save time

- Edit data and scripts, then use the Mods screen to switch the mod off and on: leaving the screen after a change reloads the title screen with the new set of mods.
- A mod folder is the whole mod. To test with a clean setup, copy only that folder and the ones it `requires` into a fresh `mods/`.
- `--export-base <folder>` and the other export flags (see Data records) show how the game's own content is written, the easiest way to learn a record type is to look at a real one.

## Sharing

A mod is a folder: zip it (the folder itself, with `manifest.json` inside it) and send it. A player unzips it into their `mods/` folder next to the game and switches it on under **Mods**.

Before you share:

- Make sure `--validate-mods` reports 0 errors, with only the mods you require installed.
- Give it a `name`, `version`, `description` and a 32x32 `icon.png`.
- List what it needs in `requires`; list mods it should load after in `load_after`.
- Raise `version` when you publish a change. If you changed how the mod's saved values look, raise `data_version` and handle the old ones in `Game.hooks():onMigrate`.
- If the mod is `trusted`, say why in the description: players see that it is.

## Compatibility

`game_api` is `"1"`. A change to the game that removes or alters something a mod can use bumps the number before the dot; additions only bump the part after it. A mod written for `1` keeps working on every `1.x`.
