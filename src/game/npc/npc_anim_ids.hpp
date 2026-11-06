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
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace gp
{

    constexpr int kPetVoiceCategories = 13;

    // What a species' trick does besides playing its animations, per level (trick_showcase.hpp). The base values are what the ROM did
    // for each species / level pair; everything is optional.
    struct TrickReaction
    {
        int roll = 0, anim = -1, sound = -1, volume = 0xbe; // when the roll comes up, play `anim` with `sound`
    };
    struct TrickRules
    {
        int teacherRepeats = 1;  // how many times the teacher's showcase repeats the trick (the ROM keys it on the player's species)
        int bulbScale = 0;       // how many times the player's pet repeats it: 0 once; 1 = 1..2 / 3..5 / 6..10 by Light Bulbs; 2 = 1..2 / 3..4 / 5
        // sounds (only when the pet performs the full sequence)
        int startSound = -1, startSoundVolume = 0xbe;
        double startSoundDelay = 0.6;                         // seconds after the trick starts
        int soundFrame = -1, frameSound = -1, frameSoundVolume = 0xff; // while the animation is on this frame (the camera pan too, if cameraPan)
        bool cameraPan = false;
        int loopSoundAnim = -1, loopSound = -1, loopSoundVolume = 0xbe; // while this animation plays and the sound is not sounding
        // what follows
        bool closingReversed = false;    // the closing animation plays last frame to first
        bool turnAroundAfter = false;    // then it turns to a random side (trickTurnAround) and back to the front (trickDirectional)
        bool actorReactionExit = false;  // or plays trickActorReaction as its exit
        std::vector<int> stopOnClose;    // sounds to stop when the closing animation starts
        // a chain of reactions after one animation (the panda's level 3): while chainAnim plays, one of chainSounds at random; when it
        // ends a roll of 1..3 may play a reaction; after chainWaitAnim it waits 1..3 s and plays chainFinalAnim
        int chainAnim = -1;
        std::vector<int> chainSounds;
        std::vector<TrickReaction> chainReactions;
        int chainWaitAnim = -1, chainFinalAnim = -1, chainFinalSound = -1, chainFinalVolume = 0xff;
    };

    // The animation numbers one pet species plays for each role (what the kXxx[3] tables below hold, one species per column).
    // The base game's three are built from those tables; artOf(species) is what the code asks, so a mod's species can have its own.
    struct SpeciesArt
    {
        std::string id; // the species' id ("base:species/0", a mod's own): what a save keeps, as the numbers of mod species change with the mods
        int walkUp = -1;
        int walkDown = -1;
        int walkSide = -1;
        int walkSideStyle = -1;
        int sitDown = -1;
        int sitAlt = -1;
        int eat = -1;
        int bowlBite = -1;
        int eatStyle = -1;
        int actionStyle = -1;
        int boredStyle = -1;
        int bedGetIn = -1;
        int sleepCycle = -1;
        int bedGetOut = -1;
        int positiveStyle = -1;
        int idleBase = -1;
        int idleSide = -1;
        int idleStyle = -1;
        int turn180 = -1;
        int turn90 = -1;
        int turn90Style = -1;
        int turn180Style = -1;
        int settled = -1;
        int settledStyle = -1;
        int idleFlagA = -1;
        int idleFlagAStyle = -1;
        int idleFlagB = -1;
        int idleFlagBStyle = -1;
        int callFront = -1;
        int callSide = -1;
        int callStyle = -1;
        // the reactions (positive, emote, shocked, negative, sad, bored, the reaction idle, the panda's intro / outro): side = facing left or right
        int posSide = -1;
        int posDown = -1;
        int emoSide = -1;
        int emoDown = -1;
        int shkSide = -1;
        int shkDown = -1;
        int neg1 = -1;
        int neg2 = -1;
        int negIO = -1;
        int sad = -1;
        int bored = -1;
        int reactIdleSide = -1;
        int reactIdleDown = -1;
        int introSide = -1;
        int introDown = -1;
        bool facesRight = false; // the side art faces right (the panda's), not left: which way it is mirrored
        // the tricks, by level 1..3 (the main animation, the intro before it, the closing one after the repeats), and the three single ones
        int trickMain[3] = {-1, -1, -1}, trickIntro[3] = {-1, -1, -1}, trickClosing[3] = {-1, -1, -1};
        int trickTurnAround = -1, trickDirectional = -1, trickActorReaction = -1;
        TrickRules trickRules[3]; // by level 1..3
        int dig = -1; // the dig animation (Z on bare ground)
        int voice[kPetVoiceCategories] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // the sound for each reaction category (pet_voice.hpp)
        // small differences in how this species behaves (the base values are what the ROM did for the dog, monkey and panda)
        int bowlX = 0x208, bowlY = 0xad; // where the food bowl is approached to; the pet walks up a diagonal that ends there
        int bowlSlope = 2;               // x pixels per y pixel on that diagonal; its sign is the side it comes from (negative: from the left, facing right)
        int hopIntro = -1;               // art played before and, reversed, after the happy hop at the bowl (the panda's); -1 = none
        bool bowlStartsWithAction = false; // starts eating with its action animation (actionStyle) first
        bool wideShadow = false;         // the wider shadow blob
        int digDirt = 1;                 // Cleanliness a dig costs
        int idleFlagBRate = 7, settledRate = 5, sleepFps = 7; // frames a second of the sick idle, the sitting pose and the sleep cycle
        int tiredDecay = 1, hungerDecay = 1; // what a wearing-down tick takes off Sleepiness / Hunger
        int thoughtDX[3] = {0xf, 0x16, 0x14}; // a thought bubble's x from the feet when facing left / right / up or down
        int thoughtDY = -0x46, overheadDY = -0x4b; // ... and its y, for a thought bubble and an overhead one
        int bubbleLower = 20;            // pixels every overhead bubble is moved down (tuned by eye)
        int bubbleRaise = 0;             // extra lift of a bubble while one of raisedAnims plays
        std::vector<int> raisedAnims;
        // the user interface
        int trickIcons[3] = {-1, -1, -1}; // the tricks panel's sprite for each known trick (left, top, right)
        int colorBase = 0x1df;            // first palette of this species' colour range: the colour machine and the potions add to it
        int potions[5] = {-1, -1, -1, -1, -1}; // the colour each potion (item w9 0..4) gives: palette numbers
        int namePortrait[3] = {0x65d, 0x22, 0x16}; // the name screen's portrait: tile, x, y
        int npcPortraitTile = 0x8ae;      // the portrait of the NPCs that stand in for the pet's own species (dialogue)
        int npcPortraitPal[3] = {0x1e3, 0x1e4, 0x1fe}; // ... and its palettes for the three of them
        std::string trickNames[3];        // text keys of the three tricks' names (the teacher says them)
        int kit = 0;                      // whose home furnishings it starts with (frontend.hpp starterKit): a base species, 0..2
        int menuPortrait = 0x982;         // the menu's portrait tile
        // The big picture of the pet on the load screen. The screen's background already has the three base pets drawn on it (grey); the picture is
        // drawn over one of them. loadPicture: an anim (its first frame is the picture, any size). loadTile: one of the ROM's own pictures (a clone of a
        // base species keeps it). With neither, the idle art's first frame. loadSpot: which pet's place it covers (0 the dog's, bottom left, 96 x 120;
        // 1 the monkey's, right, 113 x 171; 2 the panda's, top left, 72 x 88): the picture is scaled to fit that place (whole-number enlarging, or
        // shrinking) and stands in its bottom middle. loadX / loadY (-1 = not set): put its top-left exactly there on the 320 x 240 screen instead,
        // at its own size (shrunk to 120 x 176 if larger).
        int loadPicture = -1, loadTile = -1, loadSpot = 0, loadX = -1, loadY = -1;
        int trickRepeatLo[3] = {1, 1, 1}, trickRepeatHi[3] = {1, 1, 1}; // how many times an NPC of this species repeats a trick, by level: lo..hi-1 (hi <= lo: always lo)

        // The animations its tricks use: per level the main, intro and closing ones and the chain's reactions, and the three single ones.
        std::vector<int> trickAnims() const
        {
            std::vector<int> v;
            for (int id : {trickTurnAround, trickDirectional, trickActorReaction})
                if (id >= 0)
                    v.push_back(id);
            for (int l = 0; l < 3; ++l)
            {
                for (int id : {trickMain[l], trickIntro[l], trickClosing[l], trickRules[l].chainFinalAnim})
                    if (id >= 0)
                        v.push_back(id);
                for (const TrickReaction &r : trickRules[l].chainReactions)
                    if (r.anim >= 0)
                        v.push_back(r.anim);
            }
            return v;
        }

        // Every animation this species has a number for (the ones to load for the pet).
        std::vector<int> all() const
        {
            std::vector<int> v;
            for (int id : {walkUp, walkDown, walkSide, walkSideStyle, sitDown, sitAlt, eat, bowlBite, eatStyle, actionStyle, boredStyle, bedGetIn, sleepCycle, bedGetOut, positiveStyle, idleBase, idleSide, idleStyle, turn180, turn90, turn90Style, turn180Style, settled, settledStyle, idleFlagA, idleFlagAStyle, idleFlagB, idleFlagBStyle, callFront, callSide, callStyle})
                if (id >= 0)
                    v.push_back(id);
            for (int id : {posSide, posDown, emoSide, emoDown, shkSide, shkDown, neg1, neg2, negIO, sad, bored, reactIdleSide, reactIdleDown, introSide, introDown, dig, hopIntro})
                if (id >= 0)
                    v.push_back(id);
            for (int id : trickAnims())
                v.push_back(id);
            return v;
        }
    };
    const SpeciesArt &artOf(int species); // out of range = the first species
    std::vector<SpeciesArt> &speciesArts(); // index = species number; filled with the base game's three, defined at the end
    constexpr int kBaseSpecies = 3; // the base game's species; a mod's come after them
    inline void forgetModSpecies() { speciesArts().resize(kBaseSpecies); } // before the mods are loaded (again): each load adds them anew
    inline bool knownSpecies(int species) { return species >= 0 && size_t(species) < speciesArts().size(); }

    // A mod's animations (PNG frames, already coloured) get a number from here up so that a species' art, which holds numbers,
    // can name them like the ROM's own. The numbers are made while the mods load and are not saved.
    // The number of the species with this id, or -1.
    inline int speciesNumber(const std::string &id)
    {
        const auto &v = speciesArts();
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].id == id)
                return int(i);
        return -1;
    }

    constexpr int kModAnimBase = 0x30000;
    // The roles of a pet species by the word scripts use ("sit", "eat", "dig"...): which animation field of its SpeciesArt each is. (player.play and --export-pet-frames.)
    inline const std::vector<std::pair<const char *, int SpeciesArt::*>> &speciesRoles()
    {
        static const std::vector<std::pair<const char *, int SpeciesArt::*>> roles = {
            {"walk_up", &SpeciesArt::walkUp}, {"walk_down", &SpeciesArt::walkDown}, {"walk_side", &SpeciesArt::walkSide}, {"sit", &SpeciesArt::sitDown},
            {"sit_idle", &SpeciesArt::sitAlt}, {"eat", &SpeciesArt::eat}, {"bowl_bite", &SpeciesArt::bowlBite}, {"bed_in", &SpeciesArt::bedGetIn},
            {"sleep", &SpeciesArt::sleepCycle}, {"bed_out", &SpeciesArt::bedGetOut}, {"idle", &SpeciesArt::idleBase}, {"idle_side", &SpeciesArt::idleSide},
            {"turn_180", &SpeciesArt::turn180}, {"turn_90", &SpeciesArt::turn90}, {"settled", &SpeciesArt::settled}, {"call_front", &SpeciesArt::callFront},
            {"call_side", &SpeciesArt::callSide}, {"happy_side", &SpeciesArt::posSide}, {"happy_down", &SpeciesArt::posDown}, {"excited_side", &SpeciesArt::emoSide},
            {"excited_down", &SpeciesArt::emoDown}, {"shocked_side", &SpeciesArt::shkSide}, {"shocked_down", &SpeciesArt::shkDown}, {"no1", &SpeciesArt::neg1},
            {"no2", &SpeciesArt::neg2}, {"sad", &SpeciesArt::sad}, {"bored", &SpeciesArt::bored}, {"dig", &SpeciesArt::dig}};
        return roles;
    }

    inline std::vector<std::string> &modAnimNames()
    {
        static std::vector<std::string> v;
        return v;
    }
    inline bool isModAnim(int n) { return n >= kModAnimBase && size_t(n - kModAnimBase) < modAnimNames().size(); }
    inline int modAnimNumber(const std::string &id) // the number of a mod's animation id (made on the first ask)
    {
        auto &v = modAnimNames();
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == id)
                return kModAnimBase + int(i);
        v.push_back(id);
        return kModAnimBase + int(v.size()) - 1;
    }
    inline const std::string &modAnimName(int n) { return modAnimNames()[size_t(n - kModAnimBase)]; }
    // The db id of animation number n under palette pal: the ROM's (recoloured by pal when pal >= 0), or the mod's own frames.
    inline std::string animKey(int n, int pal)
    {
        if (isModAnim(n))
            return modAnimName(n);
        char b[48];
        std::snprintf(b, sizeof b, pal >= 0 ? "base:anim/%d@%x" : "base:anim/%d", n, pal);
        return b;
    }


    struct AnimChoice
    {
        int id = -1;
        bool mirror = false;
        int rate = 14;
        bool loop = false;
    };

    // Which animation a reaction plays: from the species' record (the ROM's selector tables are its base values).
    // tier = actor[4] (0 for the pet, the NPC's species for an NPC); side = facing left/right, else facing down.
    static int reactId(gp::ReactAnim a, int tier, int variant, bool side)
    {
        const SpeciesArt &art = artOf(tier);
        switch (a)
        {
        case gp::ReactAnim::Positive:
            return side ? art.posSide : art.posDown;
        case gp::ReactAnim::Emote:
        case gp::ReactAnim::EmoteOutro: // same art, played in reverse
            return side ? art.emoSide : art.emoDown;
        case gp::ReactAnim::Shocked:
            return side ? art.shkSide : art.shkDown;
        case gp::ReactAnim::Negative:
            return variant == 1 ? art.neg1 : art.neg2;
        case gp::ReactAnim::NegIntro:
        case gp::ReactAnim::NegOutro:
            return art.negIO;
        case gp::ReactAnim::Intro:
        case gp::ReactAnim::Outro:
            return side ? art.introSide : art.introDown;
        case gp::ReactAnim::Sad:
        case gp::ReactAnim::SadOutro: // same art, played in reverse
            return art.sad;
        case gp::ReactAnim::Bored:
            return art.bored;
        case gp::ReactAnim::Idle:
            return side ? art.reactIdleSide : art.reactIdleDown;
        default:
            return -1;
        }
    }

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
    inline bool reactMirror(int tier, bool faceRight) { return artOf(tier).facesRight ? !faceRight : faceRight; }

    // Is `animId` side-facing reaction art, i.e. art that reactMirror applies to when the actor faces sideways?
    // That is every side variant the ROM mirrors with the 0x1004 sprite bank: PlayPositiveReactionAnimVariant
    // (0x0153d6), the emote and shocked tables, and SelectAndPlayReactionAnim (0x015462) for the intro and outro.
    // The panda's intro/outro side art (0x101) is the only intro/outro art there is; it uses the same bank rule as
    // the happy hop it frames, so leaving it out makes the pet flip in and out around the hop.
    inline bool isSideReactArt(int tier, int animId)
    {
        if (animId < 0)
            return false;
        for (ReactAnim a : {ReactAnim::Positive, ReactAnim::Emote, ReactAnim::Shocked, ReactAnim::Intro, ReactAnim::Outro})
            if (reactId(a, tier, 0, true) == animId)
                return true;
        return false;
    }

    // ---- UpdateActorSpriteFrame (0x014bde): walking, loops at 14 ----
    constexpr int kWalkUp[3] = {54, 157, 230};
    constexpr int kWalkDown[3] = {55, 158, 236};
    constexpr int kWalkSide[3] = {56, 159, 250};      // art faces right; left = mirrored
    constexpr int kWalkSideStyle[3] = {78, 181, 276}; // style != 0: sideways only, no up/down art
    constexpr int kSitDown[3] = {46, 154, 260};
    constexpr int kSitAlt[3] = {49, 130, 234};
    constexpr int kEat[3] = {29, 127, 232};
    // The food bowl in a room (Pet_RepeatedTrickWithDialogue 0x015ab1): the style (indoor) versions of the eating art.
    constexpr int kBowlBite[3] = {0x3a, 0xa1, 0x108};   // PlayReactionAnimForActorKind: lean in to the bowl
    constexpr int kEatStyle[3] = {0x3b, 0xa2, 0x109};   // PlayEatingAnim with style != 0: the chewing, repeated
    constexpr int kActionStyle[3] = {0x46, 0xb1, 0x11a}; // PlayActorActionAnimation with style != 0 (the panda's intro)
    // The bed in a room: the yawn (PlayBoredReactionAnim, style), getting in (UpdateActorSpriteFrame2), one sleep cycle
    // (TriggerActorSpawnEffect) and getting out (PlayEncounterExitAnim). None of these is mirrored.
    constexpr int kBoredStyle[3] = {0x4f, 0xb6, 0x11d};
    constexpr int kBedGetIn[3] = {0x48, 0xb2, 0x11b};
    constexpr int kSleepCycle[3] = {0x47, 0xae, 0x116};
    constexpr int kBedGetOut[3] = {0x4d, 0xb4, 0x10b};
    // SelectAndPlayReactionAnim (0x015462) with style != 0: the panda's intro before the happy hop and its outro after it
    // (the same art, last frame to first). The dog and monkey have none. Unmirrored.
    constexpr int kPandaFrameStyle = 0x118;
    // PlayPositiveReactionAnimVariant with style != 0 ignores the facing and is not mirrored: the happy hop, indoor size.
    constexpr int kPositiveStyle[3] = {0x3c, 0xa4, 0x10d};

    inline AnimChoice walkAnim(int sp, int facing, bool style)
    {
        AnimChoice c;
        c.loop = true;
        if (!knownSpecies(sp))
            return c;
        if (style)
        {
            if (facing == FaceLeft || facing == FaceRight)
            {
                c.id = gp::artOf(sp).walkSideStyle;
                c.mirror = facing == FaceLeft;
            }
            return c;
        }
        switch (facing)
        {
        case FaceLeft:
            c.id = gp::artOf(sp).walkSide;
            c.mirror = true;
            break;
        case FaceRight:
            c.id = gp::artOf(sp).walkSide;
            break;
        case FaceUp:
            c.id = gp::artOf(sp).walkUp;
            break;
        case FaceDown:
            c.id = gp::artOf(sp).walkDown;
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
        if (!knownSpecies(sp))
            return c;
        if (style)
        {
            c.id = gp::artOf(sp).idleStyle;
            return c;
        }
        if (facing == FaceLeft || facing == FaceRight)
        {
            c.id = gp::artOf(sp).idleSide;
            const bool artFacesLeft = !gp::artOf(sp).facesRight;
            c.mirror = artFacesLeft ? facing == FaceRight : facing == FaceLeft;
        }
        else
            c.id = gp::artOf(sp).idleBase;
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
        if (!knownSpecies(sp))
            return c;
        c.id = style ? gp::artOf(sp).turn180Style : gp::artOf(sp).turn180;
        c.mirror = !style && facing == FaceRight;
        return c;
    }
    inline AnimChoice turn90(int sp, int facing, bool style = false)
    {
        AnimChoice c;
        if (!knownSpecies(sp))
            return c;
        c.id = style ? gp::artOf(sp).turn90Style : gp::artOf(sp).turn90;
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
        if (!knownSpecies(sp))
            return c;
        c.id = style ? gp::artOf(sp).settledStyle : gp::artOf(sp).settled;
        c.rate = gp::artOf(sp).settledRate;
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


    // ---- the species' art, one record each ----
    inline SpeciesArt baseSpeciesArt(int i)
    {
        SpeciesArt a;
        a.id = "base:species/" + std::to_string(i);
        a.walkUp = kWalkUp[i];
        a.walkDown = kWalkDown[i];
        a.walkSide = kWalkSide[i];
        a.walkSideStyle = kWalkSideStyle[i];
        a.sitDown = kSitDown[i];
        a.sitAlt = kSitAlt[i];
        a.eat = kEat[i];
        a.bowlBite = kBowlBite[i];
        a.eatStyle = kEatStyle[i];
        a.actionStyle = kActionStyle[i];
        a.boredStyle = kBoredStyle[i];
        a.bedGetIn = kBedGetIn[i];
        a.sleepCycle = kSleepCycle[i];
        a.bedGetOut = kBedGetOut[i];
        a.positiveStyle = kPositiveStyle[i];
        a.idleBase = kIdleBase[i];
        a.idleSide = kIdleSide[i];
        a.idleStyle = kIdleStyle[i];
        a.turn180 = kTurn180[i];
        a.turn90 = kTurn90[i];
        a.turn90Style = kTurn90Style[i];
        a.turn180Style = kTurn180Style[i];
        a.settled = kSettled[i];
        a.settledStyle = kSettledStyle[i];
        a.idleFlagA = kIdleFlagA[i];
        a.idleFlagAStyle = kIdleFlagAStyle[i];
        a.idleFlagB = kIdleFlagB[i];
        a.idleFlagBStyle = kIdleFlagBStyle[i];
        a.callFront = kCallFront[i];
        a.callSide = kCallSide[i];
        a.callStyle = kCallStyle[i];
        static const int posS[3] = {0x1f, 0x86, 0xee}, posD[3] = {0x20, 0x87, 0xed};
        static const int emoS[3] = {0x2b, 0x90, 0xf7}, emoD[3] = {0x28, 0x91, 0xf6};
        static const int shkS[3] = {0x2c, 0x92, -1}, shkD[3] = {0x27, 0x93, -1};
        static const int neg1[3] = {0x19, 0x7d, 0xe4}, neg2[3] = {0x1a, 0x7e, 0xe5}, negIO[3] = {0x32, 0x98, 0xff};
        static const int sad[3] = {0x25, 0x8f, 0xf4}, bored[3] = {0x39, 0xa0, 0x107};
        static const int idS[3] = {0x30, 0x97, 0xfe}, idD[3] = {0x2f, 0x96, 0xfd};
        a.posSide = posS[i]; a.posDown = posD[i]; a.emoSide = emoS[i]; a.emoDown = emoD[i]; a.shkSide = shkS[i]; a.shkDown = shkD[i];
        a.neg1 = neg1[i]; a.neg2 = neg2[i]; a.negIO = negIO[i]; a.sad = sad[i]; a.bored = bored[i]; a.reactIdleSide = idS[i]; a.reactIdleDown = idD[i];
        a.introSide = i == 2 ? 0x101 : -1; a.introDown = i == 2 ? 0x100 : -1; // the panda's intro / outro art; the others have none
        a.facesRight = i == 2;
        static const int tMain[3][3] = {{0x1b, 0x81, 0xfc}, {0x24, 0x84, 0xe7}, {0x21, 0x89, 0xf0}};
        static const int tIntro[3][3] = {{-1, -1, -1}, {-1, 0x83, -1}, {0x33, 0x88, 0x102}};
        static const int tClose[3][3] = {{-1, -1, -1}, {-1, 0x85, -1}, {0x1e, 0x8a, 0x102}};
        for (int l = 0; l < 3; ++l)
        {
            a.trickMain[l] = tMain[l][i];
            a.trickIntro[l] = tIntro[l][i];
            a.trickClosing[l] = tClose[l][i];
        }
        static const int tTurn[3] = {0x34, 0x9b, 0x105}, tDir[3] = {0x35, 0x9c, 0x106}, tAct[3] = {0x31, 0x82, 0xea}, dig[3] = {0x1c, 0x80, 0xe9};
        a.trickTurnAround = tTurn[i]; a.trickDirectional = tDir[i]; a.trickActorReaction = tAct[i]; a.dig = dig[i];
        static const int kVoice[3][kPetVoiceCategories] = {
            {0x03, 0x04, 0x05, 0x0c, 0x0d, 0x07, 0x08, 0x0a, 0x10, 0x11, 0x0f, 0x0e, 0x0b},
            {0x28, 0x29, 0x2a, 0x25, 0x26, 0x07, 0x08, 0x23, 0x2b, 0x2c, 0x0f, 0x27, 0x24},
            {0x3c, 0x3d, 0x3e, 0x33, 0x34, 0x2f, 0x30, 0x31, 0x40, 0x41, 0x3f, 0x3b, 0x32},
        };
        for (int c = 0; c < kPetVoiceCategories; ++c)
            a.voice[c] = kVoice[i][c];
        a.bowlX = i == 2 ? 0x1c3 : 0x208; a.bowlY = i == 2 ? 0xa1 : 0xad; a.bowlSlope = i == 2 ? -2 : 2;
        a.hopIntro = i == 2 ? kPandaFrameStyle : -1;
        a.bowlStartsWithAction = i == 2;
        a.wideShadow = i == 2;
        a.digDirt = i == 1 ? 2 : 1;
        a.idleFlagBRate = i == 0 ? 14 : 7; a.settledRate = i == 0 ? 7 : 5; a.sleepFps = i == 0 ? 5 : 7;
        a.tiredDecay = i == 0 ? 2 : 1; a.hungerDecay = i == 2 ? 2 : 1;
        a.thoughtDX[0] = i == 0 ? 0 : 0xf; a.thoughtDX[1] = i == 0 ? 0x25 : 0x16; a.thoughtDX[2] = 0x14;
        a.thoughtDY = i == 0 ? -0x32 : -0x46; a.overheadDY = i == 0 ? -0x32 : -0x4b;
        a.bubbleLower = i == 0 ? 15 : i == 2 ? 21 : 20;
        if (i == 2)
        {
            a.bubbleRaise = 0x14; // the panda's raised art lifts a bubble further
            a.raisedAnims = {0xed, 0xee, 0xf0, 0xf1, 0xf2, 0xf3, 0xe4, 0xe5};
        }
        // an idle NPC's trick repeats: level 2 the monkey 2..5 times, the others once; level 3 the panda 1..3, the others 2..5
        a.trickRepeatLo[1] = i == 1 ? 2 : 1; a.trickRepeatHi[1] = i == 1 ? 6 : 1;
        {
            static const int icons[3][3] = {{0x58, 0x5b, 0x5c}, {0x5d, 0x5a, 0x59}, {0x60, 0x56, 0x57}};
            static const int bases[3] = {0x1df, 0x29e, 0x31e};
            for (int k = 0; k < 3; ++k)
                a.trickIcons[k] = icons[i][k];
            a.colorBase = bases[i];
            a.kit = i;
            for (int k = 0; k < 3; ++k)
                a.trickNames[k] = "base:str/trick_" + std::to_string(k + 1) + "_" + std::to_string(i);
            static const int pot[3][5] = {{0x1df, 0x1e4, 0x1ec, 0x1f9, 0x1fe}, {0x29e, 0x2a3, 0x2ab, 0x2b8, 0x2bd}, {0x31e, 0x323, 0x32b, 0x338, 0x33d}};
            static const int np[3][3] = {{0x1e3, 0x1e4, 0x1fe}, {0x2a2, 0x2a3, 0x2bd}, {0x322, 0x323, 0x33d}};
            static const int nt[3] = {0x8ae, 0x8ba, 0x8bc};
            static const int pn[3][3] = {{0x65d, 0x22, 0x16}, {0x65c, 0x0e, 0x19}, {0x65b, 0x0d, 0x05}};
            for (int k = 0; k < 5; ++k)
                a.potions[k] = pot[i][k];
            for (int k = 0; k < 3; ++k)
            {
                a.npcPortraitPal[k] = np[i][k];
                a.namePortrait[k] = pn[i][k];
            }
            a.npcPortraitTile = nt[i];
            a.menuPortrait = 0x982 + i;
            static const int lt[3] = {0x9f0, 0x9f2, 0x9f3}, lx[3] = {0, 0xcf, 0x1b}, ly[3] = {0x7c, 0x45, 0x29};
            a.loadTile = lt[i];
            a.loadX = lx[i];
            a.loadY = ly[i];
        }
        a.trickRepeatLo[2] = i == 2 ? 1 : 2; a.trickRepeatHi[2] = i == 2 ? 4 : 6;
        // the tricks' extras (see TrickRules): the sounds and the follow-ups the ROM has for each species / level pair
        a.trickRules[1].teacherRepeats = i == 1 ? 5 : 1;
        a.trickRules[2].teacherRepeats = i == 2 ? 3 : 5;
        if (i == 0)
            a.trickRules[2].bulbScale = 1;
        if (i == 1)
        {
            a.trickRules[0].startSound = 0x5b; // 0.6 s after the level 1 trick starts
            a.trickRules[1].bulbScale = 1;
            a.trickRules[1].turnAroundAfter = true; // then it turns round and back
            a.trickRules[2].bulbScale = 1;
            a.trickRules[2].loopSoundAnim = 0x89;
            a.trickRules[2].loopSound = 0x1a;
        }
        if (i == 2)
        {
            a.trickRules[0].actorReactionExit = true;
            a.trickRules[1].soundFrame = 0xc; // the belly flop: the screen shakes and the thud sounds on frame 12
            a.trickRules[1].frameSound = 0x2e;
            a.trickRules[1].cameraPan = true;
            TrickRules &t = a.trickRules[2];
            t.bulbScale = 2;
            t.closingReversed = true;
            t.stopOnClose = {0x35, 0x36, 0x37, 0x3a};
            t.chainAnim = 0xf0;
            t.chainSounds = {0x35, 0x36, 0x37};
            t.chainReactions = {{2, 0xf1, 0x3a, 0xbe}, {3, 0xf2, 0x38, 0xff}};
            t.chainWaitAnim = 0xf2;
            t.chainFinalAnim = 0xf3;
            t.chainFinalSound = 0x39;
        }
        return a;
    }
    inline std::vector<SpeciesArt> &speciesArts()
    {
        static std::vector<SpeciesArt> v = {baseSpeciesArt(0), baseSpeciesArt(1), baseSpeciesArt(2)};
        return v;
    }
    inline const SpeciesArt &artOf(int species)
    {
        std::vector<SpeciesArt> &v = speciesArts();
        return v[species >= 0 && size_t(species) < v.size() ? size_t(species) : 0];
    }
} // namespace gp