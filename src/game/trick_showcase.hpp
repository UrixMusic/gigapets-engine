// trick_showcase.hpp - a pet or NPC performing a trick: TickPetNPCBehavior (0x01ebc5).
//
// The teacher shows the trick off before offering the lesson (step 5 of TickLearnTrickEncounterSequence1..3, 0x016ef8 /
// 0x01733f / 0x017795), and the player's own pet performs it from the tricks panel (activity 0xd,
// TickActor_NPCEncounterSequence 0x016167). What it does, in its own states:
//   1. TryPlayTrickAnimVariant (0x014ec8): an optional intro animation (rate 0xe). Most entries are "none";
//      if there is none, skip to 3.
//   2. wait until the animation is done.
//   3. PlayTrickAnimVariant (0x014e31): the trick itself (rate 0xc).
//   4. while it plays: sounds for some species/level pairs (below). When it is done: repeats--; if repeats < 1 the trick is
//      over, else play the trick again.
//   5. PlayReactionAnim_State2 (0x014f2b): a closing animation for a few pairs (rate 0xe, the panda's played last frame to
//      first), then 6: wait for it.
// So the trick plays exactly `repeats` times. Both animation tables are indexed [level][species] (species 0, 1, 2).
//
// The sounds and the closing animation are the "full" sequence (Host::full). The teacher's showcase runs without them, as
// before; the player's pet runs it in full. Sounds, with the pair they belong to (species / level):
//   1 / 1  0x5b once, 0.6 s after the trick starts
//   2 / 2  0x2e (and a scripted camera pan) while the animation is on frame 12
//   2 / 3  while 0xf0 plays and none of 0x35/0x36/0x37 is sounding: one of them (1 in 3 each); between plays of
//          0xf0, 1 in 3 each: reaction 0xf1 with 0x3a, or reaction 0xf2 with 0x38; a finished 0xf2 waits 1.0..3.0 s and plays
//          0xf3 with 0x39
//   1 / 3  0x1a while animation 0x89 plays
// After the closing animation: the monkey (level 2) turns around to a random side (PlayTurnAroundIdleAnim) and back to the
// front (PlayDirectionalIdleAnim); the panda at level 1, which has no closing animation, plays the actor reaction
// animation (PlayActorReactionAnimation) as its exit. The panda's level 2 trick (the belly flop) shakes the screen: while
// it is on frame 12 it starts the scripted camera pan (StartScriptedCameraPan 0x01f5f3), a 27-entry table the game steps
// once per tick (AdvanceScriptedCameraPanStep 0x01f5d3) into DAT_000005, the camera's vertical lead (the camera is at
// pet.y - (DAT_000005 + 0x78), and 0x19 is the rest value, so the entries are offsets from 0x19). See CameraShake.
#pragma once
#include <functional>
#include <initializer_list>

namespace gp
{

    // The scripted camera pan (screen shake): the table at RAM 0x219f, read in MAME. Entries are the camera's vertical lead
    // for one tick each; 0x19 is the rest value.
    class CameraShake
    {
    public:
        static constexpr int kRest = 0x19, kLength = 27;
        static constexpr int kTable[kLength] = {0x1b, 0x1e, 0x1a, 0x16, 0x15, 0x19, 0x1b, 0x1c, 0x1a, 0x18, 0x16, 0x19, 0x1a, 0x1b,
                                                0x19, 0x18, 0x17, 0x19, 0x19, 0x1a, 0x19, 0x18, 0x19, 0x1a, 0x19, 0x1a, 0x19};

        // StartScriptedCameraPan: ignored in a pan's first three entries; otherwise (idle, or later in a pan) it starts from the
        // top (the game compares the index, -1 included, as unsigned: `2 < index`).
        void start()
        {
            if (index_ < 0 || index_ > 2)
                index_ = 0;
        }
        // AdvanceScriptedCameraPanStep, once per tick; returns the offset from the rest value, in pixels.
        int step()
        {
            if (index_ >= 0)
            {
                if (index_ < kLength)
                {
                    lead_ = kTable[index_];
                    ++index_;
                }
                else
                    index_ = -1;
            }
            return lead_ - kRest;
        }
        bool active() const { return index_ >= 0; }

