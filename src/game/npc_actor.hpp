// npc_actor.hpp - runtime NPC state. Reads NpcDef only; no ROM.
// Glue between the ported engine pieces:
//   npc_spawn.hpp  -> initial position / facing / wander state (initNpcInstance)
//   wander.hpp     -> which waypoint to head for next (wanderTarget)
//   npc_walk.hpp   -> pixel-exact stepping toward that waypoint (tickWalk, 30 Hz)
//   npc_brain.hpp  -> what to do next (activity roll, pauses); decides when to wander and when to stand idle
//   sprite_anim.hpp-> animation timer / current frame (advanceSpriteAnimFrame)
// Path semantics: path[i] is an offset from the spawn point; index starts at 0 (see wander.hpp).
#pragma once
#include "../content/records.hpp"
#include "npc_brain.hpp"
#include "npc_spawn.hpp"
#include "npc_walk.hpp"
#include "sprite_anim.hpp"
#include <vector>

namespace gp
{

    constexpr int kNpcFollowsPath = 0x800; // behaviorFlags bit
    static_assert(kActivityWander == kActWanderPath, "activity 0x17 and walk action 0x17 are the same id");

    struct NpcActor
    {
        const NpcDef *def;
        WalkActor body; // x, y (ints, feet anchor), facing, style
        WalkState walk;
        WanderState wander;
        PathPoint target{0, 0}; // world-space waypoint currently being walked to
        std::vector<PathPoint> path;
        bool pingPong = false, moves = false;
        SpriteAnimState anim;
        int animFacing = -1;
        int activity = kActivityIdle; // current activity id (npc_brain.hpp)
        int activityTicks = 0;        // ticks left in a non-wander activity
        bool started = false;
        enum Settle
        {
            SettleNone,
            SettleTurn180,
            SettleTurn90
        };
        int settle = SettleNone; // turn-to-idle sequence after a wander stop (key 4, then key 5)
        bool locked = false;     // held by an encounter: settles, but never wanders or picks a new activity

        explicit NpcActor(const NpcDef &d) : def(&d)
        {
            NpcSpawnInfo info;
            info.spawnX = d.x;
            info.spawnY = d.y;
            info.behaviorFlags = d.behaviorFlags;
            info.stationary = d.stationary;
            const NpcInstanceInit init = initNpcInstance(info, 0);
            body.x = init.x;
            body.y = init.y;
            body.facing = init.facing;
            body.style = init.style;
            wander = init.wander;
            for (const auto &p : d.path)
                path.push_back({p.x, p.y});
            pingPong = d.pingPong;
            moves = !d.stationary && (d.behaviorFlags & kNpcFollowsPath) && !path.empty();
            target = {body.x, body.y};
            activity = init.pendingActivity; // 0x19 / 0x1b / 0x1d: the first thing the NPC does
        }

        int x() const { return body.x; }
        int y() const { return body.y; }

        std::string forcedAnimId;
        bool forcedFlip = false;

        bool animBusy() { return isAnimationBusy(&anim); }
        void forceAnim(const std::string &id)
        {
            forcedAnimId = id;
            anim.animId = -1;
        } // -1 makes setAnim restart even for the same id
        void clearForced()
        {
            forcedAnimId.clear();   // was: forcedAnim = -1;
            anim.animId = -1;
        }

        bool walking() const { return moves && activity == kActivityWander; }

        // Animation key for walk.cpp: 0 idle, 1 walk up, 2 walk down, 3 walk sideways, 4 turn 180, 5 turn 90.
        // Mirroring: key 3 flips when facing left; keys 4/5 (art drawn facing left) flip when facing right.
        int animKey() const
        {
            if (!forcedAnimId.empty())   // was: if (forcedAnim >= 0)
                return 6;
            if (walking())
                return body.facing == FaceUp ? 1 : body.facing == FaceDown ? 2
                                                                           : 3;
            if (settle == SettleTurn180)
                return 4;
            if (settle == SettleTurn90)
                return 5;
            return 0;
        }

