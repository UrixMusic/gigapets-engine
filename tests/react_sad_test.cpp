// tests/react_sad_test.cpp - the sad reaction (TickNPCReactionAnimationSequence, 0x01e915):
// sad, hold 0.5 s, then the same animation again in reverse.
#include <cstdio>
#include <vector>
#include "../src/game/npc_anim_ids.hpp"
#include "../src/game/reaction_seq.hpp"
using namespace gp;

struct Play
{
    ReactAnim a;
    int tick;
};

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    { // drive the sequence with an animation that takes 6 ticks
        std::vector<Play> plays;
        int tick = 0, left = 0;
        ActorIO io;
        io.play = [&](ReactAnim a, int)
        { plays.push_back({a, tick}); left = 6; return true; };
        io.busy = [&]
        { return left > 0; };
        io.sound = [](int, int) {};
        io.bubble = [](int) {};
        io.settle = [] { return true; };
        io.rand = [](int lo, int) { return lo; };

        ReactionSeq seq(ReactionSeq::Kind::Sad);
        bool done = false;
        for (tick = 0; tick < 200 && !done; ++tick)
        {
            if (left > 0)
                --left;
            done = seq.tick(io);
        }
        check("the sequence finishes", done);
        check("it plays exactly two animations", plays.size() == 2);
        check("first: the sad animation, forward", plays.size() > 0 && plays[0].a == ReactAnim::Sad && !playsReversed(plays[0].a));
        check("second: the sad animation in reverse", plays.size() > 1 && plays[1].a == ReactAnim::SadOutro && playsReversed(plays[1].a));
        std::printf("      second play started %d ticks after the first\n", plays.size() > 1 ? plays[1].tick - plays[0].tick : -1);
        // 6 ticks of animation, then the 0.5 s (15 tick) hold, then the second play
        check("0.5 s hold between them", plays.size() > 1 && plays[1].tick - plays[0].tick >= 6 + ReactionSeq::kHalfSecond);
    }

    { // the reverse rule is one shared function, and covers every outro
        check("outro reverses", playsReversed(ReactAnim::Outro));
        check("negative outro reverses", playsReversed(ReactAnim::NegOutro));
        check("sad outro reverses", playsReversed(ReactAnim::SadOutro));
        check("intros and ordinary reactions do not",
              !playsReversed(ReactAnim::Intro) && !playsReversed(ReactAnim::NegIntro) && !playsReversed(ReactAnim::Sad) &&
                  !playsReversed(ReactAnim::Positive) && !playsReversed(ReactAnim::Negative) && !playsReversed(ReactAnim::Emote));
    }

    { // the sad outro uses the same art as sad, for every species
        bool same = true;
        for (int t = 0; t < 3; ++t)
            same = same && reactId(ReactAnim::SadOutro, t, 0, false) == reactId(ReactAnim::Sad, t, 0, false) &&
                   reactId(ReactAnim::Sad, t, 0, false) >= 0;
        check("SadOutro is the Sad art (dog 0x25, monkey 0x8f, panda 0xf4)",
              same && reactId(ReactAnim::SadOutro, 0, 0, false) == 0x25 && reactId(ReactAnim::SadOutro, 1, 0, false) == 0x8f &&
                  reactId(ReactAnim::SadOutro, 2, 0, false) == 0xf4);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