    private:
        int index_ = -1, lead_ = kRest;
    };

    class TrickShowcase
    {
    public:
        static constexpr int kIntroRate = 0xe, kMainRate = 0xc, kReactionRate = 0xc;

        // What the sequencer needs from the game. play() starts a one-shot animation on the performer and returns false
        // when there is no art for it; busy() says whether that animation is still running. The rest is for the full sequence.
        struct Host
        {
            std::function<bool(int animId, int rate)> play;
            std::function<bool()> busy;
            bool full = false;
            std::function<bool(int animId, int rate, bool reverse)> playReverse; // the panda's closing animation
            std::function<void()> cameraPan;                                     // StartScriptedCameraPan(0)
            std::function<void(bool faceRight)> setFacing;                       // before the monkey's turn-around
            std::function<int()> anim;                                           // id of the animation now playing
            std::function<int()> frame;                                          // its current frame
            std::function<void(int id, int volume)> sound;                       // PlaySoundEffect(id, volume, 0)
            std::function<bool(int id)> soundActive;                             // IsSoundIdActive
            std::function<void(int id)> stopSound;                               // StopSoundChannelsByTag
            std::function<int(int lo, int hi)> rand;                             // RandRange: hi exclusive
            std::function<double()> seconds;                                     // a clock, in seconds
        };

        // The trick animation for level 1..3 and the performer's species 0..2.
        static int mainAnim(int level, int species)
        {
            static const int t[3][3] = {{0x1b, 0x81, 0xfc}, {0x24, 0x84, 0xe7}, {0x21, 0x89, 0xf0}};
            return valid(level, species) ? t[level - 1][species] : -1;
        }
        // The intro animation, or -1 for none (the ROM's 0xffff entries).
        static int introAnim(int level, int species)
        {
            static const int t[3][3] = {{-1, -1, -1}, {-1, 0x83, -1}, {0x33, 0x88, 0x102}};
            return valid(level, species) ? t[level - 1][species] : -1;
        }
        // PlayReactionAnim_State2's table: the animation after the repeats, or -1 for none.
        static int closingAnim(int level, int species)
        {
            static const int t[3][3] = {{-1, -1, -1}, {-1, 0x85, -1}, {0x1e, 0x8a, 0x102}};
            return valid(level, species) ? t[level - 1][species] : -1;
        }
        // PlayTurnAroundIdleAnim (0x015144), PlayDirectionalIdleAnim (0x0150f1) and PlayActorReactionAnimation (0x01505b, the
        // overworld table), by species.
        static int turnAroundAnim(int species) { static const int t[3] = {0x34, 0x9b, 0x105}; return species >= 0 && species <= 2 ? t[species] : -1; }
        static int directionalAnim(int species) { static const int t[3] = {0x35, 0x9c, 0x106}; return species >= 0 && species <= 2 ? t[species] : -1; }
        static int actorReactionAnim(int species) { static const int t[3] = {0x31, 0x82, 0xea}; return species >= 0 && species <= 2 ? t[species] : -1; }
        // PlayReactionAnim (0x014e97): 2 -> 0xf1, 3 -> 0xf2, anything else 0xf3.
        static int reactionAnim(int n) { return n == 2 ? 0xf1 : n == 3 ? 0xf2 : 0xf3; }

        // How many times the teacher's trick plays. The ROM keys this on the PLAYER's species (g_playerSpecies).
        static int repeats(int level, int playerSpecies)
        {
            if (level == 2)
                return playerSpecies == 1 ? 5 : 1;
            if (level == 3)
                return playerSpecies == 2 ? 3 : 5;
            return 1;
        }

