// walk_pet_activities.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of PetActivities.
// What they need from the rest of the walk session comes in through PetActivitiesEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct PetActivitiesEnv
{
    bool&animDone;
    int&animFrame;
    int&animId;
    bool&animLoop;
    std::function<AnimRef(const std::string&)> animRefById;
    bool&animReverse;
    menubar::MenuBar &bar;
    BarAnim&barAnim;
    int&barBusy;
    std::unique_ptr<gp::ReactionSeq> &barkSeq;
    int &barkStep;
    gp::CameraShake &cameraShake;
    int &colorDir;
    int &colorIdx;
    int &colorStep;
    int &colorTicks;
    gp::ContentDB &db;
    gp::Dialogue &dialogue;
    std::unique_ptr<gp::EncounterSequence>&enc;
    int &exclaimTicks;
    bool&faceLeft;
    int&facing;
    int&feedCount;
    bool&feedFood;
    int&feedHappy;
    int&feedRepeats;
    int&feedStep;
    unsigned int&gameTicks;
    std::function<AnimRef(int)> getAnim;
    gp::EncounterHost &host;
    long int &hungerDecayAccum;
    gp::ThoughtTimer &hungerTimer;
    std::function<void()> importPetAnims;
    std::function<std::vector<int>()> machineColours;
    gp::ThoughtTimer &moodTimer;
    int&overId;
    int&overTicks;
    BarAnim &panelAnim;
    bool &panelClosing;
    int &petPal;
    gp::TrickShowcase &petTrick;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    Pose&pose;
    std::unique_ptr<gp::ReactionSeq> &potionSeq;
    int &potionStep;
    int &questionBubble;
    int &questionStep;
    int &questionTicks;
    std::function<std::string(int, int)> reactAnimId;
    std::function<int()> restIdleId;
    std::function<int()> restIdleRate;
    gp::Rom&rom;
    std::unique_ptr<RoomView> &room;
    std::function<void()> &saveGame;
    gp::ScriptHost &scripts;
    const gp::Settings &settings;
    int&settlePhase;
    std::function<void(int, int)> sfxFn;
    gp::ThoughtTimer &sickTimer;
    int &sp;
    int &stinkTicks;
    int &thoughtIcon;
    int &thoughtTicks;
    long int &tiredDecayAccum;
    gp::ThoughtTimer &tiredTimer;
    const gp::TrickLayout &trickLay;
    int &trickLevel;
    int &trickStep;
    int &tutEvent;
    bool &tutNudge;
    int &tutStep;
    int &tutTicks;
    int&walkTicks;
    std::unique_ptr<gp::ReactionSeq> &washSeq;
    int&washStep;
};

