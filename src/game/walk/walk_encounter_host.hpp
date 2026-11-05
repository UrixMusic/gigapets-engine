// walk_encounter_host.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of EncounterHostBindings.
// What they need from the rest of the walk session comes in through EncounterHostBindingsEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct EncounterHostBindingsEnv
{
    bool&animDone;
    int&animFrame;
    int&animId;
    bool&animLoop;
    std::function<AnimRef(const std::string&)> animRefById;
    bool&animReverse;
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    gp::NpcActor*&encNpc;
    gp::WorldItems &events;
    int &exclaimTicks;
    bool&faceLeft;
    int&facing;
    std::function<AnimRef(int)> getAnim;
    std::function<bool(int)> isRestIdle;
    const std::vector<gp::ItemDef>&itemDefs;
    std::function<std::string(int)> itemName;
    bool &npcMute;
    gp::Rng&npcRng;
    std::function<int(int)> ownedCount;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    int &playerSpecies;
    gp::Point&pos;
    Pose&pose;
    std::function<std::string(int, int)> reactAnimId;
    std::function<int()> restIdleId;
    std::function<int()> restIdleRate;
    gp::ScriptHost &scripts;
    int&settlePhase;
    int&settleTicks;
    std::function<void(int, int)> sfxFn;
    int &sp;
    int&walkTicks;
    std::unique_ptr<AreaWorld> &world;
};

