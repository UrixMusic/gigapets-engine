// game/encounter_sequence.hpp
// The befriending encounter: port of UpdateNPCEncounter / UpdateNPCEncounterSequence as a state machine.
// Step numbers are the original's g_petSequenceStep values, so the engine's log can be compared with a MAME log
// (MAME frames = 2 x engine ticks). Pure logic: walk.cpp (the host) moves the pet, plays animations and sounds.
//
// Timings: MEASURED = from one MAME run of an angry encounter (frames / 2). PLACEHOLDER = a guess, not measured.
#pragma once
#include "encounter.hpp"
#include <cstdio>
#include <functional>
#include <vector>
#include "reaction_seq.hpp"
#include <memory>

namespace gp
{

    // Everything the sequence needs from the game. walk.cpp fills these in; every member must be set.
    struct EncounterHost
    {
        std::function<int()> petX, npcX, npcY;
        std::function<bool(int)> petWalkToX; // true when the pet is at x (TickActor_WalkToTargetX)
        std::function<bool(int)> petWalkToY; // true when the pet is at y (AnimateActorMoveToTarget)
        std::function<void(bool)> petFace;   // face sideways, true = left
        ActorIO pet, npc;
        std::function<void()> petEngage;        // step 2: facing up -> sideways toward the NPC, then idle
        std::function<void()> petIdle, npcIdle; // HandleNPCInteractionInput
        std::function<void()> npcFacePet;       // SetNPCApproachSide at the start of the NPC's noise
        std::function<bool()> npcSettled;
        std::function<bool()> npcLock;   // hold the NPC; true if it was walking and has to settle first
        std::function<void()> npcUnlock, npcNoise;
        std::function<void(int)> npcReact;
        std::function<int()> markFriended;            // returns the number of friends afterwards
        std::function<void(const char *, int)> event; // sounds, bubbles, pending events (placeholders)
    };

    class EncounterSequence
    {
    public:
        enum class Outcome
        {
            Pending,
            Friend,
            Negative,
            Neutral
        };

        static constexpr int kAnimalReactAt = 90; // PLACEHOLDER: when the NPC reacts inside step 0xb (random in the original)
        static constexpr int kSettleTicks = 11;   // MEASURED: steps 0x15 and 0x16 took about 22 frames each
        static constexpr int kGiveUpTicks = 120;   // PLACEHOLDER: "a few seconds", not measured
        static constexpr int kApproachDX = 0x4b;  // step 6: stand 75 px from the NPC, on the pet's side
        static constexpr int kCloserDX = 0x14;    // step 0xc, friend: step 20 px closer
        static constexpr int kAwayDX = 0x1e;      // step 0xc, negative: walk 30 px away

        std::unique_ptr<ReactionSeq> petSeq_, npcSeq_;
        int npcDelay_ = 0;

        static std::unique_ptr<ReactionSeq> makeReaction(int cat, int count, bool isNpc)
        {
            using K = ReactionSeq::Kind;
            switch (cat)
            {
            case 1:
            case 2:
                return std::make_unique<ReactionSeq>(K::Positive, count, cat == 1 ? 1 : 2);
            case 4:
            case 5:
                return std::make_unique<ReactionSeq>(K::Negative, count, isNpc ? (cat == 4 ? 2 : 3) : (cat == 4 ? 1 : 2));
            case 6:
                return std::make_unique<ReactionSeq>(K::Bored);
            case 3:
            case 7:
                return std::make_unique<ReactionSeq>(K::Emote, 0, cat);
            }
            return nullptr;
        }

        // petDelta / npcDelta = personalityStatDelta(halos, pitchforks) of the pet and of the NPC's requirements.
        EncounterSequence(int petDelta, int npcDelta, bool verboseLog = true) : verbose(verboseLog), pet_(petDelta), npc_(npcDelta) { enter(2); }

        bool done() const { return step_ == 0; }
        int step() const { return step_; }
        Outcome outcome() const { return outcome_; }

        bool verbose;             // prints "STEP,<step>,<MAME-equivalent frames>" on every step change
        std::vector<int> history; // the steps visited, in order