class PetActivities : public PetActivitiesEnv
{
public:
    explicit PetActivities(PetActivitiesEnv env) : PetActivitiesEnv(std::move(env)) {}
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, next); }
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }

    // PlaySoundFromCategory(species, category, volume): the pet's own voice (pet_voice.hpp)
    auto petSound(int category, int vol)
    { gp::audio().playSfx(gp::petVoiceId(sp, category), vol); }

    auto playFeedHappy()
    {
        // (3, 2) in the original: 3 = repeat count, 2 = probably the variant. Try 0 if the art looks wrong.
        petSound(4, 0xbe); // PlayPositiveReactionAnim: the happy panting
        if (!host.pet.play(gp::ReactAnim::Positive, 2))
            animDone = true; // no art: let the sequence move on
    }

    // TickActor_RepeatAnimThenPause: each time the eating animation starts, category 6, or 5 one time in three, at 0xbe
    auto chewSound()
    { petSound(std::rand() % 3 == 0 ? 5 : 6, 0xbe); }

    auto startFeed()
    {
        facing = 1; // face down so the front-facing happy plays
        walkTicks = 0;
        feedFood = true;
        feedStep = 1;
        playAnim(gp::artOf(sp).eat, false, 14, Pose::Special);
    }

    auto startMedicine(int curedIllness)
    {
        petSound(0xa, 0xff); // HandleCureIllnessEncounter: the gulp
        facing = 1;
        walkTicks = 0;
        feedFood = false;
        feedStep = 3;
        feedHappy = 0;
        overId = 137 + (curedIllness % 13);
        overTicks = 0;
        playFeedHappy();
    }

    auto tickFeed()
    {
        if (!feedStep || !animDone)
            return;
        if (feedStep == 1)
        {
            feedRepeats = 4 + std::rand() % 6; // RandRange(4,9)
            feedCount = 1;
            feedStep = 2;
            playAnim(gp::artOf(sp).eat, false, 14, Pose::Special);
            chewSound();
        }
        else if (feedStep == 2)
        {
            if (++feedCount <= feedRepeats)
            {
                playAnim(gp::artOf(sp).eat, false, 14, Pose::Special);
                chewSound();
            }
            else
            {
                feedStep = 3;
                feedHappy = 0;
                playFeedHappy();
            }
        }
        else if (feedStep == 3)
        {
            if (++feedHappy < 3)
                playFeedHappy();
            else
            {
                if (feedFood && player.care[gp::Happiness] < 0x1a)
                    player.adjustCare(gp::Happiness, 6);
                feedStep = 0;
                overId = -1;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            }
        }
    }

    // ---- first-time messages (TickFirstNPCEncounterSequence, activity 0x26); see settings.hpp ----
    auto startWorldTutorial(int event)
    {
        tutEvent = event;
        tutStep = 1;
        settlePhase = 0;
        walkTicks = 0;
    }

    auto tickWorldTutorial()
    {
        if (tutStep == 1) // the pet turns to face the front (TickActor_SettleFacing)
        {
            if (!host.pet.settle())
                return;
            if (!tutNudge)
            {
                player.tutorialsShown = gp::markShown(player.tutorialsShown, tutEvent); // marked shown as it begins
                saveGame();
            }
            // HandleNPCInteractionInput: the idle that fits how the pet is (sick, unhappy, or ordinary)
            facing = 1;
            walkTicks = 0;
            if (!player.isHealthy())
                playAnim(gp::artOf(sp).idleFlagB, true, gp::artOf(sp).idleFlagBRate, Pose::Idle);
            else if (player.care[gp::Happiness] < gp::kUnhappyBelow)
                playAnim(gp::artOf(sp).idleFlagA, true, 7, Pose::Idle);
            else
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            tutTicks = 0;
            tutStep = 2;
        }
        else if (tutStep == 2 && ++tutTicks >= gp::kTutorialWaitTicks) // 0.25 s, then the message
        {
            tutStep = 0;
            if (tutNudge) // ShowOneTimeEventMessage(-1): the message with the Mayor's portrait
            {
                tutNudge = false;
                gp::DialogueBox nb = gp::messageBox(readRomString(rom, gp::kNudgeMessageAddr));
                for (const gp::NpcDef *d : db.allNpcs()) // the base NPC the reminder points to (tuning.nudgeNpc: Mayor Peter)
                    if (d->index == gp::tuning().nudgeNpc && d->id.rfind("base:", 0) == 0)
                    {
                        nb.portrait = gp::uiSpriteId(d->portraitSprite, d->portraitPalette);
                        break;
                    }
                dialogue.show(std::move(nb));
            }
            else
                dialogue.show(gp::messageBox(db.textOr("base:text/" + std::to_string(gp::tutorialText(tutEvent)))));
            tutEvent = -1;
        }
    }

    // ---- the hungry / tired bubble (TickCareStatWatchdog conditions 4 and 6); see thought_bubble.hpp ----
    // The bubble over the pet (kind "hungry", "sick", "tired" or "mood"): the engine system "thought.show" first (change `kind`, or cancel / replace for no bubble). True if it went up.
    bool showThought(const std::string &kind0)
    {
        static const std::pair<const char *, int> icons[] = {{"hungry", gp::kThoughtHungerIcon}, {"sick", gp::kThoughtSickIcon}, {"tired", gp::kThoughtTiredIcon}, {"mood", gp::kThoughtMoodIcon}};
        gp::SystemCtx c;
        c.set("kind", kind0);
        bool up = false;
        scripts.system("thought.show", c, [&]
                       {
            for (const auto &[name, icon] : icons)
                if (c.str("kind") == name)
                {
                    thoughtIcon = icon;
                    up = true;
                } });
        return up;
    }

    bool scriptThinkRun(const std::string &kind)
    {
        if (thoughtIcon >= 0 || room)
            return false;
        if (kind != "hungry" && kind != "sick" && kind != "tired" && kind != "mood")
            return false;
        thoughtTicks = 0;
        return showThought(kind);
    }

    auto tickThoughtBubble()
    {
        const bool walking = pose == Pose::Move;
        const bool calm = tutStep == 0 && !enc && !dialogue.open();
        ++stinkTicks;
        // the stats wear down (conditions 1 and 2)
        // (the engine system "care.decay": a mod may change the amount, cancel it or do it its own way; see systems.hpp)
        if (gp::decayStep(tiredDecayAccum, gp::romToSim(gp::tuning().tiredDecayRomTicks), player.questsCompleted))
        {
            gp::SystemCtx c;
            c.set("stat", "tiredness").set("amount", gp::tiredDecayAmount(sp));
            scripts.system("care.decay", c, [&]
                           { player.adjustCare(gp::Tiredness, -int(c.num("amount"))); });
            tiredTimer.accum = tiredTimer.limit - gp::kThoughtRetryTicks;
        }
        if (gp::decayStep(hungerDecayAccum, gp::romToSim(gp::tuning().hungerDecayRomTicks), player.questsCompleted))
        {
            const int before = player.care[gp::Hunger];
            gp::SystemCtx c;
            c.set("stat", "hunger").set("amount", gp::hungerDecayAmount(sp));
            scripts.system("care.decay", c, [&]
                           { player.adjustCare(gp::Hunger, -int(c.num("amount"))); });
            if (gp::crossedHungry(before, player.care[gp::Hunger]))
                hungerTimer.accum = hungerTimer.limit - gp::kThoughtRetryTicks;
        }
        if (thoughtIcon < 0)
        {
            const char *kind = gp::hungerBubbleStep(hungerTimer, player.care[gp::Hunger], walking, false)       ? "hungry"
                               : gp::sickBubbleStep(sickTimer, !player.isHealthy(), walking, false)                  ? "sick"
                               : gp::tiredBubbleStep(tiredTimer, player.care[gp::Tiredness], walking, false)         ? "tired"
                               : gp::moodBubbleStep(moodTimer, player.care[gp::Happiness], walking, false)           ? "mood"
                                                                                                                     : nullptr;
            thoughtTicks = 0;
            if (kind)
                showThought(kind);
            return;
        }
        ++thoughtTicks;
        // the timers keep counting while a bubble is up, but cannot add another
        gp::hungerBubbleStep(hungerTimer, player.care[gp::Hunger], walking, true);
        gp::tiredBubbleStep(tiredTimer, player.care[gp::Tiredness], walking, true);
        gp::sickBubbleStep(sickTimer, !player.isHealthy(), walking, true);
        gp::moodBubbleStep(moodTimer, player.care[gp::Happiness], walking, true);
        if (thoughtTicks < gp::kThoughtShowTicks)
            return;
        const AnimRef G = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimGrow) + "@28c");
        const AnimRef H = animRefById("base:anim/" + std::to_string(gp::kThoughtAnimHold) + "@28c");
        if (!gp::thoughtView(thoughtTicks, int(G.size()), int(H.size())).done)
            return;
        const int event = calm ? gp::thoughtTutorial(settings.tutorials, player.tutorialsShown, thoughtIcon, pose == Pose::Idle) : -1;
        thoughtIcon = -1;
        if (event >= 0)
            startWorldTutorial(event);
    }

    // ---- a colour potion's reaction (activity 0x13, TickActor_ShockedEncounterSequence); see potion.hpp ----
    // Step 1: turn to face front. Step 2: the emote reaction with the exclamation bubble over the pet, then the shocked one.
    auto startPotionReaction()
    {
        potionStep = 1;
        settlePhase = 0;
        walkTicks = 0;
        potionSeq = std::make_unique<gp::ReactionSeq>(gp::ReactionSeq::Kind::Emote);
    }

    auto tickPotion()
    {
        if (exclaimTicks >= 0)
            ++exclaimTicks;
        if (potionStep == 1)
        {
            if (host.pet.settle())
                potionStep = 2;
        }
        else if (potionStep == 2 && potionSeq && potionSeq->tick(host.pet))
        {
            potionSeq.reset();
            potionStep = 0;
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
        }
    }

    // ---- X in the overworld (HandlePetStageEvents event 5) ----
    // Standing still: activity 0xc settles the pet to face front, then TickActor_IdleRandomSound with a count of 1, the
    // species' call (category 1 or 2). Walking: just category 3 at full volume, unless it is already sounding.
    auto pressCancelInWorld()
    {
        if (pose == Pose::Idle)
        {
            barkStep = 1;
            settlePhase = 0;
            walkTicks = 0;
            barkSeq = std::make_unique<gp::ReactionSeq>(gp::ReactionSeq::Kind::Noise, 1);
        }
        else if (pose == Pose::Move && !gp::audio().sfxActive(gp::petVoiceId(sp, 3)))
            petSound(3, 0xff);
    }

    auto tickBark()
    {
        if (barkStep == 1)
        {
            if (host.pet.settle())
                barkStep = 2;
        }
        else if (barkStep == 2 && barkSeq && barkSeq->tick(host.pet))
        {
            barkSeq.reset();
            barkStep = 0;
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
        }
    }

    auto closeTrickPanel()
    {
        if (barBusy != 4)
            return;
        sfx(trickLay.click, trickLay.clickVolume);
        panelAnim.visible = false;
        panelClosing = false;
        barAnim.visible = false;
        bar.forceClose();
        barBusy = 0;
    }

    auto startQuestion()
    {
        questionStep = 1;
        questionTicks = 0;
        questionBubble = trickLay.questionBubble;
    }

    auto startTrick(int level)
    {
        trickStep = 1;
        trickLevel = level;
        settlePhase = 0;
        walkTicks = 0;
        petTrick.startWith(level, sp, gp::TrickShowcase::petRepeats(level, sp, player.personality[gp::Lightbulbs], [&](int lo, int hi)
                                                                   { return lo + std::rand() % (hi - lo); })); // RandRange: hi exclusive
    }

    auto pickTrick(int slot)
    {
        panelAnim.visible = false;
        panelClosing = false;
        barAnim.visible = false;
        bar.forceClose();
        barBusy = 0;
        // the engine system "trick.perform": the player picked a slot on the tricks panel. `known` is 1 if the pet has learned it; the default is the trick (level slot + 1),
        // or the "?" bubble if it is not known. Change `known` (a mod may let it do the trick anyway), cancel for nothing, or replace and do it with player.play.
        gp::SystemCtx c;
        c.set("slot", slot).set("level", slot + 1).set("known", player.knowsTrick(slot) ? 1 : 0);
        scripts.system("trick.perform", c, [&]
                       {
            if (c.num("known") == 0)
                startQuestion();
            else
                startTrick(std::clamp(int(c.num("level")), 1, 3)); // (the prize roll, RollPetPrizeId(3), isn't wired)
                       });
    }

    auto tickQuestion()
    {
        if (!questionStep)
            return;
        const AnimRef A = animRefById("base:anim/" + std::to_string(questionBubble) + "@28c");
        const int len = gp::exclaimSteps(int(A.size()));
        ++questionTicks;
        if (questionStep == 1 && questionTicks >= len)
        {
            questionStep = 2;
            questionTicks = 0;
        }
        else if (questionStep == 2 && questionTicks >= int(trickLay.questionWait * 30))
        {
            questionStep = 3;
            questionBubble = trickLay.answerBubble;
            questionTicks = 0;
        }
        else if (questionStep == 3 && questionTicks >= len)
        {
            questionStep = 0;
            questionBubble = 0;
        }
    }

    auto tickTrick()
    {
        if (!trickStep)
            return;
        if (trickStep == 1)
        {
            if (!host.pet.settle()) // TickActor_ReactionState: face the front first
                return;
            trickStep = 2;
        }
        gp::TrickShowcase::Host th;
        th.full = true;
        th.play = [&](int id, int rate)
        {
            if (getAnim(id).size() == 0)
                return false;
            playAnim(id, false, rate, Pose::Special);
            return true;
        };
        th.playReverse = [&](int id, int rate, bool)
        {
            if (getAnim(id).size() == 0)
                return false;
            playAnim(id, false, rate, Pose::Special);
            animReverse = true;
            return true;
        };
        th.cameraPan = [&]
        { cameraShake.start(); };
        th.setFacing = [&](bool faceRight) // the monkey's turn-around: the turn art is mirrored by faceLeft
        {
            faceLeft = !faceRight;
            facing = 2;
        };
        th.busy = [&]
        { return !animLoop && !animDone; };
        th.anim = [&]
        { return animId; };
        th.frame = [&]
        { return animFrame; };
        th.sound = [&](int id, int vol)
        { sfx(id, vol); };
        th.soundActive = [&](int id)
        { return gp::audio().sfxActive(id); };
        th.stopSound = [&](int id)
        { gp::audio().stopSfx(id); };
        th.rand = [&](int lo, int hi)
        { return lo + std::rand() % (hi - lo); }; // RandRange: hi exclusive
        th.seconds = [&]
        { return double(gameTicks) / 60.0; }; // gameTicks is +2 per 30 Hz step
        if (petTrick.tick(th))
        {
            trickStep = 0;
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
        }
    }

    auto tickColorMachine(bool zHeld)
    {
        if (colorStep == 1)
        {
            if (!host.pet.settle())
                return;
            if (!zHeld) // the button was let go before the pet was facing front: nothing happens
            {
                colorStep = 0;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
                return;
            }
            if (const int i = gp::colourIndex(machineColours(), player.appearance); i >= 0)
                colorIdx = i;
            colorDir = gp::nextDirection(colorDir);
            colorTicks = 0;
            colorStep = 2;
            sfx(gp::kColorMachineSound);
            return;
        }
        if (colorStep != 2)
            return;
        if (!zHeld) // letting go of the action button stops it on this colour
        {
            importPetAnims();
            saveGame();
            colorStep = 0;
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            return;
        }
        const gp::MinigameDef &machine = db.minigameOr("base:minigame/colour_machine");
        if (colorTicks++ % std::max(1, machine.get("stepTicks", gp::kColorStepTicks)) == 0)
        {
            const std::vector<int> colours = machineColours();
            colorIdx = gp::stepIndex(colorIdx, colorDir, int(colours.size()));
            const int pal = gp::colourAt(colours, colorIdx, petPal);
            player.appearance = player.baseColour = petPal = pal;
            try
            {
                gp::importAnim(rom, db, restIdleId(), petPal); // only the idle on show; the rest follows when it stops
            }
            catch (const std::exception &)
            {
            }
        }
    }

    auto startWash()
    {
        // the engine system "pet.wash": the pet is washed (bar button 4); cancel or replace and it is not (a mod may do its own: player.adjustCare ...)
        gp::SystemCtx c;
        c.set("cleanliness", player.care[gp::Cleanliness]);
        scripts.system("pet.wash", c, [&]
                       { washStep = 1; });
    }

    auto tickWash()
    {
        switch (washStep)
        {
        case 0:
            return;
        case 1: // TickActor_ReactionState: a sitting pet gets up first, then the bubble starts
        {
            const gp::SitPhase ph = gp::sitPhase(sp, animId, pose == Pose::Idle, pose == Pose::Special, animDone);
            if (ph == gp::SitPhase::Sitting)
            {
                playAnim(gp::artOf(sp).sitAlt, false, 14, Pose::Special);
                break;
            }
            if (ph != gp::SitPhase::None) // sitting down or getting up: wait
                break;
            // Always front-on: after walking sideways the pet settles facing the front, but our `facing` stays
            // "side" until something resets it, and the idle and the happy reaction would turn sideways.
            facing = 1;
            walkTicks = 0;
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            overId = gp::kWashBubbleAnim;
            overTicks = 0;
            washStep = 2;
            break;
        }
        case 2: // the bubble plays once at the pet
        {
            const int frames = int(animRefById(reactAnimId(gp::kWashBubbleAnim, 0x28c)).size());
            if (overTicks < gp::bubbleTicks(frames))
                break;
            overId = -1;
            gp::applyWash(player); // +100 hygiene, original colour back
            const int pal = gp::washedPalette(player, petPal);
            if (pal != petPal) // the potion colour comes off
            {
                petPal = pal;
                importPetAnims();
            }
            saveGame();
            washSeq = std::make_unique<gp::ReactionSeq>(gp::ReactionSeq::Kind::Positive, gp::kWashHappyRepeats, gp::kWashHappyVariant);
            washStep = 3;
            break;
        }
        case 3: // PlayPositiveReactionAnim(pet, 3, 2), then the mood top-up
            if (washSeq && washSeq->tick(host.pet))
            {
                washSeq.reset();
                gp::applyWashMood(player);
                host.petIdle();
                washStep = 0;
                saveGame();
            }
            break;
        }
    }
};
