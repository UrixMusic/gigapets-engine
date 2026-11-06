// app_includes.hpp - the headers the game program (walk.cpp, and the main() in gigapets.cpp) needs: the engine and most of the game.
// Kept in one place so the program can be compiled in several .cpp files that see the same declarations.
#pragma once
#define NOMINMAX // windows.h first, before any "#pragma GCC optimize" in the audio headers: otherwise its inline functions are emitted in every .cpp file
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../engine/tuning.hpp"
#include "../engine/input_bridge.hpp"
#include "../game/frontend/fe_strings.hpp" // the front end's words are texts a mod may replace: they exist in every db the mods load into
#include <SDL2/SDL.h>
#include "../loaders/import.hpp"
#include "../engine/collision.hpp"
#include "../engine/registry.hpp"
#include "../game/pet_colour_save.hpp"
#include "../engine/player_input.hpp"
#include "../engine/ground.hpp"
#include "../engine/area_runtime.hpp"
#include "../engine/render_content.hpp"
#include "../game/npc_actor.hpp"
#include "../game/player.hpp"
#include "../game/dialogue.hpp"
#include "../game/befriend.hpp"
#include "../game/encounter.hpp"
#include "../game/encounter_sequence.hpp"
#include "../game/text_draw.hpp"
#include "../game/shop.hpp" // with the other includes
#include "../engine/render.hpp"
#include "../engine/sprite_cache.hpp" // AnimRef, SpriteCache
#include "../game/menu_draw.hpp"
#include "../game/item_tabs.hpp"
#include "../game/story_blast.hpp"
#include "../game/menu_bar.hpp"
#include "../game/item_use.hpp" // at the top with the other includes
#include "../game/pending_event.hpp"
#include "../game/fixed_spawn.hpp"
#include "../loaders/fixed_spots.hpp"
#include "../game/quest_flow.hpp"
#include "../game/item.hpp"
#include "../game/progress_bits.hpp"
#include "../game/offerings.hpp"
#include "../game/resume_area.hpp"
#include "../game/save.hpp"
#include "../game/save_player.hpp"
#include "../game/profile.hpp"
#include "../game/debug_mode.hpp"
#include "../game/settings.hpp"
#include "../game/thought_bubble.hpp"
#include "../game/pet_voice.hpp"
#include "../game/trick_panel.hpp"
#include "../game/quest_nudge.hpp"
#include "../game/potion.hpp"
#include "../game/stat_gate.hpp"
#include "../game/color_machine.hpp"
#include "../game/world_map.hpp"
#include "../game/npc_stink.hpp"
#include "../game/sale.hpp"
#include "../engine/audio.hpp"
#include "../engine/display.hpp"
#include "../engine/keybinds.hpp"
#include "../game/stat_notify.hpp"
#include "../game/swift_pick.hpp"
#include "../mods/mod_loader.hpp"
#include "../mods/mod_list.hpp"
#include "../game/three_in_a_row.hpp"
#include "../game/memory_match.hpp"
#include "../game/trick_showcase.hpp"
#include "../game/pet_sit.hpp"
#include "../game/pet_wash.hpp"
#include "../game/selector_icon.hpp"
#include "../game/room_scene.hpp"
#include "../engine/room_render.hpp"
#include "../game/item_pools.hpp"
#include "../game/interactions.hpp"
#include "../mods/script_host.hpp"
#include "../mods/native_mod.hpp"
#include "../mods/script_check.hpp" // hook target / hook name validation
#include "app_context.hpp"
#include "../game/minigame_ui.hpp" // AnimRef, readRomString, Minigames (swift pick, wells, tents)

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
