// walk_world_actions.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of WorldActions.
// What they need from the rest of the walk session comes in through WorldActionsEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

// The oldest hole filling in when a sixth is dug (UpdateDigHoleVisuals 0x03fa5f): the dig animation (bubble 0x16) played
// backwards, at 28 fps, where that hole was.
struct HoleFill
{
    int x, y, ticks;
};

// The story blasts (game/story_blast.hpp): the explosion animation at a world spot.
struct Blast
{
    int x, y, ticks;
};

struct WorldActionsEnv
{
    bool &afterPickup;
    bool&animDone;
    std::function<AnimRef(const std::string&)> animRefById;
    std::vector<Blast> &blasts;
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    gp::Dialogue &dialogue;
    int&digStep;
    int&digTicks;
    gp::WorldItems &events;
    std::vector<HoleFill> &holeFills;
    gp::EncounterHost &host;
    std::function<bool(int)> isSpecialItem;
    const std::vector<gp::ItemDef>&itemDefs;
    std::function<std::string(int)> itemName;
    const int &kDigTicks;
    const int &kHoleFillFps;
    Minigames &minigames;
    int&overId;
    int&overTicks;
    std::function<int(int)> ownedCount;
    std::function<void()>&pendingScreen;
    std::function<void()> petFellIll;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    gp::Point&pos;
    std::function<int(int, int)> randIn;
    std::function<std::string(int, int)> reactAnimId;
    std::function<int()> restIdleId;
    std::function<int()> restIdleRate;
    const std::function<int(int, int)> &rnd;
    gp::ScriptHost &scripts;
    int&settlePhase;
    std::function<void(int, int)> sfxFn;
    int &sp;
    gp::FixedSpawnEnv &spawnEnv;
    std::vector<int>&swiftDropItems;
    int&swiftDropStep;
    int&swiftDropTicks;
    int&walkTicks;
};

