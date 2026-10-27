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
};