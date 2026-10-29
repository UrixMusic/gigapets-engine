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
#include "reaction_seq.hpp"
#include "sprite_anim.hpp"
#include "trick_showcase.hpp"
#include <optional>
#include <vector>

namespace gp
{

    constexpr int kNpcFollowsPath = 0x800; // behaviorFlags bit
    // behaviorFlags (g_petFlags, copied at spawn) of a roaming NPC: bit 0 sick idle, bit 1 sad idle, 0x100 stinky (stink cloud).
    // A stationary NPC's word is a bubble anim id instead, so these never apply to it.
    constexpr int kNpcSick = 0x1, kNpcSad = 0x2, kNpcStinky = 0x100;
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
        // Activity 0x13, a trick: the level, and the sequence that plays it (driven by the game, which owns the art).
        int trickLevel = 0, trickAnimId = -1;
        bool trickStarted = false, trickFlip = false; // trickFlip: the monkey's turn-around is to its right
        TrickShowcase trick;
        int overheadTick = -1; // a stationary NPC's overhead animation: ticks since it began, -1 = not playing
        // Activities 1, 2 and 7: the call, the happy hop and the yawn, a ReactionSeq the game ticks on this NPC.
        int reactCount = 0, reactVariant = 1;
        std::optional<ReactionSeq> react;
        // Running a sequence the game drives: nothing may cut it short until the game calls endTrick.
        bool sequenced() const { return activity == kActivityTrick || activity == kActivityNoise || activity == kActivityHappy || activity == kActivityBored; }
        enum Settle
        {
            SettleNone,
            SettleTurn180,
            SettleTurn90
        };
        int settle = SettleNone; // turn-to-idle sequence after a wander stop (key 4, then key 5)
        bool locked = false;     // held by an encounter: settles, but never wanders or picks a new activity
        // TickAllPets: the NPC the pet can talk to stops, turns to face the pet and idles for as long as the pet stays
        // in range. walk.cpp sets `selected` and `petOnRight` before every tick(); `facingPet` is this file's state.
        bool selected = false;   // this NPC is the one in the talk box (the same test as pressing Z)
        bool petOnRight = false; // the ROM sets the NPC's facing to (npcX <= petX): right if the pet is on its right
        bool facingPet = false;  // standing idle, turned sideways toward the pet (animKey 7)
        // Moods. The record's flag bits are the starting state; a "sick NPC" quest (typeMode 2) makes its receiver sick while
        // it is active (SetPetActiveFlagAndResetIdle), and the stink spreads to a roamer standing near a stinky pet.
        bool questSick = false;
        bool stinky = false;
        bool sick() const { return !def->stationary && ((def->behaviorFlags & kNpcSick) || questSick); }
        bool sad() const { return !def->stationary && (def->behaviorFlags & kNpcSad); }

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
            stinky = !d.stationary && (d.behaviorFlags & kNpcStinky) != 0;
        }

        int x() const { return body.x; }
        int y() const { return body.y; }

        std::string forcedAnimId;
        bool forcedFlip = false;
        int forcedRate = 14; // frame rate of the forced anim (the trick showcase uses 0xc / 0xe)
        // The forced anim plays last frame to first (flag 4). SelectAndPlayReactionAnim (0x015462) does this for every
        // actor's outro, so an NPC's happy reaction settles down instead of crouching and rising again.
        bool forcedReverse = false;

        bool animBusy() { return isAnimationBusy(&anim); }
        void forceAnim(const std::string &id)
        {
            forcedAnimId = id;
            forcedReverse = false; // the caller sets it after, for outros
            anim.animId = -1;
        } // -1 makes setAnim restart even for the same id
        void clearForced()
        {
            forcedAnimId.clear();   // was: forcedAnim = -1;
            forcedRate = 14;
            forcedReverse = false;
            anim.animId = -1;
        }

        bool walking() const { return moves && activity == kActivityWander; }

        // Animation key for walk.cpp: 0 idle, 1 walk up, 2 walk down, 3 walk sideways, 4 turn 180, 5 turn 90,
        // 6 forced reaction anim, 7 sideways idle facing the pet, 8 sick idle, 9 sad idle. UpdateNPCIdleActivity picks the
        // idle by the flags (sick first), whether or not the NPC is looking at the pet, and those idles have no facing.
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
            if (sick())
                return 8;
            if (sad())
                return 9;
            if (facingPet)
                return 7;
            return 0;
        }

        // Leave the current activity and begin `id`. Stopping at a waypoint (activity 9) plays the turn sequence:
        // up -> 180 turn then 90 turn, sideways -> 90 turn, down -> nothing. Every sequence ends facing down.
        void startActivity(int id, Rng &rng)
        {
            settle = SettleNone;
            if (id == kActivityWander)
                facingPet = false; // walking sets its own facing
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

        // A trick or reaction is over (or cannot go on): back to the default idle.
        void endTrick(Rng &rng)
        {
            clearForced();
            trickStarted = false;
            trickAnimId = -1;
            trick = TrickShowcase{};
            react.reset();
            startActivity(defaultActivity(def->behaviorFlags), rng);
        }

        // The original's "reset action" (1 in 99 per tick): drop whatever is running, decide again next tick.
        void resetAction(Rng &rng)
        {
            walk.actionId = 0;
            walk.phase = 0;
            startActivity(defaultActivity(def->behaviorFlags), rng);
            activityTicks = 0;
        }

        // Standing still and not mid-turn: plain idle, or idling toward the pet.
        bool settled() const { return animKey() == 0 || animKey() == 7 || animKey() == 8 || animKey() == 9; }

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
            if (!locked)
            {
                // TickAllPets: the selected NPC, when wandering or between activities, stops where it stands, faces the
                // pet's side and runs the idle action (0x19), again every tick, so it follows the pet if it crosses over.
                // Other actions (a trick, a turn in progress) run to the end first. The 1 in 99 reset below only happens
                // off screen in the ROM, so a selected NPC never gets one.
                const bool defaultIdle = activity == kActivityIdle || activity == kActivityIdleA || activity == kActivityIdleB ||
                                         activity == kActivityTalkedTo; // the idle after stopping at a waypoint
                if (selected && settle == SettleNone && (activity == kActivityWander || defaultIdle || activityTicks <= 0))
                {
                    if (activity == kActivityWander)
                        walk.actionId = 0;
                    body.facing = petOnRight ? FaceRight : FaceLeft;
                    activity = kActivityIdle;
                    activityTicks = 1; // nothing new is picked while the pet is here
                    facingPet = !(sick() || sad()); // the sick and sad idles have no side view, so there is no turn to undo later
                    return;
                }
                // No longer selected: activity 9 (TickActor_SettleFacing) turns it back to face the front, then it
                // carries on as usual.
                if (!selected && facingPet && settle == SettleNone)
                {
                    facingPet = false;
                    settle = SettleTurn90;
                    return;
                }
            }
            // One roll per tick, and never while an encounter holds the NPC: a reset here would snap its facing and
            // activity out from under the encounter's animations.
            if (!locked && !sequenced() && rng.range(1, 100) == 50)
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
            const ActivityPick pick = pickActivity(rng.range(1, 101), def->behaviorFlags);
            int id = pick.id;
            if (id == kActivityWander && !moves)
                id = -1;
            startActivity(id < 0 ? defaultActivity(def->behaviorFlags) : id, rng);
            if (id == kActivityTrick)
            {
                trickLevel = pick.param;
                trickStarted = false;
                activityTicks = 1 << 24; // runs until the trick is done: the game calls endTrick
            }
            else if (id == kActivityNoise || id == kActivityHappy || id == kActivityBored)
            {
                reactCount = id == kActivityBored ? 1 : reactionRepeatCount(rng);
                reactVariant = id == kActivityHappy ? happyVariant(rng) : 1;
                react.reset();
                activityTicks = 1 << 24; // runs until the sequence is done
            }
        }

        // Start (or keep) an animation. Restarts when the id or the facing changes, like UpdateActorSpriteFrame.
        void setAnim(int animId, int frameCount, int rate = 14, bool loop = true, bool reverse = false)
        {
            if (anim.animId == animId && animFacing == body.facing)
                return;
            playSpriteAnimation(anim, animId, -1, body.x, body.y, rate, (loop ? 2u : 0u) | (reverse ? 4u : 0u), 0x1000, frameCount, 0, rate);
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