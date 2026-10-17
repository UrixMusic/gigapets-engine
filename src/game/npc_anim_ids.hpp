// npc_anim_ids.hpp - which animation id an actor plays, per species / facing / style.
// Read off the Ghidra decompiles of the actor animation functions (0x014bde..0x0151fe). In all of them:
//   actor[2] = facing (0 left, 1 right, 2 up, 3 down)   actor[3] = style flag (0 = overworld, !=0 = house/indoor art)
//   actor[4] = species (0 dog, 1 monkey, 2 panda)        actor[5] = duration override
// Sprite bank: 0x1000 normal, 0x1004 mirrored (Ghidra prints 0x1004 as "&g_npcSpawnParam2" because that RAM label
// happens to sit at 0x1004). `mirror` below means "bank 0x1004".
// Rates: 14 unless noted. Everything else (anim 49, 46, 29 ...) comes from functions not dumped yet.
#pragma once
#include "npc_walk.hpp"
#include "reaction_seq.hpp"
#include <vector>
#include <algorithm>

namespace gp
{

    struct AnimChoice
    {
        int id = -1;
        bool mirror = false;
        int rate = 14;
        bool loop = false;
    };

    // Selector tables from the decompiles. tier = actor[4] (0 for the pet); side = facing left/right, else facing down.
    static int reactId(gp::ReactAnim a, int tier, int variant, bool side)
    {
        static const int posS[3] = {0x1f, 0x86, 0xee}, posD[3] = {0x20, 0x87, 0xed};
        static const int emoS[3] = {0x2b, 0x90, 0xf7}, emoD[3] = {0x28, 0x91, 0xf6};
        static const int shkS[3] = {0x2c, 0x92, -1}, shkD[3] = {0x27, 0x93, -1};
        static const int neg1[3] = {0x19, 0x7d, 0xe4}, neg2[3] = {0x1a, 0x7e, 0xe5}, negIO[3] = {0x32, 0x98, 0xff};
        static const int sad[3] = {0x25, 0x8f, 0xf4}, bored[3] = {0x39, 0xa0, 0x107};
        switch (a)
        {
        case gp::ReactAnim::Positive:
            return side ? posS[tier] : posD[tier];
        case gp::ReactAnim::Emote:
            return side ? emoS[tier] : emoD[tier];
        case gp::ReactAnim::Shocked:
            return side ? shkS[tier] : shkD[tier];
        case gp::ReactAnim::Negative:
            return variant == 1 ? neg1[tier] : neg2[tier];
        case gp::ReactAnim::NegIntro:
        case gp::ReactAnim::NegOutro:
            return negIO[tier];
        case gp::ReactAnim::Intro:
        case gp::ReactAnim::Outro:
            return tier == 2 ? (side ? 0x101 : 0x100) : -1;
        case gp::ReactAnim::Sad:
            return sad[tier];
        case gp::ReactAnim::Bored:
            return bored[tier];
        case gp::ReactAnim::Idle:
        {
            static const int idS[3] = {0x30, 0x97, 0xfe}, idD[3] = {0x2f, 0x96, 0xfd};
            return side ? idS[tier] : idD[tier];
        }
        default:
            return -1; // Idle (the noise): PlayActorIdleAnimation not decompiled yet
        }
    }

#include "reaction_seq.hpp"
#include <vector>
#include <algorithm>

    // (move reactId here unchanged, as inline)

    inline std::vector<int> reactIdsForTier(int tier)
    {
        std::vector<int> out;
        for (int a = int(ReactAnim::Idle); a <= int(ReactAnim::Sad); ++a)
            for (int variant = 1; variant <= 2; ++variant)
                for (int side = 0; side < 2; ++side)
                {
                    const int id = reactId(ReactAnim(a), tier, variant, side != 0);
                    if (id >= 0 && std::find(out.begin(), out.end(), id) == out.end())
                        out.push_back(id);
                }
        return out;
    }

    // Mirror rule for reaction art: dog and monkey art faces left, panda faces right.
    inline bool reactMirror(int tier, bool faceRight) { return tier == 2 ? !faceRight : faceRight; }

    // ---- UpdateActorSpriteFrame (0x014bde): walking, loops at 14 ----
    constexpr int kWalkUp[3] = {54, 157, 230};
    constexpr int kWalkDown[3] = {55, 158, 236};
    constexpr int kWalkSide[3] = {56, 159, 250};      // art faces right; left = mirrored
    constexpr int kWalkSideStyle[3] = {78, 181, 276}; // style != 0: sideways only, no up/down art
    constexpr int kSitDown[3] = {46, 154, 260};
    constexpr int kSitAlt[3] = {49, 130, 234};
    constexpr int kEat[3] = {29, 127, 232};

