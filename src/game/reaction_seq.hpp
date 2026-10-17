// The per-actor reaction sequences of the encounter; pet and NPC run the same code through an ActorIO.
// Ports of TickActor_IdleRandomSound, PlayPositive/NegativeReactionAnim, PlayEmoteReactionAnim and the sad
// sequence. tick() = one game tick (30 Hz); it returns true once, when the sequence is finished.
#pragma once
#include <functional>

namespace gp
{
    enum class ReactAnim
    {
        Idle,
        Intro,
        Outro,
        Positive,
        Negative,
        NegIntro,
        NegOutro,
        Emote,
        Shocked,
        Bored,
        Sad
    };

    struct ActorIO
    {
        std::function<bool(ReactAnim, int)> play; // start it; false = the actor has no such animation
        std::function<bool()> busy;               // IsAnimationBusy
        std::function<void(int, int)> sound;      // (category, id): placeholder until there is audio
        std::function<void(int)> bubble;          // SpawnMessageBubble(id)
        std::function<bool()> settle;             // TickActor_SettleFacing: true once facing down
        std::function<int(int, int)> rand;        // RandRange(lo, hi), inclusive
    };

    class ReactionSeq
    {
    public:
        enum class Kind
        {
            Noise,
            Positive,
            Negative,
            Emote,
            Sad,
            Bored
        };
        static constexpr int kGapTicksPerUnit = 3; // GUESS: noise gap = RandRange(2,6) / 10 s at 30 ticks/s
        static constexpr int kHalfSecond = 15;     // 0.5 s (0x3f000000), tick counter / 59.94

        ReactionSeq(Kind k, int count = 0, int variant = 1) : kind_(k), count_(count), variant_(variant) {}

        bool tick(ActorIO &io)
        {
            switch (kind_)
            {
            case Kind::Noise:
                return noise(io);
            case Kind::Positive:
                return positive(io);
            case Kind::Negative:
                return negative(io);
            case Kind::Emote:
                return emote(io);
            case Kind::Sad:
                return sad(io);
            case Kind::Bored:
                return bored(io);
            }
            return true;
        }

    private:
        bool noise(ActorIO &io)
        {
            switch (step_)
            {
            case 1:
                left_ = count_;
                step_ = 2;
                break;
            case 2:
            {
                io.play(ReactAnim::Idle, 0);
                const int r = io.rand(1, 4); // 2 -> (1,4); 3 -> (1,0xbe); 1 or 4 -> (2,0xbe)
                if (r == 2)
                    io.sound(1, 4);
                else
                    io.sound(r == 3 ? 1 : 2, 0xbe);
                step_ = 3;
                break;
            }
            case 3:
                if (io.busy())
                    break;
                if (--left_ < 1)
                    return true;
                wait_ = io.rand(2, 6) * kGapTicksPerUnit;
                step_ = 4;
                break;
            case 4:
                if (--wait_ <= 0)
                    step_ = 2;
                break;
            }
            return false;
        }

        bool positive(ActorIO &io)
        {
            switch (step_)
            {
            case 1:
                left_ = count_;
                step_ = io.play(ReactAnim::Intro, variant_) ? 2 : 3;
                break;
            case 2:
                if (!io.busy())
                    step_ = 3;
                break;
            case 3:
                io.play(ReactAnim::Positive, variant_);
                io.sound(4, 0xbe);
                step_ = 4;
                break;
            case 4:
                if (io.busy())
                    break;
                if (--left_ >= 1)
                {
                    step_ = 3;
                    break;
                }
                step_ = io.play(ReactAnim::Outro, variant_) ? 5 : 6;
                break;
            case 5:
                if (!io.busy())
                    step_ = 6;
                break;
            case 6:
                return true;
            }
            return false;
        }

        bool negative(ActorIO &io)
        {
            switch (step_)
            {
            case 1:
                left_ = count_;
                special_ = io.rand(2, count_);
                io.play(ReactAnim::NegIntro, 1);
                step_ = 2;
                break;
            case 2:
                if (!io.busy())
                    step_ = 3;
                break;
            case 3:
                io.play(ReactAnim::Negative, left_ == special_ ? 1 : variant_);
                io.sound(7, 0xbe);
                step_ = 4;
                break;
            case 4:
                if (io.busy())
                    break;
                if (--left_ >= 1)
                {
                    step_ = 3;
                    break;
                }
                io.play(ReactAnim::NegOutro, 0);
                step_ = 5;
                break;
            case 5:
                if (!io.busy())
                    return true;
                break;
            }
            return false;
        }

        bool emote(ActorIO &io)
        {
            switch (step_)
            {
            case 1:
                if (variant_ == 3) // PlayReactionFollowupAnim: outro-style animation first, then the emote
                {
                    variant_ = 7;
                    if (io.play(ReactAnim::Outro, 0))
                    {
                        step_ = 5;
                        break;
                    }
                }
                io.play(ReactAnim::Emote, 0);
                io.sound(9, 0xff);
                io.bubble(0xb);
                step_ = 2;
                break;
            case 5:
                if (!io.busy())
                    step_ = 1;
                break;
            case 2:
                if (!io.busy())
                {
                    wait_ = kHalfSecond;
                    step_ = 3;
                }
                break;
            case 3:
                if (--wait_ > 0)
                    break;
                if (!io.play(ReactAnim::Shocked, 0))
                    return true;
                step_ = 4;
                break;
            case 4:
                if (!io.busy())
                    return true;
                break;
            }
            return false;
        }

        bool sad(ActorIO &io) // TickNPCReactionAnimationSequence: sad, 0.5 s, sad
        {
            switch (step_)
            {
            case 1:
                io.play(ReactAnim::Sad, 0);
                io.sound(0xb, 0xbe);
                step_ = 2;
                break;
            case 2:
                if (!io.busy())
                {
                    wait_ = kHalfSecond;
                    step_ = 3;
                }
                break;
            case 3:
                if (--wait_ > 0)
                    break;
                io.play(ReactAnim::Sad, 0);
                step_ = 4;
                break;
            case 4:
                if (!io.busy())
                    return true;
                break;
            }
            return false;
        }

        bool bored(ActorIO &io)
        {
            switch (step_)
            {
            case 1:
                if (io.settle())
                    step_ = 2;
                break; // TickActor_SettleFacing until facing down
            case 2:
                io.play(ReactAnim::Bored, 0);
                io.sound(8, 0xa0);
                step_ = 3;
                break;
            case 3:
                if (!io.busy())
                    return true;
                break;
            }
            return false;
        }

        Kind kind_;
        int count_, variant_;
        int step_ = 1, left_ = 0, special_ = 0, wait_ = 0;
    };
}