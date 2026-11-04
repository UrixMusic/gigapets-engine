// species_art_test.cpp - the per-species animation roles (SpeciesArt): the base species' records hold what the old kXxx[3] tables did,
// and code that asks artOf(species) sees a species a mod added.
#include "../src/game/npc_anim_ids.hpp"
#include "../src/game/pet_sit.hpp"
#include "../src/game/pet_voice.hpp"
#include "../src/game/trick_showcase.hpp"
#include <algorithm>
#include <cstdio>
using namespace gp;

static int mainA(int sp) { return TrickShowcase::mainAnim(1, sp); }
static int g_fail = 0;
#define CHECK(c)                                                  \
    do                                                            \
    {                                                             \
        if (!(c))                                                 \
        {                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);      \
            ++g_fail;                                             \
        }                                                         \
    } while (0)

int main()
{
    for (int sp = 0; sp < 3; ++sp)
    {
        const SpeciesArt &a = artOf(sp);
        CHECK(a.walkUp == kWalkUp[sp] && a.walkDown == kWalkDown[sp] && a.walkSide == kWalkSide[sp] && a.walkSideStyle == kWalkSideStyle[sp]);
        CHECK(a.sitDown == kSitDown[sp] && a.sitAlt == kSitAlt[sp] && a.eat == kEat[sp] && a.bowlBite == kBowlBite[sp] && a.eatStyle == kEatStyle[sp]);
        CHECK(a.actionStyle == kActionStyle[sp] && a.boredStyle == kBoredStyle[sp] && a.bedGetIn == kBedGetIn[sp] && a.sleepCycle == kSleepCycle[sp]);
        CHECK(a.bedGetOut == kBedGetOut[sp] && a.positiveStyle == kPositiveStyle[sp] && a.idleBase == kIdleBase[sp] && a.idleSide == kIdleSide[sp]);
        CHECK(a.idleStyle == kIdleStyle[sp] && a.turn180 == kTurn180[sp] && a.turn90 == kTurn90[sp] && a.turn90Style == kTurn90Style[sp]);
        CHECK(a.turn180Style == kTurn180Style[sp] && a.settled == kSettled[sp] && a.settledStyle == kSettledStyle[sp]);
        CHECK(a.idleFlagA == kIdleFlagA[sp] && a.idleFlagAStyle == kIdleFlagAStyle[sp] && a.idleFlagB == kIdleFlagB[sp] && a.idleFlagBStyle == kIdleFlagBStyle[sp]);
        CHECK(a.callFront == kCallFront[sp] && a.callSide == kCallSide[sp] && a.callStyle == kCallStyle[sp]);
        CHECK(a.all().size() >= 31); // every role has a number
    }
    { // the reactions, tricks, dig and voices hold what the ROM's selector tables did (these are the old tables, copied)
        const int posS[3] = {0x1f, 0x86, 0xee}, posD[3] = {0x20, 0x87, 0xed}, emoS[3] = {0x2b, 0x90, 0xf7}, emoD[3] = {0x28, 0x91, 0xf6};
        const int shkS[3] = {0x2c, 0x92, -1}, shkD[3] = {0x27, 0x93, -1}, neg1[3] = {0x19, 0x7d, 0xe4}, neg2[3] = {0x1a, 0x7e, 0xe5}, negIO[3] = {0x32, 0x98, 0xff};
        const int sad[3] = {0x25, 0x8f, 0xf4}, bored[3] = {0x39, 0xa0, 0x107}, idS[3] = {0x30, 0x97, 0xfe}, idD[3] = {0x2f, 0x96, 0xfd};
        const int tMain[3][3] = {{0x1b, 0x81, 0xfc}, {0x24, 0x84, 0xe7}, {0x21, 0x89, 0xf0}};
        const int tIntro[3][3] = {{-1, -1, -1}, {-1, 0x83, -1}, {0x33, 0x88, 0x102}}, tClose[3][3] = {{-1, -1, -1}, {-1, 0x85, -1}, {0x1e, 0x8a, 0x102}};
        const int tTurn[3] = {0x34, 0x9b, 0x105}, tDir[3] = {0x35, 0x9c, 0x106}, tAct[3] = {0x31, 0x82, 0xea}, dig[3] = {0x1c, 0x80, 0xe9};
        const int voice[3][13] = {
            {0x03, 0x04, 0x05, 0x0c, 0x0d, 0x07, 0x08, 0x0a, 0x10, 0x11, 0x0f, 0x0e, 0x0b},
            {0x28, 0x29, 0x2a, 0x25, 0x26, 0x07, 0x08, 0x23, 0x2b, 0x2c, 0x0f, 0x27, 0x24},
            {0x3c, 0x3d, 0x3e, 0x33, 0x34, 0x2f, 0x30, 0x31, 0x40, 0x41, 0x3f, 0x3b, 0x32},
        };
        for (int sp = 0; sp < 3; ++sp)
        {
            CHECK(reactId(ReactAnim::Positive, sp, 1, true) == posS[sp] && reactId(ReactAnim::Positive, sp, 1, false) == posD[sp]);
            CHECK(reactId(ReactAnim::Emote, sp, 1, true) == emoS[sp] && reactId(ReactAnim::EmoteOutro, sp, 1, false) == emoD[sp]);
            CHECK(reactId(ReactAnim::Shocked, sp, 1, true) == shkS[sp] && reactId(ReactAnim::Shocked, sp, 1, false) == shkD[sp]);
            CHECK(reactId(ReactAnim::Negative, sp, 1, false) == neg1[sp] && reactId(ReactAnim::Negative, sp, 2, false) == neg2[sp]);
            CHECK(reactId(ReactAnim::NegIntro, sp, 1, false) == negIO[sp] && reactId(ReactAnim::NegOutro, sp, 1, false) == negIO[sp]);
            CHECK(reactId(ReactAnim::Sad, sp, 1, false) == sad[sp] && reactId(ReactAnim::SadOutro, sp, 1, false) == sad[sp] && reactId(ReactAnim::Bored, sp, 1, false) == bored[sp]);
            CHECK(reactId(ReactAnim::Idle, sp, 1, true) == idS[sp] && reactId(ReactAnim::Idle, sp, 1, false) == idD[sp]);
            CHECK(reactId(ReactAnim::Intro, sp, 1, true) == (sp == 2 ? 0x101 : -1) && reactId(ReactAnim::Outro, sp, 1, false) == (sp == 2 ? 0x100 : -1));
            CHECK(reactMirror(sp, true) == (sp == 2 ? false : true) && reactMirror(sp, false) == (sp == 2 ? true : false));
            for (int l = 1; l <= 3; ++l)
                CHECK(TrickShowcase::mainAnim(l, sp) == tMain[l - 1][sp] && TrickShowcase::introAnim(l, sp) == tIntro[l - 1][sp] && TrickShowcase::closingAnim(l, sp) == tClose[l - 1][sp]);
            CHECK(TrickShowcase::turnAroundAnim(sp) == tTurn[sp] && TrickShowcase::directionalAnim(sp) == tDir[sp] && TrickShowcase::actorReactionAnim(sp) == tAct[sp]);
            CHECK(artOf(sp).dig == dig[sp]);
            for (int c = 0; c < 13; ++c)
                CHECK(petVoiceId(sp, c) == voice[sp][c]);
            // what the pet loads is every role's animation, once each (no -1)
            const std::vector<int> all = artOf(sp).all();
            for (int id : all)
                CHECK(id >= 0);
            for (int id : {mainA(sp), dig[sp], tTurn[sp], posS[sp]})
                CHECK(std::find(all.begin(), all.end(), id) != all.end());
        }
        CHECK(TrickShowcase::mainAnim(0, 0) == -1 && TrickShowcase::mainAnim(4, 0) == -1 && TrickShowcase::mainAnim(1, 7) == -1 && petVoiceId(0, 13) == -1 && petVoiceId(0, -1) == -1);
        CHECK(petVoiceId(99, 3) == voice[2][3] && petVoiceId(-4, 3) == voice[0][3]); // out of range: as before, clamped
    }
    { // the small differences and the trick rules, per base species (the values the code used to hard-code)
        const SpeciesArt &d = artOf(0), &m = artOf(1), &p = artOf(2);
        CHECK(d.bowlX == 0x208 && d.bowlY == 0xad && d.bowlSlope == 2 && p.bowlX == 0x1c3 && p.bowlY == 0xa1 && p.bowlSlope == -2 && m.bowlSlope == 2);
        CHECK(p.hopIntro == kPandaFrameStyle && d.hopIntro == -1 && m.hopIntro == -1 && p.bowlStartsWithAction && !d.bowlStartsWithAction);
        CHECK(p.wideShadow && !d.wideShadow && !m.wideShadow && m.digDirt == 2 && d.digDirt == 1 && p.digDirt == 1);
        CHECK(d.idleFlagBRate == 14 && m.idleFlagBRate == 7 && p.idleFlagBRate == 7 && d.settledRate == 7 && m.settledRate == 5 && d.sleepFps == 5 && p.sleepFps == 7);
        CHECK(d.tiredDecay == 2 && m.tiredDecay == 1 && p.hungerDecay == 2 && d.hungerDecay == 1);
        CHECK(d.bubbleLower == 15 && m.bubbleLower == 20 && p.bubbleLower == 21 && p.bubbleRaise == 0x14 && d.raisedAnims.empty() && p.raisedAnims.size() == 8);
        // the idle NPCs' trick repeats
        CHECK(m.trickRepeatLo[1] == 2 && m.trickRepeatHi[1] == 6 && d.trickRepeatHi[1] <= d.trickRepeatLo[1] && p.trickRepeatLo[2] == 1 && p.trickRepeatHi[2] == 4 && d.trickRepeatLo[2] == 2 && d.trickRepeatHi[2] == 6);
        // the trick rules
        CHECK(TrickShowcase::repeats(1, 0) == 1 && TrickShowcase::repeats(2, 1) == 5 && TrickShowcase::repeats(2, 0) == 1 && TrickShowcase::repeats(3, 2) == 3 && TrickShowcase::repeats(3, 0) == 5 && TrickShowcase::repeats(3, 1) == 5);
        CHECK(m.trickRules[0].startSound == 0x5b && m.trickRules[1].turnAroundAfter && m.trickRules[2].loopSoundAnim == 0x89 && m.trickRules[2].loopSound == 0x1a);
        CHECK(p.trickRules[0].actorReactionExit && p.trickRules[1].soundFrame == 0xc && p.trickRules[1].cameraPan && p.trickRules[1].frameSound == 0x2e);
        const TrickRules &c = p.trickRules[2];
        CHECK(c.closingReversed && c.chainAnim == 0xf0 && c.chainSounds.size() == 3 && c.chainReactions.size() == 2 && c.chainReactions[0].anim == 0xf1 && c.chainReactions[1].anim == 0xf2);
        CHECK(c.chainWaitAnim == 0xf2 && c.chainFinalAnim == 0xf3 && c.chainFinalSound == 0x39 && c.stopOnClose.size() == 4);
        const std::vector<int> all = p.all();
        for (int id : {0xf1, 0xf2, 0xf3, 0x118}) // what the panda's pet preloads for the chain and the hop
            CHECK(std::find(all.begin(), all.end(), id) != all.end());
        CHECK(d.trickRules[2].bulbScale == 1 && m.trickRules[1].bulbScale == 1 && m.trickRules[2].bulbScale == 1 && p.trickRules[2].bulbScale == 2 && d.trickRules[1].bulbScale == 0);
        auto rnd = [](int lo, int hi) { return hi - 1; };
        CHECK(TrickShowcase::petRepeats(3, 0, 50, rnd) == 2 && TrickShowcase::petRepeats(3, 0, 500, rnd) == 5 && TrickShowcase::petRepeats(3, 0, 5000, rnd) == 10);
        CHECK(TrickShowcase::petRepeats(3, 2, 5000, rnd) == 5 && TrickShowcase::petRepeats(3, 2, 500, rnd) == 4 && TrickShowcase::petRepeats(1, 1, 500, rnd) == 1 && TrickShowcase::petRepeats(2, 0, 500, rnd) == 1);
    }
    { // the interface numbers (the ROM's tables)
        const SpeciesArt &d = artOf(0), &m = artOf(1), &p = artOf(2);
        CHECK(d.trickIcons[0] == 0x58 && m.trickIcons[1] == 0x5a && p.trickIcons[2] == 0x57);
        CHECK(d.colorBase == 0x1df && m.colorBase == 0x29e && p.colorBase == 0x31e);
        CHECK(m.potions[2] == 0x2ab && p.potions[4] == 0x33d && d.potions[0] == 0x1df);
        CHECK(d.loadTile == 0x9f0 && m.loadTile == 0x9f2 && p.loadTile == 0x9f3 && m.loadX == 0xcf && m.loadY == 0x45 && p.loadX == 0x1b && p.loadY == 0x29 && d.loadPicture == -1);
        CHECK(d.menuPortrait == 0x982 && p.menuPortrait == 0x984 && m.namePortrait[0] == 0x65c && p.namePortrait[2] == 0x05);
        CHECK(m.npcPortraitTile == 0x8ba && p.npcPortraitPal[2] == 0x33d && m.npcPortraitPal[0] == 0x2a2);
        CHECK(d.trickNames[0] == "base:str/trick_1_0" && p.trickNames[2] == "base:str/trick_3_2");
    }
    CHECK(&artOf(-1) == &artOf(0) && &artOf(99) == &artOf(0)); // out of range: the first species

    // a species added at run time is what the code asks for (a mod's, later): the sit logic and the walk choice follow it
    SpeciesArt mine = artOf(0);
    mine.settled = 70001;
    mine.walkUp = 70002;
    speciesArts().push_back(mine);
    CHECK(artOf(3).settled == 70001 && artOf(3).walkUp == 70002 && artOf(0).settled == kSettled[0]);
    CHECK(walkAnim(3, 2, false).id == 70002); // facing up
    speciesArts().pop_back();

    std::printf(g_fail ? "%d check(s) failed\n" : "species_art_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
