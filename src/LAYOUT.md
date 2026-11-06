# Source layout

```
src/
  app/            the programs: gigapets.cpp (main), walk.cpp (the game), frontend_demo.cpp (title screens); app_includes.hpp, app_context.hpp
  engine/
    audio/        sound and music: audio, audio_clip, audio_registry, rom_audio, midi, midi_export, instrument_export
    render/       drawing: display, gl_shader, render, render_content, room_render, sprite_cache, text_layout
    input/        keybinds, input_bridge (controller and mouse), player_input, mouse_state
    (root)        the rest of the engine: collision, ground, registry, systems, tuning, area_runtime, ...
  loaders/        reading the ROM and its formats (rom, text, sprite, png, objects, npc, shops, ...)
  content/        the content database (db.hpp) and its record types (records.hpp)
  mods/           mod loading, manifests, the script host (Lua), native mods, the Mods screen's state
  game/
    pet/          the pet: player, care and personality, befriending, tricks, thought bubbles, washing, sitting, potions
    npc/          NPCs: actors, brains, walking, spawning, dialogue, encounters, wandering
    items/        items, shops, selling, offerings, starter pools
    quests/       quests (story, generated, side), quest text, the reminder
    minigames/    the minigames and their prize tables
    ui/           the menu bar, name entry, text and script drawing, test and debug modes
    data/         saves, settings, profile, resume position
    world/        rooms, the world map, interactions
    walk/         the walking world, one header per part of runWalk (walk_state.hpp, walk_world_step.hpp, ...)
    frontend/     the title screens: events, frame, Mods screen, the front end's words
    walk_state.hpp  the plain data of a walk session (the one file kept at the top of game/)
```

Includes are relative to the file that includes them (`"../pet/player.hpp"`), except the few that go through the include
directories the build passes (`-Isrc/engine -Isrc/loaders -Isrc/game -Ithird_party`), like `"pet/player.hpp"`.
A native mod includes from `src/` itself (`"engine/input/keybinds.hpp"`, `"game/pet/thought_bubble.hpp"`).