        // How many times the player's pet repeats the trick (TickActor_NPCEncounterSequence): the dog at level 3 and the
        // monkey at levels 2 and 3 do it more the more Light Bulbs the pet has; the panda at level 3 too; others once.
        // rand(lo, hi) is the game's RandRange: hi exclusive (so 1..2 for (1, 3)).
        static int petRepeats(int level, int species, int lightbulbs, const std::function<int(int, int)> &rand)
        {
            if ((species == 0 && level == 3) || (species == 1 && (level == 2 || level == 3)))
                return lightbulbs <= 99 ? rand(1, 3) : lightbulbs > 999 ? rand(6, 11) : rand(3, 6);
            if (species == 2 && level == 3)
                return lightbulbs <= 99 ? rand(1, 3) : lightbulbs > 999 ? 5 : rand(3, 5);
            return 1;
        }

        void start(int level, int species, int playerSpecies) { begin(level, species, repeats(level, playerSpecies)); }
        void startWith(int level, int species, int count) { begin(level, species, count); }

        bool active() const { return state_ != State::Idle; }

        // One 30 Hz tick. Returns true on the tick the trick finishes.
        bool tick(const Host &h)
        {
            switch (state_)
            {
            case State::Idle:
                return false;
            case State::Intro: // ROM state 1
            {
                const int intro = introAnim(level_, species_);
                state_ = intro >= 0 && h.play(intro, kIntroRate) ? State::WaitIntro : State::Main;
                return false;
            }
            case State::WaitIntro: // ROM state 2
                if (!h.busy())
                    state_ = State::Main;
                return false;
            case State::Main: // ROM state 3
                h.play(mainAnim(level_, species_), kMainRate);
                timerSet_ = false;
                if (h.full && species_ == 1 && level_ == 1 && h.seconds)
                {
                    timerStart_ = h.seconds();
                    timerDur_ = 0.6;
                    timerSet_ = true;
                }
                state_ = State::Repeat;
                return false;
            case State::Repeat: // ROM state 4
                return h.full ? repeatFull(h) : repeatPlain(h);
            case State::Closing: // ROM state 5
                if (species_ == 2 && level_ == 3 && h.stopSound)
                    for (int id : {0x35, 0x36, 0x37, 0x3a})
                        h.stopSound(id);
                if (playClosing(h))
                {
                    state_ = State::WaitClosing;
                    return false;
                }
                return afterClosing(h);
            case State::WaitClosing: // ROM state 6
                if (h.busy())
                    return false;
                return afterClosing(h);
            case State::Turn: // ROM state 7: turned around, now back to the front
                if (h.busy())
                    return false;
                h.play(directionalAnim(species_), kIntroRate);
                state_ = State::TurnBack;
                return false;
            case State::TurnBack: // ROM states 8 and 10
            case State::Exit:
                if (h.busy())
                    return false;
                return finish();
            }
            return false;
        }

    private:
        static bool valid(int level, int species) { return level >= 1 && level <= 3 && species >= 0 && species <= 2; }
        enum class State
        {
            Idle,
            Intro,
            WaitIntro,
            Main,
            Repeat,
            Closing,
            WaitClosing,
            Turn,
            TurnBack,
            Exit
        };

        void begin(int level, int species, int count)
        {
            level_ = level;
            species_ = species;
            remaining_ = count;
            timerSet_ = false;
            state_ = State::Intro;
        }
        bool finish()
        {
            state_ = State::Idle;
            return true;
        }

        // What follows the closing animation (or its absence): ROM states 6 and 9.
        bool afterClosing(const Host &h)
        {
            if (species_ == 1 && level_ == 2)
            {
                const int r = h.rand ? h.rand(1, 3) : 2; // 1 turns left, 2 right
                if (h.setFacing)
                    h.setFacing(r != 1);
                if (h.play(turnAroundAnim(species_), kIntroRate))
                {
                    state_ = State::Turn;
                    return false;
                }
            }
            else if (species_ == 2 && level_ == 1 && h.play(actorReactionAnim(species_), kIntroRate))
            {
                state_ = State::Exit;
                return false;
            }
            return finish();
        }

