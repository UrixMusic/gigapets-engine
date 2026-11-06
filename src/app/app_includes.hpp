// app_includes.hpp - the headers the game program (walk.cpp, and the main() in gigapets.cpp) needs: the engine and most of the game.
// Kept in one place so the program can be compiled in several .cpp files that see the same declarations.
#pragma once
#define NOMINMAX // windows.h first, before any "#pragma GCC optimize" in the audio headers: otherwise its inline functions are emitted in every .cpp file
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../engine/tuning.hpp"
#include "../engine/input/input_bridge.hpp"
#include "../game/data/input_settings.hpp"
#include "../game/frontend/fe_strings.hpp" // the front end's words are texts a mod may replace: they exist in every db the mods load into
#include <SDL2/SDL.h>
#include "../loaders/import.hpp"
#include "../engine/collision.hpp"
#include "../engine/registry.hpp"
#include "../game/data/pet_colour_save.hpp"
#include "../engine/input/player_input.hpp"
#include "../engine/ground.hpp"
#include "../engine/area_runtime.hpp"
#include "../engine/render/render_content.hpp"
#include "../game/npc/npc_actor.hpp"
#include "../game/pet/player.hpp"
#include "../game/npc/dialogue.hpp"
#include "../game/pet/befriend.hpp"
#include "../game/npc/encounter.hpp"
#include "../game/npc/encounter_sequence.hpp"
#include "../game/ui/text_draw.hpp"
#include "../game/items/shop.hpp" // with the other includes
#include "../engine/render/render.hpp"
#include "../engine/render/sprite_cache.hpp" // AnimRef, SpriteCache
#include "../game/ui/menu_draw.hpp"
#include "../game/items/item_tabs.hpp"
#include "../game/quests/story_blast.hpp"
#include "../game/ui/menu_bar.hpp"
#include "../game/items/item_use.hpp" // at the top with the other includes
#include "../game/npc/pending_event.hpp"
#include "../game/npc/fixed_spawn.hpp"
#include "../loaders/fixed_spots.hpp"
#include "../game/quests/quest_flow.hpp"
#include "../game/items/item.hpp"
#include "../game/pet/progress_bits.hpp"
#include "../game/items/offerings.hpp"
#include "../game/data/resume_area.hpp"
#include "../game/data/save.hpp"
#include "../game/data/save_player.hpp"
#include "../game/data/profile.hpp"
#include "../game/ui/debug_mode.hpp"
#include "../game/data/settings.hpp"
#include "../game/pet/thought_bubble.hpp"
#include "../game/pet/pet_voice.hpp"
#include "../game/pet/trick_panel.hpp"
#include "../game/quests/quest_nudge.hpp"
#include "../game/pet/potion.hpp"
#include "../game/pet/stat_gate.hpp"
#include "../game/pet/color_machine.hpp"
#include "../game/world/world_map.hpp"
#include "../game/npc/npc_stink.hpp"
#include "../game/items/sale.hpp"
#include "../engine/audio/audio.hpp"
#include "../engine/render/display.hpp"
#include "../engine/input/keybinds.hpp"
#include "../game/pet/stat_notify.hpp"
#include "../game/minigames/swift_pick.hpp"
#include "../mods/mod_loader.hpp"
#include "../mods/mod_list.hpp"
#include "../game/minigames/three_in_a_row.hpp"
#include "../game/minigames/memory_match.hpp"
#include "../game/pet/trick_showcase.hpp"
#include "../game/pet/pet_sit.hpp"
#include "../game/pet/pet_wash.hpp"
#include "../game/ui/selector_icon.hpp"
#include "../game/world/room_scene.hpp"
#include "../engine/render/room_render.hpp"
#include "../game/items/item_pools.hpp"
#include "../game/world/interactions.hpp"
#include "../mods/script_host.hpp"
#include "../mods/native_mod.hpp"
#include "../mods/script_check.hpp" // hook target / hook name validation
#include "app_context.hpp"
#include "../game/minigames/minigame_ui.hpp" // AnimRef, readRomString, Minigames (swift pick, wells, tents)

#include <random>
#include <set>
#include <functional>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
//../game/walk/
#include "../game/walk/walk_state.hpp" // pet/world/flow/ui state of a run (needs the game headers above)
#include "../game/walk/walk_services.hpp"  // shared services for free functions
#include "../game/walk/walk_script_bindings.hpp" // ScriptHost <-> game
#include "../game/walk/walk_npc_talk.hpp" // talking to NPCs, friend greetings, onTalk seam
#include "../game/walk/walk_interactions.hpp" // Z on an object: actions, minigame starters, onInteract seam