        void tick(EncounterHost &h)
        {
            if (done())
                return;
            ++t_;
            ++timer_;
            ++total_;
            switch (step_)
            {
            case 2:
                timer_ = 0;
                h.petEngage();
                enter(3);
                break;
            case 3: // idle NPCs are locked at once; a wandering one settles its facing first (step 4)
                enter(h.npcLock() ? 4 : 6);
                break;
            case 4:
                if (h.npcSettled())
                    enter(6);
                break;
            case 6: // pick the side the pet is already on
                targetX_ = h.npcX() < h.petX() ? h.npcX() + kApproachDX : h.npcX() - kApproachDX;
                enter(7);
                break;
            case 7: // walk to the side; the idle timer started in step 2 makes the pet give up
                if (h.petWalkToX(targetX_) || gaveUp())
                    enter(8);
                break;
            case 8:
                if (h.petWalkToY(h.npcY()) || gaveUp())
                {
                    h.petFace(h.npcX() < h.petX());
                    h.petIdle();
                    petSeq_ = std::make_unique<ReactionSeq>(ReactionSeq::Kind::Noise, h.pet.rand(2, 5)); // check the bounds in the listing
                    enter(9);
                }
                break;
            case 9:
                if (petSeq_->tick(h.pet))
                {
                    h.petIdle();
                    h.npcFacePet();
                    npcSeq_ = std::make_unique<ReactionSeq>(ReactionSeq::Kind::Noise, h.pet.rand(2, 5));
                    enter(10);
                }
                break;
            case 10:
                if (npcSeq_->tick(h.npc))
                {
                    h.npcIdle();
                    playerCat_ = classifyReaction(pet_, npc_);
                    animalCat_ = 0;
                    if (playerCat_ == 2)
                        h.event("bubble", 0x18);
                    petSeq_ = makeReaction(playerCat_, h.pet.rand(3, 6), false);
                    if (!petSeq_)
                        playerCat_ = 8;
                    npcSeq_.reset();
                    npcDelay_ = h.pet.rand(5, 10) * 3; // RandRange(5,10) / 10 s at 30 ticks/s
                    enter(0xb);
                }
                break;
            case 0xb:
                if (playerCat_ != 8 && petSeq_ && petSeq_->tick(h.pet))
                {
                    h.petIdle();
                    playerCat_ = 8;
                }
                if (animalCat_ == 0 && t_ >= npcDelay_)
                {
                    animalCat_ = classifyCompat(pet_, npc_);
                    if (animalCat_ == 2)
                        h.event("bubble_npc", 0x18);
                    npcSeq_ = makeReaction(animalCat_, h.pet.rand(2, 6), true);
                    if (!npcSeq_)
                        animalCat_ = 8;
                }
                if (npcSeq_ && animalCat_ != 8)
                {
                    if ((animalCat_ == 4 || animalCat_ == 5) && (playerCat_ == 1 || playerCat_ == 2))
                    {
                        playerCat_ = playerCat_ == 2 ? 3 : 7; // a happy pet turns sad / startled
                        petSeq_ = makeReaction(playerCat_, 0, false);
                    }
                    if (npcSeq_->tick(h.npc))
                    {
                        h.npcIdle();
                        animalCat_ = 8;
                    }
                }
                if (playerCat_ == 8 && animalCat_ == 8)
                    enter(0xc);
                break;

            case 0xc: // resolve (the categories are recomputed here, like the original)
            {
                const int pc = classifyReaction(pet_, npc_), ac = classifyCompat(pet_, npc_);
                const int px = h.petX(), nx = h.npcX();
                if (ac == 4 || ac == 5) // negative: walk away from the NPC. (AdjustCareStat(Happiness, ?): delta not read yet)
                {
                    timer_ = 0;
                    targetX_ = nx < px ? px + kAwayDX : px - kAwayDX;
                    outcome_ = Outcome::Negative;
                    enter(0x14);
                }
                else if ((ac == 1 || ac == 2) && pc != 4 && pc != 5) // friend
                {
                    timer_ = 0;
                    targetX_ = nx < px ? px - kCloserDX : px + kCloserDX;
                    outcome_ = Outcome::Friend;
                    const int friends = h.markFriended();
                    h.event("pending_event", 0xa3);
                    h.event("sound", 0x54);
                    if (const int ms = milestoneEvent(friends))
                        h.event("pending_event", ms);
                    enter(0xd);
                }
                else // neutral: straight to the settle steps
                {
                    outcome_ = Outcome::Neutral;
                    enter(0xe);
                }
                break;
            }
            case 0xd:
                if (h.petWalkToX(targetX_) || gaveUp())
                    enter(0xe);
                break;
            case 0xe:
                if (t_ >= kSettleTicks && h.npcSettled())
                {
                    h.npcUnlock();
                    enter(0xf);
                }
                break;

            case 0xf:
                if (h.pet.settle())
                    finish();
                break;
            case 0x14: // negative path (UpdateNPCEncounterSequence)
                if (h.petWalkToX(targetX_) || gaveUp())
                    enter(0x15);
                break;
            case 0x15:
                if (t_ >= kSettleTicks && h.npcSettled())
                {
                    h.npcUnlock();
                    enter(0x16);
                }
                break;
            case 0x16:
                if (h.pet.settle())
                {
                    petSeq_ = std::make_unique<ReactionSeq>(ReactionSeq::Kind::Sad);
                    enter(0x17);
                }
                break;
            case 0x17:
                if (petSeq_->tick(h.pet))
                {
                    h.petIdle();
                    finish();
                }
                break;
            default:
                finish();
                break;
            }
        }

        static int milestoneEvent(int friends) // pending events spawned at 10, 20, 30, 35 and 40 friends
        {
            switch (friends)
            {
            case 10:
                return 0xc4;
            case 20:
                return 0x10;
            case 30:
                return 0x0d;
            case 35:
                return 0x5d;
            case 40:
                return 0x11;
            default:
                return 0;
            }
        }

    private:
        bool gaveUp() const { return timer_ >= kGiveUpTicks; }
        void enter(int s)
        {
            step_ = s;
            t_ = 0;
            history.push_back(s);
            if (verbose)
                std::printf("STEP,%d,%d\n", s, total_ * 2);
        }
        void finish()
        {
            step_ = 0;
            if (verbose)
                std::printf("STEP,done,%d\n", total_ * 2);
        }

        int pet_, npc_;
        int step_ = 0, t_ = 0, timer_ = 0, total_ = 0;
        int targetX_ = 0, playerCat_ = 0, animalCat_ = 0;
        Outcome outcome_ = Outcome::Pending;
    };

} // namespace gp