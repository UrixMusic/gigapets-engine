# Saves and mod data

`save.get / save.set` (one slot) and `profile.get / profile.set` (the whole game) are kept as `mod.<mod>.<key>` in the save file / data/profile.txt.
- A mod that is switched off (Mods screen) or removed keeps its values: the save is written with every key it was loaded with, so switching it back on finds them.
- `"data_version": N` in manifest.json (default 1) versions those values. When a save made with an older data_version (or with values and no version = 1) is loaded,
  the mod's `Game.hooks():onMigrate(ctx)` runs once before `onLoad` (`ctx.from`, `ctx.to`; save.get / save.set work), then the save is stamped with `N` (`mod.<mod>.__data_version`).
  A mod that never raises data_version has no stamp. A save made with a *newer* data_version than the mod's is left alone and a message says so.
- Profile values are not migrated (they are read by the front end too); keep them backward compatible.