class WorldActions : public WorldActionsEnv
{
public:
    explicit WorldActions(WorldActionsEnv env) : WorldActionsEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, next); }

    // AdjustPersonalityStat + AddToStatCounter(2 + stat*4, applied delta)
    auto personalityDelta(int stat, int delta)
    {
        const int before = player.personality[stat];
        player.adjustPersonality(gp::PersonalityStat(stat), delta);
        itemHost.addStatCounter(2 + stat * 4, player.personality[stat] - before);
    }

    // GrantItemOrEvent, special-id branches only. Returns false for normal items.
    bool grantSpecial(int id, bool sound)
    {
        if (!isSpecialItem(id))
            return false;

        if (id == 0x109)
        {
            player.adjustMoney(randIn(5, 11));
            if (sound)
                sfx(0x14, 200);
        }
        else if (id == 0x10a)
        {
            player.adjustMoney(randIn(20, 51));
            if (sound)
                sfx(0x14, 200);
        }
        else if (id == 0x10b) // mystery bag
        {
            if (randIn(0, 99) < 60)
            {
                const int r = randIn(1, 5);
                if (r == 1)
                {
                    player.adjustMoney(randIn(30, 61));
                    if (sound)
                        sfx(0x14, 200);
                }
                else // r = 2..5 -> stat 0..3
                    personalityDelta(r - 2, randIn(1, 10));
                if (sound)
                    sfx(0x53, 200);
            }
            else
            {
                const int r = randIn(1, 8);
                switch (r)
                {
                case 1:
                    player.adjustCare(gp::Tiredness, -80);
                    break;
                case 2:
                    player.illnessId = randIn(0, 9);
                    petFellIll(); // SetActiveIllness
                    break;
                case 3:
                    player.adjustCare(gp::Cleanliness, -80);
                    break;
                case 4:
                    player.adjustCare(gp::Happiness, -80);
                    break;
                default: // r = 5..8 -> stat 0..3
                    personalityDelta(r - 5, -randIn(1, 10));
                    break;
                }
                if (sound)
                    sfx(1, 200);
            }
        }
        else // ids 2..17
        {
            const int stat = (id - 2) / 4, k = (id - 2) % 4;
            const int amt = k == 0 ? 1 : k == 1 ? randIn(1, 3)
                                     : k == 2   ? randIn(1, 5)
                                                : 50;
            personalityDelta(stat, amt);
            if (sound)
                sfx(0x53, 200);
        }
        return true;
    }

    auto tryPickup(int idx)
    {
        const int area = cur->def->legacyId;
        gp::PickupEnv pe;
        pe.held = [&](int i)
        { return ownedCount(i); };
        pe.capacity = [&](int i)
        { return isSpecialItem(i) ? 1 : (i >= 0 && i < int(itemDefs.size()) ? itemDefs[i].maxQty : 1); };
        pe.grant = [&](int i)
        {
            if (!grantSpecial(i, true))
            {
                if (i >= 0 && i < int(itemDefs.size()))
                    player.addItem(itemDefs[size_t(i)].id, 1);
                sfx(0x53, 200); // GrantItemOrEvent: PlaySoundEffect(0x53, 200) when the pickup asks for a sound
                gp::onItemGranted(events, area, i, db.fixedSpotsIn(area), spawnEnv);
            }
        };
        const int pickedItem = idx >= 0 && idx < events.size() ? events.events()[size_t(idx)].item : -1;
        const gp::WorldItem *pickedEv = idx >= 0 && idx < events.size() ? &events.events()[size_t(idx)] : nullptr;
        const gp::Id pickedItemId = pickedItem >= 0 && pickedItem < int(itemDefs.size()) ? itemDefs[size_t(pickedItem)].id : gp::Id();
        const int pickX = pickedEv ? pickedEv->x : pos.x, pickY = pickedEv ? pickedEv->y : pos.y;
        if (!pickedItemId.empty() && scripts.fireBeforePickup(cur->def->id, pickedItemId, pickX, pickY).suppress)
            return; // a mod left it lying there
        const gp::PickupOutcome o = events.pickup(idx, pe);
        if (o.result == gp::PickupResult::Granted && pickedItem >= 0 && pickedItem < int(itemDefs.size()))
        {
            // Spots with notLegacyFlag stay gone once their item has been picked up (SetLegacyFlag).
            const gp::Id &pickedId = itemDefs[size_t(pickedItem)].id;
            for (const gp::FixedSpotDef *s : db.fixedSpotsIn(area))
                if (s->notLegacyFlag && s->item == pickedId)
                {
                    gp::markLegacy(player, pickedId);
                    break;
                }
        }
        if (o.result == gp::PickupResult::Granted && !pickedItemId.empty())
            scripts.fireAfterPickup(cur->def->id, pickedItemId, pickX, pickY);
        if (o.result == gp::PickupResult::Granted)
            afterPickup = true; // ProcessStoryModeObjectInteraction: IsFirstTimeEvent(0xf) right after the item is granted
        if (o.result == gp::PickupResult::NeedItem)
            dialogue.show(gp::messageBox("You need " + itemName(o.needItem) + " first.")); // placeholder text
        else if (o.result == gp::PickupResult::AtCapacity)
            dialogue.show(gp::messageBox("You can't carry any more.")); // placeholder text
    }

    auto tickHoleFills()
    {
        if (holeFills.empty())
            return;
        const int frames = int(animRefById("base:anim/22@28c").size());
        for (HoleFill &f : holeFills)
            ++f.ticks;
        holeFills.erase(std::remove_if(holeFills.begin(), holeFills.end(), [&](const HoleFill &f)
                                       { return f.ticks * kHoleFillFps / 30 >= frames; }),
                        holeFills.end());
    }

    auto tickBlasts()
    {
        if (blasts.empty())
            return;
        const int frames = int(animRefById("base:anim/" + std::to_string(gp::kBlastAnim) + "@28c").size());
        for (Blast &b : blasts)
            ++b.ticks;
        blasts.erase(std::remove_if(blasts.begin(), blasts.end(), [&](const Blast &b)
                                    { return b.ticks * gp::kBlastFps / 30 >= frames; }),
                     blasts.end());
    }

    auto startDig()
    { digStep = 1; digTicks = 0; settlePhase = 0; walkTicks = 0; }

    auto tickDig()
    {
        if (!digStep)
            return;
        switch (digStep)
        {
        case 1: // settle facing (the ROM's reaction step is skipped)
            if (host.pet.settle())
                digStep = 2;
            break;
        case 2: // bubble + RunSoundedAnimationTask
            overId = 0x16;
            overTicks = 0;
            player.adjustCare(gp::Cleanliness, -gp::artOf(sp).digDirt);
            player.adjustCare(gp::Tiredness, -1); // check this direction in-game
            playAnim(gp::artOf(sp).dig, true, 14, Pose::Special); // PlayTaskAnimationVariant
            sfx(9, 0xbe); // RunSoundedAnimationTask: PlaySoundEffect(9, 0xbe)
            digTicks = 0;
            digStep = 3;
            break;
        case 3: // wait for sound 9 to end (IsSoundIdActive); without an audio device, a fixed time
            if (++digTicks >= (gp::audio().ready() ? 1 : kDigTicks) && !gp::audio().sfxActive(9))
                digStep = 4;
            break;
        case 4:
        { // IsBubbleFinished, then the hit test
            const int frames = int(animRefById(reactAnimId(0x16, 0x28c)).size());
            if (frames > 0 && overTicks * kBubbleFps / 30 < frames)
                break;
            overId = -1;
            const size_t holesBefore = events.holes().size();

            uint32_t bits = 0; // player flags -> bits
            for (int n = 0; n < 32; ++n)
                if (player.hasTrigger(n))
                    bits |= 1u << n;

            const gp::DigResult r = events.dig(cur->def->legacyId, pos.x, pos.y, bits, rnd);
            if (r.filledHole && r.filledHole->area == cur->def->legacyId) // the oldest hole fills in
                holeFills.push_back({r.filledHole->x, r.filledHole->y, 0});

            for (int n = 0; n < 32; ++n) // bits -> player flags (only new ones matter)
                if (bits >> n & 1u)
                    player.setTrigger(n);

            const bool found = r.kind == gp::DigResult::Kind::Item;
            std::printf("dig: %s\n", found ? "item" : r.kind == gp::DigResult::Kind::Dud ? "dud"
                                                                                         : "miss");
            std::printf("holes now: %zu\n", events.holes().size());
            scripts.fireAfterDig(cur->def->id, pos.x, pos.y, found ? "item" : r.kind == gp::DigResult::Kind::Dud ? "dud" : "miss",
                                 found && r.item >= 0 && r.item < int(itemDefs.size()) ? itemDefs[size_t(r.item)].id : gp::Id());
            if (r.triggerSet)
                std::printf("trigger flags 0x%x (SavePetStatusBlock)\n", bits);
            animDone = true;
            digStep = 5;
            break;
        }
        case 5: // wait for the reaction, then idle
            if (animDone)
            {
                digStep = 0;
                playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
            }
            break;
        }
    }

    auto startItemDrop(std::vector<int> items)
    {
        if (items.empty())
            return;
        sfx(0x5d);
        swiftDropItems = std::move(items);
        swiftDropStep = 1;
        swiftDropTicks = 0;
        settlePhase = 0;
        walkTicks = 0;
    }

    auto tickSwiftDrop()
    {
        if (!swiftDropStep || minigames.active() || pendingScreen) // activity runs once the world is back
            return;
        if (swiftDropStep == 1)
        {
            if (host.pet.settle())
                swiftDropStep = 2;
        }
        else if (++swiftDropTicks >= 45) // 1.5 s at 30 Hz
        {
            // ROM RandRange is exclusive at the top; this randIn is inclusive, so hi - 1.
            int x = minigames.petX + randIn(-35, 35), y = minigames.petY + randIn(-2, 3);
            for (size_t k = 0; k < swiftDropItems.size(); ++k)
            {
                if (k > 0) // each next item: 6..10 lower, 0x14..0x22 to the other side of the target x (sign of the side: GUESS)
                {
                    y += randIn(6, 0xb - 1);
                    const int d = randIn(0x14, 0x23 - 1);
                    x = minigames.petX + (x >= minigames.petX ? -d : d);
                }
                events.placeAt(cur->def->legacyId, swiftDropItems[k], x, y, false); // persist flag is a guess
            }
            sfx(0x54);
            swiftDropStep = 0;
            playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
        }
    }
};
