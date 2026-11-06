// walk_world_step.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of WorldStep.
// What they need from the rest of the walk session comes in through WorldStepEnv, under the names they had, so the bodies read as before.
#pragma once
#include "../../engine/activity_system.hpp"
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct WorldStepEnv
{
    const int &W;
    bool &afterPickup;
    bool&animDone;
    int&animFrame;
    int&animId;
    bool&animLoop;
    int&animRate;
    std::function<AnimRef(const std::string&)> animRefById;
    bool&animReverse;
    int&animTicks;
    menubar::MenuBar &bar;
    BarAnim&barAnim;
    std::function<AnimRef(int)> barAnimRef;
    int&barBusy;
    WalkBarHost &barHost;
    int &barkStep;
    std::string &blockedExit;
    std::function<void(const gp::ShopEvent &)> buyFromShop;
    gp::CameraShake &cameraShake;
    std::function<void()> closeTrickPanel;
    int &colorStep;
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    gp::Dialogue &dialogue;
    int&digStep;
    std::unique_ptr<gp::EncounterSequence>&enc;
    gp::NpcActor*&encNpc;
    gp::WorldItems &events;
    bool&exitsArmed;
    bool&faceLeft;
    int&facing;
    Fade&fade;
    int&feedStep;
    long int &gameTickN;
    unsigned int&gameTicks;
    std::function<AnimRef(int)> getAnim;
    bool &giveMode;
    std::function<void()> &holdDone;
    gp::NpcActor *&holdNpc;
    int &holdTicks;
    gp::EncounterHost &host;
    int&idleThreshold;
    int&idleTicks;
    ItemMenu&im;
    walk::Interactions &interactions;
    std::function<bool(int)> isRestIdle;
    bool &itemLeave;
    std::function<void(uint8_t, bool, bool, bool, bool)> itemMenuTick;
    std::function<std::string(int)> itemName;
    const int &kBarRate;
    const long long unsigned int &kMaxNpcs;
    const int &kSettleTicks;
    const int (&kWalkAnim)[3];
    gp::SceneInput &lastKeys;
    unsigned int &lastSaveTick;
    bool&medStuck;
    int&medStuckLimit;
    int&medStuckTicks;
    Minigames &minigames;
    std::function<gp::NpcActor*()> nearestNpc;
    std::function<const gp::ShopEvent*()> nearestShopItem;
    bool &npcMute;
    std::vector<std::pair<int, int> >&npcPrev;
    gp::Rng&npcRng;
    std::vector<gp::NpcActor>&npcs;
    gp::QuestNudge &nudge;
    int&overId;
    int&overTicks;
    std::function<int(int)> ownedCount;
    BarAnim &panelAnim;
    bool &panelClosing;
    int &pendingBarState;
    std::optional<gp::ExitDef>&pendingExit;
    std::function<void()>&pendingScreen;
    std::function<void(int)> pickTrick;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    gp::Point&pos;
    Pose&pose;
    int &potionStep;
    std::function<void()> pressCancelInWorld;
    unsigned char&prevDirs;
    gp::Point&prevPos;
    bool&prevX;
    bool&prevZ;
    int &questionStep;
    gp::QuestRuntime &quests;
    std::vector<AnimStep>&queue;
    std::function<std::string(int, int)> reactAnimId;
    std::function<void()> &refreshBits;
    std::function<void(const gp::ExitDef&)> requestGoTo;
    std::function<void(std::function<void()>)> requestScreen;
    std::function<int()> restIdleId;
    std::function<int()> restIdleRate;
    gp::Rom&rom;
    std::unique_ptr<RoomView> &room;
    std::function<void()> roomTutorial;
    std::function<void(int)> runBarState;
    std::function<void()> &saveGame;
    bool &sceneActive;
    int &scriptLock;
    ScriptNpcState &scriptNpc;
    gp::ScriptHost &scripts;
    bool &sellMode;
    const gp::Settings &settings;
    int&settleTicks;
    std::function<void(int, int)> sfxFn;
    int &shakeDy;
    double &shopRestockAccum;
    std::function<int(int)> shopRnd;
    gp::ShopStock &shopStock;
    std::function<void()> &showDone;
    int &showLevel;
    gp::NpcActor *&showNpc;
    bool &showStarted;
    gp::TrickShowcase &showcase;
    int &sp;
    int&speedMul;
    std::function<void()> startDig;
    std::function<void(int)> startWorldTutorial;
    gp::StatNotice &statNotice;
    bool &statsOpen;
    gp::StinkTransfer &stinkXfer;
    WalkServices &svc;
    int&swiftDropStep;
    std::function<void()> tickBark;
    std::function<void()> tickBlasts;
    std::function<void(bool)> tickColorMachine;
    std::function<void()> tickDig;
    std::function<void()> tickFeed;
    std::function<void()> tickHoleFills;
    std::function<void()> tickPotion;
    std::function<void()> tickQuestion;
    std::function<void(uint8_t, uint8_t, bool, bool)> tickRoom;
    std::function<void()> tickStatNotice;
    std::function<void()> tickSwiftDrop;
    std::function<void()> tickThoughtBubble;
    std::function<void()> tickTrick;
    std::function<void()> tickWash;
    std::function<void()> tickWorldTutorial;
    int &trickStep;
    std::function<void(int)> tryPickup;
    bool &tutNudge;
    int &tutStep;
    std::function<void(gp::NpcActor&)> updateNpcAnim;
    std::function<void()> useSelectedItem;
    int&walkTicks;
    int&washStep;
    std::unique_ptr<AreaWorld> &world;
};

