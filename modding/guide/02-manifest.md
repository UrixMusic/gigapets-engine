# The manifest

`manifest.json` in the mod's folder. Only `id` and `game_api` are required.

| Field | What it is |
|---|---|
| `id` | The mod's id: lowercase letters, digits and `_`. Must be the folder's name. `base` is reserved. Every id the mod makes starts with it (`mymod:item/cake`). |
| `game_api` | The version of the game's mod interface the mod was made for: `"1"`. Only the number before the first dot decides compatibility; a mod for another major number is refused. |
| `name` | What the Mods screen lists. The id if there is none (and a warning). |
| `version` | The mod's own version, shown on the Mods screen. Default `"0"`. |
| `description` | A few lines for the info box of the Mods screen. |
| `icon.png` | Not a field: a file in the mod's folder, 32x32, shown on the Mods screen. A warning if it is missing or another size. |
| `requires` | A list of mod ids that must be installed (and switched on); `"base"` is allowed. The mod loads after them. A missing one is an error. |
| `load_after` | A list of mod ids to load after *if they are installed*. Ordering only, nothing is required. |
| `trusted` | `true` lets the mod's scripts leave the sandbox (`io`, `os`, `require`, `load` ...). The player is shown that on the Mods screen. See the scripting page. Default `false`. |
| `data_version` | The version of what the mod keeps in saves. Raise it when you change how saved values look; see the saves page. Default `1`. |

## Load order

Mods load in an order that puts every mod after what it `requires` and `load_after`s. Within that, the player's order from the Mods screen is used, and mods the player has not placed come alphabetically. A later mod can change what an earlier one made, so the order matters when two mods touch the same thing.

## Switched-off mods

A player can switch a mod off on the Mods screen. It is not loaded at all: its data and scripts do not exist for that run. A mod that `requires` an off mod is left out too (the screen says why). What the mod saved stays in the save file and is back when it is switched on again.
