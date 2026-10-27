// walk_state.hpp - the plain-data state of a walk session, pulled out of runWalk().
//
// Step 1 of untangling walk.cpp: this is ONLY data (no lambdas, no callbacks). runWalk keeps its old local
// names as reference aliases (`int &facing = S.pet.facing;`), so the code that uses them is unchanged.
// Later steps turn lambdas into free functions that take `WalkState &`, and the aliases go away one by one.
//
// Include this AFTER the other game headers in walk.cpp: it needs gp::MenuSub, gp::NpcActor, gp::ExitDef,
// gp::EncounterSequence and gp::Point to be declared already.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

enum class Pose
{
    Move,
    Wait,
    Settle,
    Idle,
    Special
};

// One step of an animation sequence (e.g. 52 -> 53 -> 34).
struct AnimStep
{
    int id;
    bool loop;
    int rate;
};

// Screen fade: alpha 0..1, target, seconds for a full 0..1. Starts black and fades in.
struct Fade
{
    double a = 1.0, to = 0.0, sec = 16.0 / 30;
    void go(double t, double s)
    {
        to = t;
        sec = s;
    }
    void step(double dt)
    {
        const double d = dt / sec;
        a = a < to ? std::max(std::min(a + d, to), a) : std::max(a - d, to);
    }
};

// Item menu (the grid with tabs)
struct ItemMenu
{
    bool open = false;
    int category = 0, tab = 0;
    gp::MenuSub sub = gp::MenuSub::Grid;
    int firstRow = 1, selRow = 1, selCol = 1;
    std::vector<int> filtered;
};

// One queued draw call for the menu layer pass
struct Cmd
{
    int layer;
    std::function<void()> draw;
};

// The menu bar's own animation
struct BarAnim
{
    int id = -1, frame = 0, ticks = 0;
    bool done = true, visible = false;
};

// The pet: movement, current animation, feeding / medicine / dig / swift-drop sequences.
struct PetState
{
    int facing = 1;        // start facing down
    bool faceLeft = false; // the game's facing word: 0 = left, 1 = right (2 = up, 3 = down); persists while turning
    int speedMul = 1;
    int walkTicks = 0;
    uint8_t prevDirs = 0; // to detect new key presses
    int anchorMode = 0, offX = 0, offY = 1;
    int flipMode = 0; // 0 = old mirror formula, 1 = hypothesis using record word 2 (toggle with F)

    Pose pose = Pose::Move; // Move with walkTicks == 0 shows walk frame 0
    int settleTicks = 0;
    int animId = 54, animFrame = 0, animTicks = 0, animRate = 14;
    bool animLoop = true, animDone = false;
    std::vector<AnimStep> queue; // what plays after the current one-shot finishes
    int idleTicks = 0, idleThreshold = 0;
    bool animReverse = false;
    int feedStep = 0, feedRepeats = 0, feedCount = 0, feedHappy = 0;

    bool medStuck = false; // wrong medicine: walking in place until another animation plays
    int medStuckTicks = 0, medStuckLimit = 0;
    int overId = -1, overTicks = 0; // overhead medicine animation (-1 = none)
    bool feedFood = true;           // the +6 Happiness bonus is food only
    int settlePhase = 0;            // TickActor_SettleFacing

    int digAnimDY = 1; // 0 = anchor sits at the pet's feet
    int digStep = 0, digTicks = 0;

    int swiftDropStep = 0, swiftDropTicks = 0;
    std::vector<int> swiftDropItems;
};

// Where things are in the current area, and how far through the tick the renderer is.
struct WorldState
{
    gp::Point pos{};     // the pet
    gp::Point prevPos{}; // where the pet was before the latest tick (smooth camera interpolates from here)
    std::vector<gp::NpcActor> npcs;
    std::vector<std::pair<int, int>> npcPrev; // each NPC's x,y before the latest tick (same order as npcs)
    float renderAlpha = 1.f;                  // how far through the current tick this frame is (0..1)
    uint32_t gameTicks = 0;                   // the game's tick counter: +2 per 30 Hz tick
    std::unique_ptr<gp::EncounterSequence> enc;
    gp::NpcActor *encNpc = nullptr;
};

// Screen switching and the main loop's flags.
struct FlowState
{
    Fade fade;
    std::optional<gp::ExitDef> pendingExit;
    std::function<void()> pendingScreen; // runs while the screen is fully black
    bool exitsArmed = false;             // stops bouncing straight back through the door you arrived by
    bool prevZ = false, prevX = false;
    bool running = true;
    bool backToMenu = false;
    double acc = 0; // tick accumulator
};

// Item menu, menu bar.
struct UiState
{
    ItemMenu im;
    std::vector<Cmd> menuCmds;
    BarAnim barAnim;
    int barBusy = 0; // g_isPetBusy
    bool barInHouse = false;
};

struct WalkState
{
    PetState pet;
    WorldState world;
    FlowState flow;
    UiState ui;
};