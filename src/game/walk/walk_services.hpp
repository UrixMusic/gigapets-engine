// walk_services.hpp - the shared services a walk session's free functions need.
//
// walk_state.hpp holds plain data (WalkState). This holds *references* to the heavier objects that stay local in
// runWalk() (the content db, the script host, the player, the dialogue, the quest runtime, the world items) plus the
// few callbacks that runWalk assigns later (refreshBits, saveGame, currentArea). Free functions take
// `WalkServices &svc` and reach everything through it, so they no longer need to be lambdas inside runWalk().
//
// Every member is a reference or a std::function owned by runWalk(); a WalkServices must not outlive it.
// Include after the game headers in walk.cpp (and after walk_state.hpp).
#pragma once
#include "walk_state.hpp"
#include <functional>

struct WalkServices
{
    gp::ContentDB &db;
    gp::ScriptHost &scripts;
    gp::Player &player;
    gp::Dialogue &dialogue;
    gp::QuestRuntime &quests;
    WalkState &S;
    gp::WorldItems &events;                // session-only ground items (also what world.drop spawns into)
    std::function<void()> &refreshBits;    // assigned later in runWalk (it needs the loaded area)
    std::function<void()> &saveGame;       // assigned later in runWalk
    std::function<int()> currentArea;      // legacy id of the area the player is in
    // A friend's gift: drop item number `item` at the pet like a minigame prize. Assigned later in runWalk.
    std::function<void(int item)> dropGift;
    // A friend's "play a game?" answered Yes: fade into a random free minigame. Assigned later in runWalk.
    std::function<void()> startRandomGame;
    // The teacher shows the trick off (its animation, repeated) before the lesson is offered. done() runs when it ends.
    // Assigned later in runWalk.
    std::function<void(gp::NpcActor &teacher, int level, std::function<void()> done)> startTrickShowcase;
    // Fades into the Trick Teacher's code game (assigned later in runWalk, once the minigames exist).
    // done(won) runs on the way back to the world.
    std::function<void(const gp::NpcDef &teacher, int level, const std::string &trickName, std::function<void(bool)> done)>
        startTrickGame;
    // Go into a player home (location 0..2) or a hotel (4..7): fade, show the room. Assigned later in runWalk.
    std::function<void(int location)> enterRoom;
    // The spaceship's colour machine (activity 0x29): the pet's colour cycles until Z. Assigned later in runWalk.
    std::function<void()> startColorMachine;
    // The Giga Island map with a marker where the pet is (the sign in Gigatown; the map item). Assigned later in runWalk.
    std::function<void()> showMap;
    // Jeremy's Yes: the item screen in sell mode (ShowSellMenu). Assigned later in runWalk.
    std::function<void()> startSell;
};