class EncounterHostBindings : public EncounterHostBindingsEnv
{
public:
    explicit EncounterHostBindings(EncounterHostBindingsEnv env) : EncounterHostBindingsEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, next); }

    auto host_classifyRun(int petDelta, int npcDelta, int &playerCode, int &animalCode)
    {
        // the engine system "encounter.classify": the reaction codes the rules gave (1, 2 = friendly, 3, 7 = startled / sad, 4, 5 = hostile, 6 = bored); a mod may change them
        gp::SystemCtx c;
        c.set("npc", encNpc ? encNpc->def->id : std::string()).set("pet", petDelta).set("target", npcDelta).set("player", playerCode).set("animal", animalCode);
        scripts.system("encounter.classify", c, [] {});
        playerCode = int(c.num("player"));
        animalCode = int(c.num("animal"));
    }

    bool walkAxis(int target, bool xAxis)
    {
        int &c = xAxis ? pos.x : pos.y;
        if (c == target)
            return true;
        const uint8_t dir = xAxis ? (target < c ? gp::DirLeft : gp::DirRight)
                                  : (target < c ? gp::DirUp : gp::DirDown);
        const int before = c;
        pos = gp::stepPlayer(*world, pos, dir).pos;
        if ((before < target) != (c < target)) // overshot: snap
            c = target;

        pose = Pose::Move;
        settleTicks = 0;
        const int oldF = facing;
        const bool oldL = faceLeft;
        if (xAxis)
        {
            facing = 2;
            faceLeft = dir == gp::DirLeft;
        }
        else
            facing = dir == gp::DirUp ? 0 : 1;
        if (facing != oldF || faceLeft != oldL)
            walkTicks = 0;
        ++walkTicks;
        return c == target;
    }

    auto host_petXRun()
    { return pos.x; }

    auto host_npcXRun()
    { return encNpc->x(); }

    auto host_npcYRun()
    { return encNpc->y(); }

    auto host_petWalkToXRun(int x)
    { return walkAxis(x, true); }

    auto host_petWalkToYRun(int y)
    { return walkAxis(y, false); }

    auto host_npcLockRun()
    {
        const bool wasWalking = encNpc->walking();
        if (wasWalking)
            encNpc->startActivity(gp::kActivityTalkedTo, npcRng);
        encNpc->locked = true;
        return wasWalking;
    }

    auto host_npcSettledRun()
    {
        return encNpc->settled();
    }

    auto host_npcUnlockRun()
    {
        encNpc->locked = false;
        encNpc->clearForced();
    }

    auto host_markFriendedRun()
    {
        player.markFriended(encNpc->def->id);
        return int(player.friended.size());
    }

    auto host_eventRun(const char *k, int v)
    {
        std::printf("event %s 0x%x\n", k, v);
        if (std::strcmp(k, "sound") == 0)
            sfx(v); // PlaySoundEffect(0x54, 200) when a friend is made
    }

    auto host_petYRun()
    { return pos.y; }

    auto host_giftsRun(int friends, int &drop, int &milestone)
    {
        // the engine system "friend.gift": what a new friend gives. `drop` and `milestone` are item ids ("" = none); the milestone is the extra gift at 10, 20, 30, 35 and 40 friends
        const auto idOf = [&](int n) -> std::string { return n > 0 && size_t(n) < itemDefs.size() ? itemDefs[size_t(n)].id : std::string(); };
        const auto numOf = [&](const std::string &id) { const gp::ItemDef *d = id.empty() ? nullptr : db.item(id); return d ? d->index : 0; };
        gp::SystemCtx c;
        c.set("npc", encNpc ? encNpc->def->id : std::string()).set("friends", friends).set("drop", idOf(drop)).set("milestone", idOf(milestone));
        scripts.system("friend.gift", c, [] {});
        drop = numOf(c.str("drop"));
        milestone = numOf(c.str("milestone"));
    }

    auto host_spawnItemRun(int item, int x, int y)
    { events.placeAt(cur->def->legacyId, item, x, y, false); }

    auto host_pet_playRun(gp::ReactAnim a, int v)
    {
        const bool side = facing == 2;
        if (facing == 0 && (a == gp::ReactAnim::Idle || a == gp::ReactAnim::Positive ||
                            a == gp::ReactAnim::Emote || a == gp::ReactAnim::Shocked))
            return false; // facing up: no art in these tables
        const int id = gp::reactId(a, sp, v, side);
        const int n = id < 0 ? 0 : int(getAnim(id).size());
        if (n == 0)
            return false;
        playAnim(id, false, a == gp::ReactAnim::Bored ? 7 : 14, Pose::Special);
        animReverse = gp::playsReversed(a);
        if (animReverse)
            animFrame = n - 1;
        return true;
    }

    auto host_pet_busyRun()
    { return !animLoop && !animDone; }

    auto host_pet_soundRun(int c, int vol)
    { gp::audio().playSfx(gp::petVoiceId(playerSpecies, c), vol < 0x20 ? 0xbe : vol); }; // PlaySoundFromCategory(species, c, vol); see host.npc.sound for vol 4

    auto host_pet_bubbleRun(int id)
    {
        if (id == gp::kExclaimIntroAnim) // the exclamation over the head (SpawnMessageBubble 0xb, phase 4)
            exclaimTicks = 0;
        else
            std::printf("bubble 0x%x\n", id);
    }

    auto host_pet_randRun(int lo, int hi)
    { return hi <= lo ? lo : lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive

    bool host_pet_settleRun()
    {
        if (settlePhase == 0)
        {
            if (facing == 1)
                return true;
            if (facing == 0)
            {
                faceLeft = (std::rand() % 3) == 0;
                playAnim(gp::artOf(sp).turn180, false, 14, Pose::Settle, {{gp::artOf(sp).turn90, false, 14}, {restIdleId(), true, restIdleRate()}});
            }
            else
                playAnim(gp::artOf(sp).turn90, false, 14, Pose::Settle, {{restIdleId(), true, restIdleRate()}});
            settlePhase = 1;
            return false;
        }
        if (pose == Pose::Idle && isRestIdle(animId))
        {
            facing = 1;
            settlePhase = 0;
            return true;
        }
        return false;
    }

    auto host_petIdleRun()
    {
        if (facing == 1)
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
        else if (facing == 2)
            playAnim(gp::artOf(sp).idleSide, true, 14, Pose::Idle); // gp::artOf(0).idleSide
        else
        {
            pose = Pose::Move; // up: no idle art, keep the old hold
            walkTicks = 0;
            settleTicks = 0;
        }
    }

    auto host_petFaceRun(bool left)
    { facing = 2; faceLeft = left; walkTicks = 0; }; // petIdle follows

    auto host_petEngageRun()
    {
        if (facing == 0)
        {
            facing = 2;
            faceLeft = pos.x >= encNpc->x();
        }
        playAnim(gp::artOf(sp).idleSide, true, 14, Pose::Idle); // gp::artOf(0).idleSide
    }

    auto host_npcIdleRun()
    { encNpc->clearForced(); }

    auto host_npcFacePetRun()
    {
        encNpc->body.facing = encNpc->x() < pos.x ? gp::FaceRight : gp::FaceLeft;
    }

    auto host_npc_soundRun(int c, int vol)
    {
        if (npcMute)
            return;
        const int s = encNpc->def->speciesOrAnim == 0xfffe ? playerSpecies : encNpc->def->speciesOrAnim;
        // The idle noise's one odd call passes 4 where the others pass 0xbe; at volume 4 it is inaudible and the first
        // sound of a meeting sometimes vanished, so it plays at the usual level (GUESS: 4 isn't a volume).
        gp::audio().playSfx(gp::petVoiceId(gp::knownSpecies(s) ? s : playerSpecies, c), vol < 0x20 ? 0xbe : vol);
    }

    auto host_npc_settleRun()
    { return encNpc->settled(); }

    auto host_npc_busyRun()
    { return !encNpc->forcedAnimId.empty() && encNpc->animBusy(); }

    auto host_npc_playRun(gp::ReactAnim a, int v)
    {
        const gp::NpcActor &n = *encNpc;
        const int f = n.body.facing;
        const bool side = f == gp::FaceLeft || f == gp::FaceRight;
        if (!side && f != gp::FaceDown &&
            (a == gp::ReactAnim::Idle || a == gp::ReactAnim::Positive || a == gp::ReactAnim::Emote || a == gp::ReactAnim::Shocked))
            return false; // facing up: no art
        const int idle = std::atoi(n.def->anim.c_str() + n.def->anim.rfind('/') + 1);
        const int tier = idle < 0x70 ? 0 : idle < 0xc0 ? 1
                                                       : 2; // GUESS: tier from the idle id's range
        const int sp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
        const int id = gp::reactId(a, sp, v, side);
        if (id < 0)
            return false;
        const gp::Id full = reactAnimId(id, n.def->appearancePalette);
        if (animRefById(full).size() == 0)
            return false;
        encNpc->forceAnim(full); // change forceAnim/forcedAnim to store an Id (std::string)
        // Outros play last frame to first, same as the pet's (host.pet.play); the ROM shares one routine for both.
        encNpc->forcedReverse = gp::playsReversed(a);
        const bool neg = a == gp::ReactAnim::Negative || a == gp::ReactAnim::NegIntro ||
                         a == gp::ReactAnim::NegOutro;
        const bool panda = gp::artOf(sp).facesRight; // the art faces right (the panda's)
        if (neg)
            encNpc->forcedFlip = panda ? (f == gp::FaceLeft) : (f != gp::FaceLeft);
        else
            encNpc->forcedFlip = side && gp::reactMirror(sp, f == gp::FaceRight);

        return true;
    }
};
