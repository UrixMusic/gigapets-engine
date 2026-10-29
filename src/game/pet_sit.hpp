// pet_sit.hpp - the pet sitting down after idling, and getting back up before it can move.
//
// The ROM (HandlePetStageEvents 0x01308a): with the pet sitting (g_petInteractionMode == 1), a held direction or the
// action button does NOT move the pet. It starts pet activity 8 (WaitFacingThenReactAndReset 0x016033) and skips
// movement that frame. Activity 8 is TickActor_ReactionState (0x01e5ea): play the get-up animation
// (PlayActorReactionAnimation 0x01505b: 49 / 130 / 234, one-shot at rate 14), wait for it, clear the sitting flag.
// Then the pet is back to normal, and a direction that is still held walks. The action button is swallowed.
//
// Sitting down is activity 7 (TickActor_PlayAnimationAndWait 0x01e749): the sit-down animation (46 / 154 / 260), then the
// sitting idle (45 / 149 / 251), and the sitting flag is set only once the sit-down has finished. The pet cannot be
// controlled during either animation.
//
// The sit phase is read off the pet's current animation, so there is no extra state to keep in step.
#pragma once
#include "npc_anim_ids.hpp"

namespace gp
{

    enum class SitPhase
    {
        None,        // standing, or doing something else
        SittingDown, // the sit-down animation is playing (activity 7)
        Sitting,     // resting in the sitting idle (the sitting flag is set)
        GettingUp    // the get-up animation is playing (activity 8)
    };

    // species sp, the pet's current animation id and pose. idlePose = a resting pose, specialPose = a one-shot activity.
    inline SitPhase sitPhase(int sp, int animId, bool idlePose, bool specialPose, bool animDone)
    {
        if (sp < 0 || sp > 2)
            return SitPhase::None;
        if (specialPose && animId == kSitDown[sp])
            return SitPhase::SittingDown;
        if (specialPose && animId == kSitAlt[sp] && !animDone)
            return SitPhase::GettingUp;
        if (idlePose && animId == kSettled[sp])
            return SitPhase::Sitting;
        return SitPhase::None;
    }

    // A sitting pet that is asked to move (or to act) gets up first.
    inline bool startsGetUp(SitPhase p, bool directionHeld, bool actionPressed)
    {
        return p == SitPhase::Sitting && (directionHeld || actionPressed);
    }

    // Input is ignored while the pet sits down or gets up.
    inline bool blocksInput(SitPhase p) { return p == SitPhase::SittingDown || p == SitPhase::GettingUp; }

} // namespace gp