        // Leave the current activity and begin `id`. Stopping at a waypoint (activity 9) plays the turn sequence:
        // up -> 180 turn then 90 turn, sideways -> 90 turn, down -> nothing. Every sequence ends facing down.
        void startActivity(int id, Rng &rng)
        {
            settle = SettleNone;
            if (activity == kActivityWander && id != kActivityWander)
            {
                walk.actionId = 0;
                if (id == kActivityTalkedTo && body.facing == FaceUp)
                {
                    body.facing = rng.range(1, 4) == 1 ? FaceLeft : FaceRight; // the pet picks the side like this (1 in 3 left)
                    settle = SettleTurn180;
                }
                else if (id == kActivityTalkedTo && (body.facing == FaceLeft || body.facing == FaceRight))
                {
                    settle = SettleTurn90;
                }
                else
                    body.facing = FaceDown;
            }
            activity = id;
            activityTicks = id == kActivityWander ? 0 : idleTicks(rng);
        }

        // The original's "reset action" (1 in 99 per tick): drop whatever is running, decide again next tick.
        void resetAction(Rng &rng)
        {
            walk.actionId = 0;
            walk.phase = 0;
            startActivity(defaultActivity(def->behaviorFlags), rng);
            activityTicks = 0;
        }

        bool settled() const { return animKey() == 0; }

        // One game tick (30 Hz). Stationary NPCs do nothing here (they only animate).
        void tick(Rng &rng)
        {
            if (def->stationary)
                return;
            if (!started)
            {
                started = true;
                activityTicks = idleTicks(rng);
            }
            if (rng.range(1, 100) == 50)
                resetAction(rng); // d153

            if (!started)
            {
                started = true;
                activityTicks = idleTicks(rng);
            }
            if (!locked && rng.range(1, 100) == 50)
                resetAction(rng); // d153

            if (activity == kActivityWander && moves)
            {
                if (walk.actionId != kActWanderPath) // between legs: choose the next waypoint
                    target = wanderTarget(wander, path, pingPong, def->x, def->y, body.x, body.y);
                const bool done = tickWalk(body, walk, target.dx, target.dy);
                if (done && rng.range(1, 4) == 1) // cd5d: 1 in 3 stops at the waypoint
                    startActivity(kActivityTalkedTo, rng);
                return;
            }
            if (settle != SettleNone)
            { // waiting for the turn animation
                if (!isAnimationBusy(&anim))
                {
                    if (settle == SettleTurn180)
                        settle = SettleTurn90;
                    else
                    {
                        settle = SettleNone;
                        body.facing = FaceDown;
                        activityTicks = idleTicks(rng);
                    }
                }
                return;
            }
            if (locked)
                return;
            if (activityTicks > 0)
            {
                --activityTicks;
                return;
            }

            // The action is over: roll for the next one (ce64). 22 in 100 pick something, the rest default to idle.
            int id = pickActivity(rng.range(1, 101), def->behaviorFlags).id;
            if (id == kActivityWander && !moves)
                id = -1;
            startActivity(id < 0 ? defaultActivity(def->behaviorFlags) : id, rng);
        }

        // Start (or keep) an animation. Restarts when the id or the facing changes, like UpdateActorSpriteFrame.
        void setAnim(int animId, int frameCount, int rate = 14, bool loop = true)
        {
            if (anim.animId == animId && animFacing == body.facing)
                return;
            playSpriteAnimation(anim, animId, -1, body.x, body.y, rate, loop ? 2u : 0u, 0x1000, frameCount, 0, rate);
            animFacing = body.facing;
        }
        void advanceAnim(int frameCount)
        {
            if (frameCount <= 0)
                anim.flags |= 1; // missing art: count a one-shot as finished so a sequence cannot hang
            advanceSpriteAnimFrame(anim, frameCount);
        }
    };

} // namespace gp