    inline AnimChoice walkAnim(int sp, int facing, bool style)
    {
        AnimChoice c;
        c.loop = true;
        if (sp < 0 || sp > 2)
            return c;
        if (style)
        {
            if (facing == FaceLeft || facing == FaceRight)
            {
                c.id = kWalkSideStyle[sp];
                c.mirror = facing == FaceLeft;
            }
            return c;
        }
        switch (facing)
        {
        case FaceLeft:
            c.id = kWalkSide[sp];
            c.mirror = true;
            break;
        case FaceRight:
            c.id = kWalkSide[sp];
            break;
        case FaceUp:
            c.id = kWalkUp[sp];
            break;
        case FaceDown:
            c.id = kWalkDown[sp];
            break;
        }
        return c;
    }

    // ---- PlayIdleAnim_Scripted (0x014c8a): standing idle; loop flag is an argument; rate 14 ----
    constexpr int kIdleBase[3] = {34, 139, 235};  // facing up/down
    constexpr int kIdleSide[3] = {42, 140, 249};  // facing left/right; dog+monkey art faces left, panda art faces right
    constexpr int kIdleStyle[3] = {61, 165, 268}; // style != 0, any facing

    inline AnimChoice idleAnim(int sp, int facing, bool style, bool loop = true)
    {
        AnimChoice c;
        c.loop = loop;
        if (sp < 0 || sp > 2)
            return c;
        if (style)
        {
            c.id = kIdleStyle[sp];
            return c;
        }
        if (facing == FaceLeft || facing == FaceRight)
        {
            c.id = kIdleSide[sp];
            const bool artFacesLeft = sp != 2;
            c.mirror = artFacesLeft ? facing == FaceRight : facing == FaceLeft;
        }
        else
            c.id = kIdleBase[sp];
        return c;
    }

    // ---- turning toward the camera after walking (turn art is drawn facing left; mirrored when facing == right) ----
    constexpr int kTurn180[3] = {52, 155, 261}; // PlayTurnAroundIdleAnim (0x015144), after walking up. no style table
    constexpr int kTurn90[3] = {53, 156, 262};  // PlayDirectionalIdleAnim (0x0150f1), after walking sideways
    constexpr int kTurn90Style[3] = {76, 179, 284};
    constexpr int kTurn180Style[3] = {72, 178, 283}; // UpdateActorSpriteFrame2 (0x0151fe); assumed to be the style turn-around

    inline AnimChoice turn180(int sp, int facing, bool style = false)
    {
        AnimChoice c;
        if (sp < 0 || sp > 2)
            return c;
        c.id = style ? kTurn180Style[sp] : kTurn180[sp];
        c.mirror = !style && facing == FaceRight;
        return c;
    }
    inline AnimChoice turn90(int sp, int facing, bool style = false)
    {
        AnimChoice c;
        if (sp < 0 || sp > 2)
            return c;
        c.id = style ? kTurn90Style[sp] : kTurn90[sp];
        c.mirror = facing == FaceRight;
        return c;
    }

    // ---- PlaySettledIdleAnim (0x014dd2): the resting pose after standing still (dog: sit idle). Rate dog 7, others 5 ----
    constexpr int kSettled[3] = {45, 149, 251};
    constexpr int kSettledStyle[3] = {69, 173, 277};
    inline AnimChoice settledIdle(int sp, bool style = false, bool loop = true)
    {
        AnimChoice c;
        c.loop = loop;
        if (sp < 0 || sp > 2)
            return c;
        c.id = style ? kSettledStyle[sp] : kSettled[sp];
        c.rate = sp == 0 ? 7 : 5;
        return c;
    }

    // ---- other idle variants, no facing (flag arguments pick loop): rate 7 (FlagB: dog 14) ----
    constexpr int kIdleFlagA[3] = {38, 142, 245};
    constexpr int kIdleFlagAStyle[3] = {65, 168, 273}; // PlayIdleAnim_FlagA 0x014d7b
    constexpr int kIdleFlagB[3] = {41, 148, 248};
    constexpr int kIdleFlagBStyle[3] = {68, 172, 275}; // PlayIdleAnim_FlagB 0x014d1c

    // ---- PlayActorIdleAnimation (0x014f96), one-shot at 14. Dog facing down = 47 = the bark. Up has no art. ----
    constexpr int kCallFront[3] = {47, 150, 253}; // facing down
    constexpr int kCallSide[3] = {48, 151, 254};  // facing left/right (dog+monkey art faces left, panda right)
    constexpr int kCallStyle[3] = {73, 175, 279};

} // namespace gp