        bool repeatPlain(const Host &h)
        {
            if (h.busy())
                return false;
            if (--remaining_ < 1)
            {
                state_ = State::Idle;
                return true;
            }
            h.play(mainAnim(level_, species_), kMainRate);
            return false;
        }

        bool playClosing(const Host &h)
        {
            const int id = closingAnim(level_, species_);
            if (id < 0)
                return false;
            if (species_ == 2 && h.playReverse) // the panda's plays last frame to first
                return h.playReverse(id, kIntroRate, true);
            return h.play(id, kIntroRate);
        }

        bool soundingAny(const Host &h, std::initializer_list<int> ids) const
        {
            if (!h.soundActive)
                return false;
            for (int id : ids)
                if (h.soundActive(id))
                    return true;
            return false;
        }

        // ROM state 4 with the sounds.
        bool repeatFull(const Host &h)
        {
            const int cur = h.anim ? h.anim() : -1;
            auto sound = [&](int id, int vol)
            {
                if (h.sound)
                    h.sound(id, vol);
            };
            bool handled = false;
            if (species_ == 1 && level_ == 1 && timerSet_ && h.seconds)
            {
                if (h.seconds() - timerStart_ > timerDur_)
                {
                    sound(0x5b, 0xbe);
                    timerSet_ = false;
                    handled = true;
                }
            }
            if (!handled)
            {
                if (species_ == 2 && level_ == 2 && h.frame && h.frame() == 0xc)
                {
                    if (h.cameraPan)
                        h.cameraPan();
                    if (!(h.soundActive && h.soundActive(0x2e)))
                        sound(0x2e, 0xff);
                }
                else if (cur == 0xf0)
                {
                    if (h.busy() && !soundingAny(h, {0x35, 0x36, 0x37}) && h.rand)
                    {
                        const int r = h.rand(1, 4);
                        sound(r == 1 ? 0x35 : r == 2 ? 0x36 : 0x37, 0xbe);
                    }
                }
                else if (cur == 0x89)
                {
                    if (!(h.soundActive && h.soundActive(0x1a)))
                        sound(0x1a, 0xbe);
                }
            }
            if (h.busy())
                return false;
            if (cur == 0xf0 && h.rand)
            {
                const int r = h.rand(1, 4);
                if (r == 2 || r == 3)
                {
                    h.play(reactionAnim(r), kReactionRate);
                    if (h.stopSound)
                        for (int id : {0x35, 0x36, 0x37})
                            h.stopSound(id);
                    r == 2 ? sound(0x3a, 0xbe) : sound(0x38, 0xff);
                    return false;
                }
            }
            else if (cur == 0xf2 && h.seconds && h.rand)
            {
                if (!timerSet_)
                {
                    timerStart_ = h.seconds();
                    timerDur_ = h.rand(10, 0x1f) / 10.0; // GUESS: the ROM divides the roll by a constant; 10 gives 1.0..3.0 s
                    timerSet_ = true;
                }
                else if (h.seconds() - timerStart_ > timerDur_)
                {
                    h.play(reactionAnim(4), kReactionRate);
                    sound(0x39, 0xff);
                    timerSet_ = false;
                }
                return false;
            }
            if (--remaining_ < 1)
            {
                state_ = State::Closing;
                return false;
            }
            h.play(mainAnim(level_, species_), kMainRate);
            return false;
        }

        int level_ = 1, species_ = 0, remaining_ = 0;
        State state_ = State::Idle;
        bool timerSet_ = false;
        double timerStart_ = 0, timerDur_ = 0;
    };

} // namespace gp