class WorldStep : public WorldStepEnv
{
public:
    explicit WorldStep(WorldStepEnv env) : WorldStepEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, next); }

    // One step of the walking world (30 a second): input, the pet, the NPCs, the world, the scripts' tick hooks. A step that stops early returns.
    auto stepWorld()
    {
        {   // the game clock counts 2 ROM ticks a step at 30 Hz, so it keeps 60 a second at any tickHz (a whole number of ticks a step; the rest carries over)
            static double gameTickFrac = 0;
            gameTickFrac += 2 * gp::refStep();
            const unsigned int whole = static_cast<unsigned int>(gameTickFrac);
            gameTicks += whole;
            gameTickFrac -= whole;
        }
        // The game-wide tick (Game.hooks onTickStart / onTickEnd): first and last in every step, even one that stops early (a menu, a screen change)
        const auto tickInfo = [&]
        {
            gp::ScriptHost::TickInfo t;
            t.tick = gameTickN;
            t.area = cur->def->id;
            t.petX = pos.x;
            t.petY = pos.y;
            t.dialogue = dialogue.open();
            t.menu = im.open || statsOpen || pendingScreen != nullptr;
            t.room = room != nullptr;
            t.encounter = enc != nullptr;
            t.scene = scripts.sceneOpen();
            t.keys = lastKeys;
            return t;
        };
        ++gameTickN;
        if (const char *r = std::getenv("GP_ENTER_ROOM")) // debug: go into that home / hotel location (0..2, 4..7) after 20 steps, for screenshots (with GP_WALK_SHOT)
            if (gameTickN == 20 && svc.enterRoom)
                svc.enterRoom(std::atoi(r));
        scripts.fireGameTick(false, tickInfo());
        struct TickEnd
        {
            std::function<void()> f;
            ~TickEnd() { f(); }
        } tickEnd{[&]
                  { scripts.fireGameTick(true, tickInfo()); }};
        if (const int every = gp::tuning().shopRestockRomTicks; every > 0 && (shopRestockAccum += 2 * gp::refStep()) >= every) // +2 ROM ticks per 30 Hz step, like gameTicks
        {
            shopRestockAccum = 0; // RollShopItemStock (TickCareStatWatchdog condition 12): one more weighted pick for every shop
            shopStock.restock(db.shops(), shopRnd);
        }
        // remember where everything was before this tick, for the smooth renderer
        prevPos = pos;
        statNotice.prevOffset = statNotice.offset; // (so a tick that doesn't run it, a dialogue say, doesn't make it shake)
        if (room) // the room before this tick, for the smooth renderer (every tick, so a paused room holds still)
        {
            room->px = room->x;
            room->py = room->y;
            room->pscroll = room->scroll;
            room->pcurX = room->curX;
            room->pcurY = room->curY;
        }
        // Changes scripts asked for on the NPCs (npc.spawn / move / remove) are made here, between ticks and only while no talk,
        // encounter or dialogue holds a pointer to one of the actors; otherwise they wait for a quieter tick.
        if (!scriptNpc.pending.empty() && !dialogue.open() && !enc && !showNpc)
        {
            auto indexOf = [&](const gp::Id &id) -> long
            {
                for (size_t i = 0; i < npcs.size(); ++i)
                    if (npcs[i].def->id == id)
                        return long(i);
                return -1;
            };
            for (const auto &op : scriptNpc.pending)
            {
                const long i = indexOf(op.id);
                std::printf("script npc %s %s at %d,%d\n", op.kind == ScriptNpcState::Op::Spawn ? "spawn" : op.kind == ScriptNpcState::Op::Move ? "move" : op.kind == ScriptNpcState::Op::Remove ? "remove" : "drive",
                            op.id.c_str(), op.at.x, op.at.y);
                if (op.kind == ScriptNpcState::Op::Spawn)
                {
                    const gp::NpcDef *d = db.npc(op.id);
                    if (!d || i >= 0 || npcs.size() >= kMaxNpcs)
                        return;
                    gp::NpcDef c = *d;
                    c.area = cur->def->legacyId;
                    c.x = op.at.x;
                    c.y = op.at.y;
                    npcs.emplace_back(scriptNpc.defs.emplace_back(std::move(c))); // (capacity was reserved: no actor moves)
                }
                else if (i < 0)
                    return;
                else if (op.kind == ScriptNpcState::Op::Move)
                {
                    gp::NpcActor &a = npcs[size_t(i)];
                    gp::NpcDef c = *a.def;
                    c.x = op.at.x;
                    c.y = op.at.y;
                    a.def = &scriptNpc.defs.emplace_back(std::move(c)); // a wanderer's route is relative to its spawn, so it follows
                    a.body.x = op.at.x;
                    a.body.y = op.at.y;
                    a.target = {op.at.x, op.at.y};
                    a.walk = decltype(a.walk){};
                    a.activity = gp::kActivityIdle;
                    a.activityTicks = 0;
                    a.settle = gp::NpcActor::SettleNone;
                    a.started = false;
                }
                else if (op.kind == ScriptNpcState::Op::Walk)
                    npcs[size_t(i)].scriptWalkTo(op.at.x, op.at.y);
                else if (op.kind == ScriptNpcState::Op::Face)
                    npcs[size_t(i)].scriptFace(op.at.x);
                else if (op.kind == ScriptNpcState::Op::Stop)
                    npcs[size_t(i)].scriptStop();
                else if (op.kind == ScriptNpcState::Op::Release)
                    npcs[size_t(i)].scriptRelease(npcRng);
                else if (op.kind == ScriptNpcState::Op::Play)
                {
                    gp::NpcActor &a = npcs[size_t(i)];
                    if (op.str.empty())
                        a.clearForced();
                    else
                    {
                        a.scriptFace(a.body.facing); // (a script that shows an anim holds the NPC still)
                        a.forceAnim(op.str);
                    }
                }
                else // Remove
                {
                    gp::NpcActor *gone = &npcs[size_t(i)];
                    if (encNpc == gone)
                    {
                        enc.reset();
                        encNpc = nullptr;
                    }
                    if (holdNpc == gone)
                        holdNpc = nullptr;
                    npcs.erase(npcs.begin() + i);
                    for (gp::NpcActor **p : {&encNpc, &holdNpc}) // the actors after it moved down one place
                        if (*p && *p > gone)
                            --*p;
                }
            }
            scriptNpc.pending.clear();
        }
        npcPrev.resize(npcs.size());
        for (size_t ni = 0; ni < npcs.size(); ++ni)
            npcPrev[ni] = {npcs[ni].x(), npcs[ni].y()};
        if (gameTicks - lastSaveTick >= 1800)
        {
            lastSaveTick = gameTicks;
            saveGame();
        }
        const Uint8 *k = SDL_GetKeyboardState(nullptr);
        const uint8_t rawDirs = enc ? 0 : gp::pollHeld(gp::keys().held(k, gp::Action::Left), gp::keys().held(k, gp::Action::Right), gp::keys().held(k, gp::Action::Up), gp::keys().held(k, gp::Action::Down));
        const uint8_t newlyPressed = uint8_t(rawDirs & ~prevDirs);

        prevDirs = rawDirs;

        // Z is read once per tick, as an edge. prevZ updates even while a box is open, so the press
        // that opens a box can never also close it.
        const bool zDown = gp::keys().held(k, gp::Action::Select);
        const bool zPressed = zDown && !prevZ;
        const bool zReleased = !zDown && prevZ;
        const bool xDown = gp::keys().held(k, gp::Action::Cancel);
        const bool xPressed = xDown && !prevX;
        static bool prevMenu = false;
        const bool menuDown = gp::keys().held(k, gp::Action::Menu);
        const bool menuPressed = menuDown && !prevMenu;
        prevMenu = menuDown;
        prevX = xDown;
        prevZ = zDown;
        scripts.setMouse(gp::inputBridge().takeMouse()); // input.mouse() and ctx.mouse for this step
        lastKeys = gp::SceneInput(); // what a game-wide tick hook sees as the keys
        lastKeys.z = zPressed;
        lastKeys.x = xPressed;
        lastKeys.left = newlyPressed & gp::DirLeft;
        lastKeys.right = newlyPressed & gp::DirRight;
        lastKeys.up = newlyPressed & gp::DirUp;
        lastKeys.down = newlyPressed & gp::DirDown;
        lastKeys.heldZ = zDown;
        lastKeys.heldX = xDown;
        lastKeys.heldLeft = rawDirs & gp::DirLeft;
        lastKeys.heldRight = rawDirs & gp::DirRight;
        lastKeys.heldUp = rawDirs & gp::DirUp;
        lastKeys.heldDown = rawDirs & gp::DirDown;
        if (barAnim.visible)
        {
            const AnimRef A = barAnimRef(barAnim.id);
            const int n = int(A.size());
            if (n == 0)
                barAnim.done = true;
            else if (!barAnim.done)
            {
                const int idx = ++barAnim.ticks * kBarRate / 30;
                barAnim.frame = std::min(idx, n - 1);
                if (idx >= n)
                    barAnim.done = true;
            }
            if (barBusy == 1 && (barAnim.frame >= 5 || barAnim.done))
                barBusy = 2; // GUESS: DAT_0020d9 == 5
            if (barBusy == 3 && barAnim.done)
            {
                barBusy = 0;
                barAnim.visible = false;
            }
        }
        if (panelAnim.visible) // the tricks panel (anim 4) and the hand-over from the bar (anim 3)
        {
            const AnimRef P = barAnimRef(panelAnim.id);
            const int n = int(P.size());
            if (n == 0)
                panelAnim.done = true;
            else if (!panelAnim.done)
            {
                const int idx = ++panelAnim.ticks * kBarRate / 30;
                panelAnim.frame = panelClosing ? std::max(0, n - 1 - idx) : std::min(idx, n - 1);
                if (idx >= n)
                    panelAnim.done = true;
            }
            if (panelClosing && panelAnim.done)
            {
                panelAnim.visible = false;
                panelClosing = false;
                bar.forceClose();
                barBusy = 0;
            }
        }
        if (barBusy == 5 && (!barAnim.visible || barAnim.done)) // AdvancePetBusyStateMachine: 5 -> 4 once the bar is gone
            barBusy = 4;
        if (pendingBarState >= 0 && barBusy == 0) // the bar has finished closing: now the screen change
        {
            const int st = pendingBarState;
            pendingBarState = -1;
            runBarState(st);
        }
        if (pendingExit || pendingScreen)
            return;
        if (statsOpen)
        {
            if (zPressed || xPressed)
            {
                requestScreen([&]
                              { statsOpen = false; barBusy = 0; barAnim.visible = false; });
            }
            return;
        }
        if (sceneActive)
        {
            gp::SceneInput in; // keys pressed this tick; ignored during the fade-in
            if (fade.a <= 0.0)
            {
                in.z = zPressed;
                in.x = xPressed;
                in.left = (newlyPressed & gp::DirLeft) != 0;
                in.right = (newlyPressed & gp::DirRight) != 0;
                in.up = (newlyPressed & gp::DirUp) != 0;
                in.down = (newlyPressed & gp::DirDown) != 0;
            }
            in.heldZ = zDown;
            in.heldX = xDown;
            in.heldLeft = (rawDirs & gp::DirLeft) != 0;
            in.heldRight = (rawDirs & gp::DirRight) != 0;
            in.heldUp = (rawDirs & gp::DirUp) != 0;
            in.heldDown = (rawDirs & gp::DirDown) != 0;
            scripts.tickScene(in);
            scripts.tick(); // wait(n) in the scene's scripts
            if (scripts.sceneCloseRequested() && !pendingScreen)
                requestScreen([&]
                              {
                                  scripts.endScene();
                                  sceneActive = false;
                              });
            return; // world frozen while a scene is up
        }
        if (minigames.active())
        {
            MinigameInput in;
            in.live = fade.a <= 0.0; // ignore presses during the fade-in
            in.z = zPressed;
            in.x = xPressed;
            in.left = (newlyPressed & gp::DirLeft) != 0;
            in.right = (newlyPressed & gp::DirRight) != 0;
            in.up = (newlyPressed & gp::DirUp) != 0;
            in.down = (newlyPressed & gp::DirDown) != 0;
            minigames.tick(in);
            return; // world frozen while a minigame is up
        }
        if (!im.open)
            sellMode = false; // left with X (or nothing left to sell): sell mode is over
        if (im.open && !dialogue.open()) // a box asked from the item screen is answered first
        {
            itemMenuTick(newlyPressed, xPressed, menuPressed, zPressed, zReleased);
            if (itemLeave)
            {
                itemLeave = false;
                requestScreen([&]
                              { im.open = false; barBusy = 0; barAnim.visible = false; }); // the item screen fades out too
            }
            if (zPressed && im.open && im.tabs.inGrid())
                useSelectedItem();
            return; // world frozen while the item screen is up
        }
        walk::showNextSay(svc); // text a script queued with say(), once nothing else is being said
        walk::sideQuestTick(svc); // side quests that end by themselves, or that a script finished
        if (dialogue.open())
        {
            gp::DialogueBox &b = *dialogue.box;
            if (b.kind == gp::BoxKind::Confirm)
            {
                if (newlyPressed & gp::DirLeft)
                    b.yesSelected = true; // Yes is on the left
                if (newlyPressed & gp::DirRight)
                    b.yesSelected = false;
                if (zPressed)
                    dialogue.choose(b.yesSelected ? 1 : 2); // don't use b after this
            }
            else if (b.kind == gp::BoxKind::Quantity)
            {
                if (newlyPressed & (gp::DirRight | gp::DirUp))
                    b.qty = std::min(b.qty + 1, b.qtyMax);
                if (newlyPressed & (gp::DirLeft | gp::DirDown))
                    b.qty = std::max(b.qty - 1, 1);
                if (zPressed)
                    dialogue.pick(b.qty); // don't use b after this
                else if (xPressed)
                    dialogue.choose(2); // cancel with X: a guess
            }
            else if (zPressed || (b.kind == gp::BoxKind::Picture && xPressed)) // the map goes with any button
                dialogue.choose(1);
            return;
        }
        if (room)
        {
            if (!im.open) // the item screen is closed (it was left with X): give mode is over
                giveMode = false;
            if (bar.isOpen()) // the house bar: the same keys as outside
            {
                barHost.pressed = zDown;
                if (newlyPressed & gp::DirLeft)
                    bar.handleEvent(0, true);
                if (newlyPressed & gp::DirRight)
                    bar.handleEvent(1, true);
                if (zPressed)
                    bar.handleEvent(4, true);
                else if (xPressed)
                    bar.cancel(barBusy == 2);
            }
            // The pet stands still, and Z and X belong to the bar, while it is opening, open or closing.
            const bool barHasKeys = barBusy != 0 || bar.isOpen();
            if (!barHasKeys && !dialogue.open())
                roomTutorial(); // the first time by the bed or bowl: a message instead of whatever Z would have done
            tickRoom(barHasKeys ? 0 : rawDirs, barHasKeys ? 0 : newlyPressed, !barHasKeys && zPressed, !barHasKeys && xPressed);
            tickStatNotice(); // the panel works inside houses too
            return;
        }

        if (barBusy == 4 || barBusy == 5) // the tricks panel takes the keys (nothing while it opens or closes)
        {
            if (barBusy == 4 && !panelClosing)
            {
                int ev = -1;
                if (newlyPressed & gp::DirLeft)
                    ev = 0;
                else if (newlyPressed & gp::DirRight)
                    ev = 1;
                else if (newlyPressed & gp::DirUp)
                    ev = 2;
                else if (newlyPressed & gp::DirDown)
                    ev = 3;
                if (zPressed)
                    ev = 4;
                else if (xPressed)
                    ev = 5;
                if (gp::trickClosesPanel(ev))
                    closeTrickPanel();
                else if (const int slot = gp::trickSlotForEvent(ev); slot >= 0)
                    pickTrick(slot);
            }
        }
        else if (bar.isOpen())
        {
            barHost.pressed = zDown;
            if (newlyPressed & gp::DirLeft)
                bar.handleEvent(0, true);
            if (newlyPressed & gp::DirRight)
                bar.handleEvent(1, true);
            if (zPressed)
                bar.handleEvent(4, true);
            else if (xPressed)
                bar.cancel(barBusy == 2);
        }
        // The pet can't be controlled while the bar is opening, open, or closing (g_isPetBusy != 0).
        // The world keeps running.
        const bool petBusyElsewhere = barBusy != 0 || feedStep != 0 || digStep != 0 || swiftDropStep != 0 || washStep != 0 || showNpc || tutStep != 0 || potionStep != 0 || barkStep != 0 || trickStep != 0 || questionStep != 0 || colorStep != 0 || holdTicks > 0;
        // Sitting: a direction or the action button gets the pet up first (activity 8) instead of moving it; the
        // action press is swallowed. Nothing is accepted while it sits down or gets up (see pet_sit.hpp).
        auto sitNow = [&]
        { return gp::sitPhase(sp, animId, pose == Pose::Idle, pose == Pose::Special, animDone); };
        if (!petBusyElsewhere && gp::startsGetUp(sitNow(), rawDirs != 0, zPressed))
            playAnim(gp::artOf(sp).sitAlt, false, 14, Pose::Special); // PlayActorReactionAnimation: one-shot at 14
        const bool petLocked = petBusyElsewhere || gp::blocksInput(sitNow());
        const uint8_t dirs = petLocked || (scriptLock & 1) ? 0 : rawDirs;
        if (xPressed && !petLocked && !(scriptLock & 2) && !bar.isOpen() && !dialogue.open() && !enc && !im.open && !pendingExit && !pendingScreen)
            pressCancelInWorld();
        if (showNpc)
        {
            gp::NpcActor &t = *showNpc;
            const int species = t.def->speciesOrAnim == 0xfffe || !gp::knownSpecies(t.def->speciesOrAnim) ? sp : t.def->speciesOrAnim;
            if (!showStarted)
            {
                if (t.settled()) // wait for any walk or turn it was in the middle of
                {
                    showStarted = true;
                    showcase.start(showLevel, species, sp);
                }
            }
            else
            {
                gp::TrickShowcase::Host sh;
                sh.play = [&](int id, int rate)
                {
                    const gp::Id full = reactAnimId(id, t.def->appearancePalette);
                    if (animRefById(full).size() == 0)
                        return false;
                    t.forceAnim(full);
                    t.forcedRate = rate;
                    t.forcedFlip = false; // trick art is drawn facing the camera
                    return true;
                };
                sh.busy = [&]
                { return !t.forcedAnimId.empty() && t.animBusy(); };
                if (showcase.tick(sh))
                {
                    t.locked = false;
                    t.clearForced();
                    showNpc = nullptr;
                    std::function<void()> done = std::move(showDone);
                    showDone = nullptr;
                    if (done)
                        done();
                }
            }
        }
        if (enc)
        {
            enc->tick(host);
            if (enc->done())
            {
                const gp::NpcDef &d = *encNpc->def;
                const bool friend_ = enc->outcome() == gp::EncounterSequence::Outcome::Friend;
                encNpc->locked = false;
                enc.reset();
                encNpc = nullptr;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                // the engine system "encounter.finish": the encounter is over; a friendly outcome rolls for friendship. `chance` is the rules' number, `friend` the answer
                // (set it to decide; it is 0 if the outcome was not friendly and no mod says otherwise)
                gp::SystemCtx fc;
                fc.set("npc", d.id).set("outcome", friend_ ? "friend" : "other").set("chance", gp::befriendChance(d.befriendOdds, player.personality[gp::Stars], d.stars)).set("friend", 0);
                scripts.system("encounter.finish", fc, [&]
                               { fc.set("friend", friend_ && gp::befriendRoll(npcRng, int(fc.num("chance"))) ? 1 : 0); });
                if (fc.num("friend") != 0)
                    walk::grantFriendship(svc, d, false);
            }
            if (enc && encNpc)
            {
                static int dbg = 0;
                if (++dbg % 30 == 0)
                    std::printf("step %d npc.busy=%d forced=%s key=%d frame=%d\n",
                                enc->step(), int(host.npc.busy()), encNpc->forcedAnimId.c_str(),
                                encNpc->animKey(), encNpc->anim.currentFrame);
            }
        }
        // TickAllPets: the NPC the pet can talk to (nearestNpc, the same test as pressing Z) stops and turns to
        // face the pet; the pet's side is (npcX <= petX), as in the ROM.
        const gp::NpcActor *talkNpc = nearestNpc();
        // An idle NPC performing a trick (activity 0x13, TickPetNPCBehavior): the same sequence the pet and the teachers
        // run, with the sounds only when the NPC is on screen.
        auto tickNpcTrick = [&](gp::NpcActor &n)
        {
            if (n.activity != gp::kActivityTrick)
                return;
            const int s = n.def->speciesOrAnim == 0xfffe ? sp : n.def->speciesOrAnim;
            if (n.locked || n.def->stationary || !gp::knownSpecies(s) || n.trickLevel < 1 || n.trickLevel > 3)
            {
                n.endTrick(npcRng);
                return;
            }
            if (!n.trickStarted)
            {
                // the engine system "npc.sequence" (kind "trick"): an idle NPC starts a trick; `level` (1..3) and `repeats` may be changed; cancel or replace and it does not
                gp::SystemCtx sc;
                sc.set("npc", n.def->id).set("kind", "trick").set("level", n.trickLevel).set("repeats", gp::trickRepeatCount(n.trickLevel, s, npcRng));
                const bool ran = scripts.system("npc.sequence", sc, [&]
                                                {
                    n.trickStarted = true;
                    n.body.facing = gp::FaceDown;
                    n.trickLevel = std::clamp(int(sc.num("level")), 1, 3);
                    n.trick.startWith(n.trickLevel, s, std::max(0, int(sc.num("repeats")))); });
                if (!ran)
                {
                    n.endTrick(npcRng);
                    return;
                }
            }
            const bool seen = std::abs(n.x() - pos.x) < W / 2 + 40 && std::abs(n.y() - pos.y) < 150;
            auto start = [&](int id, int rate, bool reverse)
            {
                const gp::Id full = reactAnimId(id, n.def->appearancePalette);
                if (animRefById(full).size() == 0)
                    return false;
                n.forceAnim(full);
                n.forcedRate = rate;
                n.forcedReverse = reverse;
                // the monkey's turn-around art faces left, so it is mirrored when it turns to its right
                n.forcedFlip = (id == gp::TrickShowcase::turnAroundAnim(s) || id == gp::TrickShowcase::directionalAnim(s)) && n.trickFlip;
                n.trickAnimId = id;
                return true;
            };
            gp::TrickShowcase::Host th;
            th.full = true;
            th.play = [&](int id, int rate)
            { return start(id, rate, false); };
            th.playReverse = [&](int id, int rate, bool)
            { return start(id, rate, true); };
            th.busy = [&]
            { return !n.forcedAnimId.empty() && n.animBusy(); };
            th.anim = [&]
            { return n.trickAnimId; };
            th.frame = [&]
            { return n.anim.currentFrame; };
            th.setFacing = [&](bool faceRight)
            { n.trickFlip = faceRight; };
            th.cameraPan = [&]
            {
                if (seen)
                    cameraShake.start();
            };
            th.sound = [&](int id, int vol)
            {
                if (seen)
                    sfx(id, vol);
            };
            th.soundActive = [&](int id)
            { return gp::audio().sfxActive(id); };
            th.stopSound = [&](int id)
            { gp::audio().stopSfx(id); };
            th.rand = [&](int lo, int hi)
            { return lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive
            th.seconds = [&]
            { return double(gameTicks) / 60.0; };
            if (n.trick.tick(th))
                n.endTrick(npcRng);
        };
        // An idle NPC's call, happy hop or yawn (activities 1, 2 and 7): the same ReactionSeq the encounters use, run on
        // this NPC by pointing the encounter NPC's hooks at it for the tick.
        auto tickNpcReaction = [&](gp::NpcActor &n)
        {
            if (n.activity != gp::kActivityNoise && n.activity != gp::kActivityHappy && n.activity != gp::kActivityBored)
                return;
            if (n.locked || n.def->stationary || n.def == (encNpc ? encNpc->def : nullptr))
            {
                n.endTrick(npcRng);
                return;
            }
            if (!n.react)
            {
                // the engine system "npc.sequence" (kind "call", "happy" or "yawn"): an idle NPC starts one; `count` (how many times) may be changed; cancel or replace and it does not
                gp::SystemCtx sc;
                sc.set("npc", n.def->id).set("kind", n.activity == gp::kActivityNoise ? "call" : n.activity == gp::kActivityHappy ? "happy" : "yawn").set("count", n.reactCount);
                const bool ran = scripts.system("npc.sequence", sc, [&]
                                                {
                    n.body.facing = gp::FaceDown;
                    using K = gp::ReactionSeq::Kind;
                    n.react.emplace(n.activity == gp::kActivityNoise ? K::Noise : n.activity == gp::kActivityHappy ? K::Positive : K::Bored,
                                    std::max(0, int(sc.num("count"))), n.reactVariant); });
                if (!ran)
                {
                    n.endTrick(npcRng);
                    return;
                }
            }
            gp::NpcActor *saved = encNpc;
            encNpc = &n;
            npcMute = !(std::abs(n.x() - pos.x) < W / 2 + 40 && std::abs(n.y() - pos.y) < 150);
            const bool done = n.react->tick(host.npc);
            npcMute = false;
            encNpc = saved;
            if (done)
                n.endTrick(npcRng);
        };
        for (gp::NpcActor &n : npcs)
        {
            // A "sick NPC" quest (typeMode 2) makes its receiver sick for as long as it is accepted and not handed in.
            n.questSick = quests.q.def.typeMode == 2 && quests.q.phase != 0 && quests.q.def.receiver == n.def->index;
            n.selected = &n == talkNpc;
            n.petOnRight = n.x() <= pos.x;
            if (scripts.hasSystem("npc.tick")) // the engine system "npc.tick": wrap or replace the NPC's own brain (systems.hpp)
            {
                gp::SystemCtx nc;
                nc.set("id", n.def->id).set("x", n.x()).set("y", n.y()).set("activity", n.activity).set("stationary", n.def->stationary ? 1 : 0).set("scripted", n.scripted ? 1 : 0);
                scripts.system("npc.tick", nc, [&]
                               { n.tick(npcRng); });
                if (int(nc.num("x")) != n.x() || int(nc.num("y")) != n.y()) // a mod placed it
                {
                    n.body.x = int(nc.num("x"));
                    n.body.y = int(nc.num("y"));
                }
            }
            else
                n.tick(npcRng);
            if (n.def->stationary && n.def->behaviorFlags != gp::kNoOverheadAnim) // TickAllPets: the overhead animation
            {
                if (n.overheadTick >= 0)
                {
                    const int frames = int(animRefById(reactAnimId(n.def->behaviorFlags, n.def->appearancePalette)).size());
                    if (++n.overheadTick >= gp::overheadTicks(frames))
                        n.overheadTick = -1;
                }
                else if (gp::overheadStarts(npcRng))
                    n.overheadTick = 0;
            }
            tickNpcTrick(n);
            tickNpcReaction(n);
            updateNpcAnim(n);
        }
        // The stink spreads to whoever the pet stands beside (npc_stink.hpp).
        {
            gp::NpcActor *beside = nullptr;
            for (gp::NpcActor &n : npcs)
                if (&n == talkNpc)
                    beside = &n;
            const bool roamer = beside && !beside->def->stationary; // a stationary NPC's flags are not moods
            switch (gp::stinkTransferStep(stinkXfer, quests.counter, player.care[gp::Cleanliness], roamer ? beside->def->index : -1,
                                          roamer && beside->stinky))
            {
            case gp::StinkEvent::NpcStinks:
                beside->stinky = true;
                break;
            case gp::StinkEvent::PetStinks:
                player.adjustCare(gp::Cleanliness, -gp::tuning().stinkHygieneDrop);
                saveGame();
                break;
            case gp::StinkEvent::None:
                break;
            }
        }

        // ---- movement + facing + walk animation ----
        if (dirs)
        {
            medStuck = false;  // real input ends the stuck walk
            pose = Pose::Move; // any direction input cancels idle/special animations
            settleTicks = 0;
            // speedMul is a setting a mod can change (engine.set "pet.speedMul"); the debug sprint multiplies it
            const int steps = std::clamp(speedMul, 0, 256) * ((gp::debugMode() && (SDL_GetModState() & KMOD_SHIFT)) ? 4 : 1);
            // the engine system "pet.move" (systems.hpp): a mod can change which way and how far the pet steps, do the move itself (set ctx.x / ctx.y), or cancel it
            gp::SystemCtx mc;
            mc.set("x", pos.x).set("y", pos.y).set("steps", steps);
            mc.set("dx", (dirs & gp::DirRight ? 1 : 0) - (dirs & gp::DirLeft ? 1 : 0)).set("dy", (dirs & gp::DirDown ? 1 : 0) - (dirs & gp::DirUp ? 1 : 0));
            const auto dirsOf = [&]
            {
                const int dx = int(mc.num("dx")), dy = int(mc.num("dy"));
                return uint8_t((dx > 0 ? gp::DirRight : 0) | (dx < 0 ? gp::DirLeft : 0) | (dy > 0 ? gp::DirDown : 0) | (dy < 0 ? gp::DirUp : 0));
            };
            scripts.system("pet.move", mc, [&]
                           {
                               const uint8_t d = dirsOf();
                               gp::Point p{int(mc.num("x")), int(mc.num("y"))};
                               for (int rep = 0, n = std::clamp(int(mc.num("steps")), 0, 256); rep < n && d; ++rep)
                                   p = gp::stepPlayer(*world, p, d).pos;
                               mc.set("x", p.x).set("y", p.y); });
            pos = {std::clamp(int(mc.num("x")), 0, std::max(0, cur->room.width - 1)), std::clamp(int(mc.num("y")), 0, std::max(0, cur->room.height - 1))};
            const uint8_t moved = dirsOf(); // the way it went, after the mods: the pet faces that way
            const int oldFacing = facing;
            const bool oldLeft = faceLeft;
            if (moved & (gp::DirLeft | gp::DirRight))
            {
                facing = 2;
                faceLeft = (moved & gp::DirLeft) != 0;
            }
            else if (moved)
                facing = (moved & gp::DirUp) ? 0 : 1;
            // The game only calls UpdateActorSpriteFrame (which restarts the walk loop via
            // PlaySpriteAnimation) when walking starts or the direction changes, not every tick.
            if (facing != oldFacing || faceLeft != oldLeft)
                walkTicks = 0;
            // The game runs this on each button press event (not while a key stays held), so every
            // new press restarts the walk loop at frame 0, even when tapping the same direction.
            if (newlyPressed)
                walkTicks = 0;
            ++walkTicks; // the game animates while a direction is held, even when blocked by a wall or object
        }
        else if (pose != Pose::Move && pose != Pose::Wait)
            walkTicks = 0; // (while Move/Wait the walk loop keeps its phase)

        // ---- just stopped walking ----
        if (!dirs && pose == Pose::Move && !enc && !medStuck)
        {
            if (facing == 1)
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle); // facing down: straight to the base idle, no wait, no turn
            else
            {
                pose = Pose::Wait; // sideways / up: the walk loop keeps playing while the 0.15 s settle timer runs
                settleTicks = 0;
            }
        }
        if (pose == Pose::Wait)
        {
            ++walkTicks; // the walk animation keeps advancing during the wait (the game doesn't freeze it)
            if (++settleTicks >= kSettleTicks)
            {
                // After walking up the 180 degree turn is 52 then 53; after sideways it is just 53; then 34.
                if (facing == 0)
                {
                    // the game picks the turn side at random: RandRange(1,3) == 1 -> left (1/3), otherwise right (2/3)
                    faceLeft = (std::rand() % 3) == 0;
                    playAnim(gp::artOf(sp).turn180, false, 14, Pose::Settle, {{gp::artOf(sp).turn90, false, 14}, {restIdleId(), true, restIdleRate()}});
                }
                else
                    playAnim(gp::artOf(sp).turn90, false, 14, Pose::Settle, {{restIdleId(), true, restIdleRate()}});
            }
        }

        // The idle follows how the pet is: it changes when the pet falls sick or unhappy, or gets better.
        if (pose == Pose::Idle && isRestIdle(animId) && animId != restIdleId() && animLoop)
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);

        // ---- idle timer: after 3-6 s of base idle the pet sits (action 7: 46 once, then 45 looping) ----
        if (pose == Pose::Idle && isRestIdle(animId) && !enc && !digStep && !washStep && ++idleTicks >= idleThreshold)
        {
            // the engine system "pet.sit": the idle pet is about to sit down; cancel or replace and it keeps standing (the timer starts over)
            gp::SystemCtx c;
            c.set("idle", idleTicks / 30.0);
            const bool ran = scripts.system("pet.sit", c, [&]
                                            { playAnim(gp::artOf(sp).sitDown, false, 14, Pose::Special,
                                                       {{gp::artOf(sp).settled, true, gp::artOf(sp).settledRate}}); });
            if (!ran)
                idleTicks = 0;
        }

        // ---- advance the current animation (rate = frames per second, same rule as walking) ----
        if (pose != Pose::Move && pose != Pose::Wait)
        {
            const int n = int(getAnim(animId).size());
            if (n > 0)
            {
                ++animTicks;
                const int idx = animTicks * animRate / 30;
                if (animLoop)
                    animFrame = idx % n;
                else if (idx >= n)
                {
                    animFrame = animReverse ? 0 : n - 1;
                    animDone = true;
                }
                else
                    animFrame = animReverse ? n - 1 - idx : idx;
            }
            else if (!animLoop)
                animDone = true; // nothing to show, don't get stuck

            if (animDone)
            {
                if (!queue.empty())
                {
                    const AnimStep s = queue.front();
                    std::vector<AnimStep> rest(queue.begin() + 1, queue.end());
                    playAnim(s.id, s.loop, s.rate, rest.empty() ? Pose::Idle : Pose::Settle, rest);
                }
                // don't drop to idle between steps (the wash's happy reaction restarts it itself)
                else if (!enc && !feedStep && !digStep && !washStep && !potionStep && !colorStep && !trickStep) // (a trick plays its own steps)
                    playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            }
        }
        if (medStuck)
        {
            ++walkTicks; // keep the walk loop animating in place
            if (++medStuckTicks >= medStuckLimit)
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle); // playAnim clears medStuck
        }
        if (overId >= 0)
            ++overTicks;
        // The engine system "pet.activity" (activity_system.hpp): each running activity of the pet takes its step through it
        const auto activity = [&](const char *kind, int &step, const std::function<void()> &tick) { gp::runActivity(scripts, kind, step, tick); };
        activity("feed", feedStep, [&] { tickFeed(); });
        activity("dig", digStep, [&] { tickDig(); });
        tickHoleFills();
        activity("wash", washStep, [&] { tickWash(); });
        activity("drop", swiftDropStep, [&] { tickSwiftDrop(); });
        if (gp::updateStatGates(player, db.gatesIn(cur->def->legacyId), quests.counter)) // CheckStatsAndMoneyGate
        {
            refreshBits();
            saveGame();
        }
        if (holdTicks > 0 && (--holdTicks == 0 || zPressed)) // the reward activity's wait; Z skips it
        {
            holdTicks = 0;
            if (holdNpc)
                holdNpc->locked = false;
            holdNpc = nullptr;
            std::function<void()> d = std::move(holdDone);
            holdDone = nullptr;
            if (d)
                d();
        }
        tickStatNotice();
        tickWorldTutorial();
        activity("potion", potionStep, [&] { tickPotion(); });
        activity("bark", barkStep, [&] { tickBark(); });
        activity("trick", trickStep, [&] { tickTrick(); });
        activity("question", questionStep, [&] { tickQuestion(); });
        tickBlasts();
        shakeDy = cameraShake.step(); // AdvanceScriptedCameraPanStep
        activity("colour", colorStep, [&] { tickColorMachine(zDown); });
        tickThoughtBubble();
        quests.tick(int(gameTicks));
        scripts.tick(); // scripts that called wait(n)
        // Area.hooks(id).onTick: every tick the world is running (not under a menu, a dialogue, a house room or a screen change)
        static long scriptTicks = 0; // 30 a second, like dt and wait(); counts every tick, also the ones a menu skips
        ++scriptTicks;
        if (!dialogue.open() && !im.open && !statsOpen && !room && !pendingScreen)
        {
            gp::SceneInput ain; // the keys, for a script that holds the action buttons (player.lock) and reads them itself
            ain.z = zPressed;
            ain.x = xPressed;
            ain.left = newlyPressed & gp::DirLeft;
            ain.right = newlyPressed & gp::DirRight;
            ain.up = newlyPressed & gp::DirUp;
            ain.down = newlyPressed & gp::DirDown;
            ain.heldZ = zDown;
            ain.heldX = xDown;
            ain.heldLeft = rawDirs & gp::DirLeft;
            ain.heldRight = rawDirs & gp::DirRight;
            ain.heldUp = rawDirs & gp::DirUp;
            ain.heldDown = rawDirs & gp::DirDown;
            scripts.fireAreaTick(cur->def->id, scriptTicks, ain);
            for (size_t k = 0; k < npcs.size(); ++k) // Npc.hooks(id).onTick: the script that drives an NPC looks at it every tick
            {
                const gp::NpcActor &n = npcs[k];
                scripts.fireNpcTick(n.def->id, n.x(), n.y(), n.body.facing, n.walking(), n.scripted, scriptTicks);
            }
        }
        // scene.open(id) from a script: open it as soon as the world is free (not in a dialogue, a menu, a house room or a talk)
        if (!sceneActive && !dialogue.open() && !im.open && !statsOpen && !room && !pendingScreen && !pendingExit && !enc && !showNpc && barBusy == 0)
            if (std::string sceneId; scripts.takeSceneOpen(sceneId))
                requestScreen([&, sceneId]
                              {
                                  sceneActive = true;
                                  scripts.beginScene(sceneId);
                              });
        if (!petBusyElsewhere && !enc && !dialogue.open() && !im.open && !statsOpen && !pendingExit && !pendingScreen && !room && !showNpc)
        {
            const gp::NpcActor *nb = nearestNpc();
            if (nudge.due(long(gameTicks), player.questsCompleted, quests.q.phase, nb ? nb->def->index : -1))
            {
                tutNudge = true; // activity 0x26 with event -1
                startWorldTutorial(-1);
            }
        }

        // ---- first-time messages: by an item, an object or a pet (HandlePetStageEvents), the first rest while sick,
        // unhappy or dirty (TickPlayerActorIdleBehavior), and the first pickup (ProcessStoryModeObjectInteraction) ----
        if (!petBusyElsewhere && !enc && !dialogue.open() && !pendingExit && !pendingScreen)
        {
            const gp::ShopEvent *shopT = nearestShopItem();
            const int itemIdx = events.nearestPrompt(cur->def->legacyId, pos.x, pos.y);
            const gp::ObjectDef *objT = cur->runtime->nearestInteractable(pos.x, pos.y);
            const bool itemNear = shopT || itemIdx >= 0;
            const int item = shopT ? shopT->item : itemIdx >= 0 ? int(events.events()[size_t(itemIdx)].item) : -1;
            int event = gp::worldTutorial(settings.tutorials, player.tutorialsShown, itemNear, item, objT ? objT->legacySlot : -1,
                                          nearestNpc() != nullptr);
            if (event < 0 && pose == Pose::Idle && !afterPickup)
                event = gp::idleTutorial(settings.tutorials, player.tutorialsShown, !player.isHealthy(),
                                         player.care[gp::Happiness] < gp::kUnhappyBelow,
                                         player.care[gp::Cleanliness] <= gp::kDirtyAtMost);
            if (event < 0 && afterPickup)
            {
                afterPickup = false;
                if (gp::isFirstTime(settings.tutorials, player.tutorialsShown, gp::kTutorialStuff))
                    event = gp::kTutorialStuff;
            }
            if (event >= 0)
                startWorldTutorial(event);
        }

        // ---- exit trigger / talk to NPC ----
        const gp::ObjectDef *nearObj = cur->runtime->nearestInteractable(pos.x, pos.y);
        const bool zWorld = zPressed && !petLocked && !(scriptLock & 2) && tutStep == 0;
        const int curArea = cur->def->legacyId;
        const gp::ShopEvent *shopItem = zWorld ? nearestShopItem() : nullptr;
        const int pickIdx = (zWorld && !shopItem) ? events.nearestPrompt(curArea, pos.x, pos.y) : -1;
        const gp::ExitDef *ie = (zWorld && nearObj && !shopItem && pickIdx < 0) // not while the pet is busy
                                    ? db.findExit(cur->def->id, nearObj->id, false)
                                    : nullptr;
        const gp::ExitDef *ex = nearObj ? db.findExit(cur->def->id, nearObj->id, true) : nullptr;

        // debug: press O to dump the nearest interactable object
        static bool prevO = false;
        const bool oDown = gp::debugMode() && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_O];
        if (oDown && !prevO)
            for (uint32_t a = 0xe393; a < 0xe540; ++a)
            {
                const int id = rom.u16(a);
                std::printf("%x: %d (0x%x) %s\n", a, id, id,
                            db.textOr("base:text/" + std::to_string(id), "").c_str());
            }
        prevO = oDown;

        if (ie)
            requestGoTo(*ie);
        else if (!ex)
        {
            exitsArmed = true;
            blockedExit.clear();
        }
        else if (exitsArmed)
            requestGoTo(*ex);

        const bool outdoors = cur->def->diggable; // base areas 0..9 (a GUESS for the ROM's 0x3c969(area) check); a mod area opts in

        if (zWorld && !ie && !enc && !showNpc)
        {
            if (shopItem)
                buyFromShop(*shopItem);
            else if (pickIdx >= 0)
                tryPickup(pickIdx);

            else if (nearObj && interactions.interact(cur->def->id, *nearObj))
            {
            }
            else if (gp::NpcActor *t = nearestNpc())
                walk::talkToNpc(svc, *t);
            else if (outdoors)
                if (!scripts.fireBeforeDig(cur->def->id, pos.x, pos.y).suppress) // a mod may call the dig off
                    startDig(); // misses and duds play the no-item reaction
        }
    }
};
