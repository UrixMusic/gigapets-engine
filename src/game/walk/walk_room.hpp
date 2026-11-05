// walk_room.hpp - the player homes and hotels in a walk session: the room view, entering and leaving, the pet's sequences in it (eating, sleeping, washing, curing), placing furnishings, giving items.
//
// Slice 1 of taking runWalk() apart (walk.cpp). The code is the one that used to be lambdas inside runWalk(); it is now the methods of RoomController. What it needs from the
// rest of the walk session comes in through RoomEnv (references to the session's variables and callbacks to its helpers, under the names they had), so the method bodies read the
// same as before. Include in walk.cpp after the types it uses (Pose, AnimStep, ItemMenu, AnimRef, WalkState, WalkServices).
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

// A player home or hotel (game/room_scene.hpp): while this exists the world is frozen and the room is shown. The
// pet is drawn with the indoor ("style") art and only walks left and right along the floor.
struct RoomView
{
    enum class Phase
    {
        Entering, // from the door, down and across to the floor line
        Idle,
        Walking,
        Settling, // stopped: the walk keeps playing for 0.15 s
        Turning,  // the 90 degree turn back to the front
        Leaving,  // along the floor, then up and across to the door
        Placing,  // choosing where a furnishing goes (the pet isn't shown)
        Placed,   // the item is down: the room stays up for a moment, then fades back to the item screen
        Eating    // at the food bowl (see eatStep)
    };
    int location = 0;
    SDL_Texture *tex = nullptr;
    int x = gp::kRoomDoorX, y = gp::kRoomDoorY; // the pet, in room coordinates
    int scroll = 0;
    int px = gp::kRoomDoorX, py = gp::kRoomDoorY, pscroll = 0; // x, y and scroll before the latest tick (smooth mode)
    int pcurX = 0, pcurY = 0;                                   // the placing cursor before the latest tick
    Phase phase = Phase::Entering;
    bool faceLeft = false;
    int walkTicks = 0, settleTicks = 0, animTicks = 0;
    // Another save slot's home: its pet sits in the room (hasVisitor), or the slot is unused (forRent).
    bool hasVisitor = false, forRent = false;
    int visitSpecies = 0, visitPal = 0, visitTicks = 0;
    std::vector<gp::RoomDecoration> table; // the room's fixed pieces: door 0, bed 1, food bowl 4, ...
    int nearby = -1;                       // the table entry the pet is next to (g_nearbyDecorationIndex)
    gp::SlotItems slots{};   // the 32 furnishing slots this room shows
    gp::RoomSprites sprites; // door, bed, furniture, wall pieces and the equipped items, in layers
    // Placing a furnishing (room_scene.hpp): the item, the slot the cursor is on, the cursor's position, and whether
    // Z has been pressed (the item is put down once the cursor arrives).
    int placeItem = 0, placeSlot = 0, curX = 0, curY = 0;
    bool placeConfirmed = false;
    // Eating at the bowl. Steps follow Pet_RepeatedTrickWithDialogue: 6 walk along the floor, 7 up the diagonal, 8 the
    // panda's intro, 9 lean in for a bite, 10 chew, 11 pause, 12 back down, 13 turn to the front, 14 happy.
    int eatStep = 0, eatRepeat = 0, eatChews = 0, eatPause = 0, eatHappy = 0, eatSX = 0, eatAnim = -1;
    bool eatFlip = false, eatLoop = false;
    bool eatGivesHappy = true;      // the bowl's +6 Happiness at the end; not for a cure
    bool eatWash = false;           // a wash or a sleep: the +10 Happiness top-up at the end (step 20 is the bubble)
    bool sleepQuit = false;         // sleeping to quit the game: no stat changes, and no waking up
    int eatFps = 14;                // the frame rate of eatAnim (the yawn and the sleep run slower)
    bool eatReverse = false;        // eatAnim plays last frame to first (the panda's outro)
    int tutorialEvent = -1;         // a first-time message in progress (steps 40 turn to the front, 41 wait, then show it)
    // Sleeping, steps 30 yawn, 31 along the floor, 32 up the diagonal to the bed, 33 get in, 34 sleep cycles, 35 get out
    // (then 12, 13, 14 as after the bowl).
    int overId = -1, overTicks = 0; // the animation over the pet (the cure's rainbow bubbles), -1 = none
    RoomView() = default;
    RoomView(const RoomView &) = delete;
    RoomView &operator=(const RoomView &) = delete;
    ~RoomView()
    {
        if (tex)
            SDL_DestroyTexture(tex);
    }
};

// What the room code takes from the walk session.
struct RoomEnv
{
    const gp::Rom &rom;
    gp::ContentDB &db;
    const std::vector<gp::ItemDef> &itemDefs;
    gp::Player &player;
    gp::ScriptHost &scripts;
    gp::Dialogue &dialogue;
    WalkState &S;
    WalkServices &svc;
    SDL_Renderer *&ren;
    ItemMenu &im;
    int &sp;
    int &slot;
    const int &W;
    uint32_t &gameTicks;
    int &walkTicks;
    gp::Point &pos;
    gp::Point &prevPos;
    int &petPal;
    int &facing;
    int &offX;
    int &offY;
    int &barBusy;
    int &barCategory;
    const gp::Settings &settings;
    bool &giveMode;
    int &giveLocation;
    std::map<int, int> &giveTheirs;
    int &roomMenuCategory;
    std::unique_ptr<RoomView> &room;
    std::function<bool(int)> &roomActivity;
    std::function<void()> &roomQuit;
    int kFrameRate;
    std::function<void()> &saveGame;
    std::function<AnimRef(const gp::Id &)> animRefById;
    std::function<AnimRef(int)> getAnim;
    std::function<gp::Id(int, int)> reactAnimId;
    std::function<gp::Id(int, int)> uiIdP;
    std::function<void(std::function<void()>)> requestScreen;
    std::function<void(int, int)> petSound;
    std::function<void(int, float, float, bool, float, float)> drawShadow;
    std::function<void()> chewSound;
    std::function<std::vector<int>(int)> buildFiltered;
    std::function<float(int, int)> smoothAt;
    std::function<int()> restIdleId;
    std::function<int()> restIdleRate;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    std::function<void()> importPetAnims;
};

class RoomController : public RoomEnv
{
public:
    explicit RoomController(RoomEnv env) : RoomEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }
    std::function<void(int, int)> sfxFn;
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, std::move(next)); }

    // ---- player homes and hotels (EnterActivityAndRunLoop 0x04ab19); see room_scene.hpp ----
    // Phase 1: the room's walls and floor, the pet walking in and out and along the floor with the indoor art, and the
    // door back to the street. Furniture, the house bar and the room's activities come later.
    // The 32 furnishing slots: a hotel's preset, the player's own equipment in their home, or another save slot's
    // (an unused slot's house is empty: "FOR RENT").
    auto roomSlots(int location)
    {
        gp::SlotItems slots{};
        if (gp::isHotel(location))
        {
            slots = gp::hotelSlotItems(rom, location);
            if (const gp::RoomDef *rd = db.room(location)) // a mod's preset changes
                for (const auto &[s, id] : rd->slots)
                    if (s >= 0 && s < gp::kInventorySlots)
                        slots[size_t(s)] = id.empty() ? 0 : db.item(id) ? db.item(id)->index : 0;
        }
        else
        {
            std::map<int, int> equip;
            if (location == slot)
                equip = player.equip;
            else if (gp::SaveData other; gp::loadFromFile(gp::slotPath(location), other) && gp::peekSlot(location).full)
                equip = other.has("equip") ? other.getMap("equip") : gp::starterEquip(other.getInt("species"));
            for (const auto &[s, item] : equip)
                if (s >= 0 && s < gp::kInventorySlots)
                    slots[size_t(s)] = item;
        }
        return slots;
    }

    // Rebuilds what the room shows from its slots: the wall and floor themes (slots 0 and 1) and the sprite layers.
    auto refreshRoom(RoomView &r)
    {
        auto themeOf = [&](int s)
        {
            const int item = r.slots[size_t(s)];
            return item > 0 && item < int(itemDefs.size()) ? itemDefs[size_t(item)].w9 : 0;
        };
        const gp::RoomTheme th = gp::roomTheme(rom, themeOf(gp::kInventoryWallSlot), themeOf(gp::kInventoryFloorSlot));
        auto artOf = [&](int s) -> const gp::RoomArt *
        {
            const int item = r.slots[size_t(s)];
            return item > 0 && item < int(itemDefs.size()) ? db.roomArt(itemDefs[size_t(item)].id) : nullptr; // a mod's wall or floor picture
        };
        const gp::Image img = gp::renderRoom(rom, db, th, artOf(gp::kInventoryWallSlot), artOf(gp::kInventoryFloorSlot));
        if (r.tex)
            SDL_DestroyTexture(r.tex);
        r.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
        SDL_UpdateTexture(r.tex, nullptr, img.rgba.data(), img.w * 4);
        SDL_SetTextureBlendMode(r.tex, SDL_BLENDMODE_BLEND);
        r.sprites = gp::roomSprites(rom, gp::roomDecorations(rom, r.location, db.room(r.location)), r.slots, itemDefs);
    }

    auto beginRoom(int location)
    {
        auto r = std::make_unique<RoomView>();
        r->location = location;
        r->slots = roomSlots(location);
        r->table = gp::roomDecorations(rom, location, db.room(location));
        // Entering a home that isn't yours: its pet, in the colour saved there, or "FOR RENT" when the slot is unused.
        if (location >= 0 && location <= 2 && location != slot)
        {
            const gp::SlotInfo other = gp::peekSlot(location);
            r->forRent = !other.full;
            if (other.full)
            {
                const gp::SpeciesDef *sd = db.speciesByLegacy(other.species);
                if (sd)
                {
                    r->hasVisitor = true;
                    r->visitSpecies = other.species;
                    r->visitPal = other.appearance >= 0 ? other.appearance : sd->defaultPalette;
                    if (!other.appearanceId.empty()) // the visited pet wears a mod's colour
                        if (const gp::PaletteDef *pd = db.palette(other.appearanceId); pd && gp::modPalettes().count(pd->number))
                            r->visitPal = pd->number;
                    try
                    {
                        gp::importAnim(rom, db, gp::artOf(other.species).settledStyle, r->visitPal);
                    }
                    catch (const std::exception &)
                    {
                    }
                }
            }
        }
        refreshRoom(*r);
        r->scroll = gp::roomScroll(0, r->x, gp::kRoomWidthTiles, W);
        room = std::move(r);
        std::printf("room: location %d\n", location);
    }

    auto enterRoom(int location)
    {
        if (!gp::isRoomLocation(location) || room)
            return;
        // the engine system "room.enter": the pet goes through a home or hotel door (kind: "home" = its own, "visit" = another save slot's, "hotel"); cancel or replace and it stays outside
        gp::SystemCtx ec;
        ec.set("location", location).set("kind", gp::isHotel(location) ? "hotel" : location == slot ? "home" : "visit");
        scripts.system("room.enter", ec, [&]
                       {
            sfx(gp::kRoomEnterSound); // PlaySoundEffect(0x13, 200)
            requestScreen([&, location]
                          { beginRoom(location); }); });
    }

    auto leaveRoom()
    {
        const gp::RoomExitPoint p = gp::roomReturnPoint(room->location, db.room(room->location)); // back at the door, by return state
        // the engine system "room.leave": where the pet comes out in the area (change x, y: it cannot be cancelled, the room is already over)
        gp::SystemCtx lc;
        lc.set("location", room->location).set("x", p.x).set("y", p.y);
        scripts.system("room.leave", lc, [] {});
        room.reset();
        pos = {int(lc.num("x")), int(lc.num("y"))};
        prevPos = pos;
        facing = 1;
        walkTicks = 0;
        playAnim(restIdleId(), true, restIdleRate(), Pose::Idle);
    }

    // ---- eating at the food bowl: see the comment in room_scene.hpp ----
    // The food in the bowl is table entry 3's sprite; it changes while the pet eats.
    auto setBowl(RoomView &r, int sprite)
    {
        if (sprite < 0 || int(r.table.size()) <= gp::kBowlEntry || r.table[gp::kBowlEntry].sprite == sprite)
            return;
        r.table[gp::kBowlEntry].sprite = sprite;
        r.sprites = gp::roomSprites(rom, r.table, r.slots, itemDefs);
    }

    // fromBowl: Z at the bowl, which gives item 0x1c's effect. From the item screen the chosen food has already had its
    // own effect (the ROM's menu result 2), and the bowl is filled and eaten from all the same.
    auto startEating(RoomView &r, bool fromBowl)
    {
        if (fromBowl)
            gp::applyFoodEffect(player, itemDefs[gp::kBowlFoodItem], itemHost); // ApplyItemCategoryEffect(0x1c), at once
        setBowl(r, gp::kBowlFull);
        r.phase = RoomView::Phase::Eating;
        r.eatStep = 6;
        r.eatSX = gp::bowlApproachX(sp, r.y);
        r.eatGivesHappy = true;
        r.eatWash = false;
        r.animTicks = 0;
    }

    // Washing (HandlePetWashInteraction, pet_wash.hpp) in a room: the bubble at the pet, then +100 cleanliness and the
    // original colour back, then the indoor happy hop three times and the mood top-up.
    auto startRoomWash(RoomView &r)
    {
        r.phase = RoomView::Phase::Eating;
        r.eatStep = 20;
        r.eatGivesHappy = false;
        r.eatWash = true;
        r.eatAnim = gp::artOf(sp).idleStyle; // front-on, any facing
        r.eatFlip = false;
        r.eatLoop = true;
        r.animTicks = 0;
        r.overId = gp::kWashBubbleAnim;
        r.overTicks = 0;
    }

    // Sleeping (activity 5 from the bed or the bar; activity 0x25 when quitting): see room_scene.hpp.
    auto startSleep(RoomView &r, bool quitting)
    {
        r.phase = RoomView::Phase::Eating;
        r.eatStep = 30;
        r.eatGivesHappy = false;
        r.eatWash = false;
        r.sleepQuit = quitting;
        r.eatAnim = gp::artOf(sp).boredStyle;
        r.eatFlip = false;
        r.eatLoop = false;
        r.eatFps = 7; // PlayBoredReactionAnim plays at 7
        r.animTicks = 0;
        petSound(8, 0xa0); // the yawn: the bored reaction's sound
    }

    auto roomQuitRun()
    { startSleep(*room, true); }

    auto roomActivityRun(int id)
    {
        if (id != 5 && id != gp::kWashActivity)
            return false;
        // the engine system "room.activity": a button of the house bar that starts a sequence (activity "sleep" or "wash"); cancel or replace and nothing starts (a mod may do its own)
        gp::SystemCtx c;
        c.set("location", room->location).set("activity", id == 5 ? "sleep" : "wash");
        scripts.system("room.activity", c, [&]
                       {
            if (id == 5) // the bar's sleep button
                startSleep(*room, false);
            else
                startRoomWash(*room); });
        return true;
    }

    // A cure (HandleCureIllnessEncounter 0x015ed8, activity 0x28): where the pet stands, the bubbles over it and the
    // indoor happy hop three times. The Happiness bonus is the bowl's only. A medicine that doesn't match does nothing.
    auto startRoomMedicine(RoomView &r, int curedIllness)
    {
        r.phase = RoomView::Phase::Eating;
        r.eatStep = 17; // the hop, with the panda's intro and outro
        r.eatGivesHappy = false;
        r.eatWash = false;
        r.eatAnim = gp::artOf(sp).idleStyle; // held for the one step before the hop starts
        r.eatFlip = false;
        r.eatLoop = true;
        r.eatFps = 14;
        r.eatReverse = false;
        r.animTicks = 0;
        r.overId = 137 + (curedIllness % 13); // the same bubble animation as outdoors
        r.overTicks = 0;
    }

    // The first-time messages (settings.hpp). Standing next to the bed or the bowl is enough: the pet stops, the event is
    // marked shown (saved with the player), and 0.25 s later the message opens.
    auto tutorialBegin(RoomView &r)
    {
        player.tutorialsShown = gp::markShown(player.tutorialsShown, r.tutorialEvent);
        saveGame();
        r.eatAnim = gp::artOf(sp).idleStyle; // front-on
        r.eatFlip = false;
        r.eatLoop = true;
        r.eatFps = 14;
        r.eatReverse = false;
        r.animTicks = 0;
        r.eatStep = 41;
    }

    auto startTutorial(RoomView &r, int event)
    {
        const bool turn = r.phase == RoomView::Phase::Walking || r.phase == RoomView::Phase::Settling;
        r.phase = RoomView::Phase::Eating;
        r.tutorialEvent = event;
        r.eatGivesHappy = false;
        r.eatWash = false;
        r.sleepQuit = false;
        r.overId = -1;
        if (turn) // it was walking: it settles to face the front first
        {
            r.eatAnim = gp::artOf(sp).turn90Style;
            r.eatFlip = !r.faceLeft; // the art faces left
            r.eatLoop = false;
            r.eatFps = 14;
            r.eatReverse = false;
            r.animTicks = 0;
            r.eatStep = 40;
        }
        else
            tutorialBegin(r);
    }

    // Called every tick the pet is free: HandlePetStageEvents starts the message the first time it is by the bed or bowl.
    auto roomTutorial()
    {
        RoomView &r = *room;
        using P = RoomView::Phase;
        if (r.phase != P::Idle && r.phase != P::Walking && r.phase != P::Settling && r.phase != P::Turning)
            return;
        const int event = r.nearby == gp::kDecorBed ? gp::kTutorialBed : r.nearby == gp::kDecorBowl ? gp::kTutorialBowl : -1;
        if (event >= 0 && gp::isFirstTime(settings.tutorials, player.tutorialsShown, event))
            startTutorial(r, event);
    }

    auto tickEat(RoomView &r)
    {
        const gp::BowlTarget t = gp::bowlTarget(sp);
        auto done = [&](int id) // a one-shot animation has played to its end
        {
            const int n = int(getAnim(id).size());
            return n == 0 || r.animTicks * r.eatFps / 30 >= n;
        };
        auto play = [&](int id, bool flip, bool loop, int fps = 14) // 14 is kFrameRate
        {
            r.eatAnim = id;
            r.eatFlip = flip;
            r.eatLoop = loop;
            r.eatFps = fps;
            r.eatReverse = false;
            r.animTicks = 0;
        };
        // The end of every happy hop (bowl, cure, wash, bed): the Happiness top-ups, save, and back to normal.
        auto finishHappy = [&]
        {
            if (r.eatGivesHappy && player.care[gp::Happiness] < gp::kHappinessCap)
                player.adjustCare(gp::Happiness, gp::kBowlHappiness);
            if (r.eatWash)
                gp::applyWashMood(player);
            saveGame();
            r.phase = RoomView::Phase::Idle;
            r.eatStep = 0;
            r.animTicks = 0;
            r.overId = -1;
        };
        auto walk = [&] // the sideways walk, mirrored for left
        {
            if (r.eatAnim != gp::artOf(sp).walkSideStyle || r.eatFlip != r.faceLeft)
                play(gp::artOf(sp).walkSideStyle, r.faceLeft, true);
        };
        // The bite, chew and action art is played unmirrored: the dog's and monkey's faces left (the bowl is on their left)
        // and the panda's faces right (it stands to the bowl's left).
        auto bite = [&]
        {
            play(gp::artOf(sp).bowlBite, false, false);
            r.eatStep = 9;
        };
        ++r.animTicks;
        switch (r.eatStep)
        {
        case 6: // along the floor to the foot of the diagonal
            if (r.x == r.eatSX)
            {
                r.eatStep = 7;
                break;
            }
            r.faceLeft = r.eatSX < r.x;
            walk();
            r.x += std::clamp(r.eatSX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
            break;
        case 7: // up the diagonal to the bowl
            if (r.y > t.y)
            {
                r.faceLeft = t.x < r.x;
                walk();
                r.x += std::clamp(t.x - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                r.y -= 1;
                break;
            }
            r.x = t.x;
            r.y = t.y;
            r.faceLeft = gp::artOf(sp).bowlSlope > 0; // the bowl is to the left of everyone but the panda
            r.eatRepeat = 2 + std::rand() % 2; // RandRange(2,4): 2 or 3 bites
            if (gp::artOf(sp).bowlStartsWithAction) // the panda starts with an action animation
            {
                play(gp::artOf(sp).actionStyle, false, false);
                r.eatStep = 8;
            }
            else
                bite();
            break;
        case 8:
            if (done(gp::artOf(sp).actionStyle))
                bite();
            break;
        case 9: // lean in; the food drops as the bites go
            setBowl(r, gp::bowlSpriteForBites(r.eatRepeat));
            if (done(gp::artOf(sp).bowlBite))
            {
                --r.eatRepeat;
                r.eatChews = 2 + std::rand() % 3; // RandRange(2,5): 2..4 chews
                play(gp::artOf(sp).eatStyle, false, false);
                chewSound(); // GUESS: the bowl's chews sound like the outdoor ones
                r.eatStep = 10;
            }
            break;
        case 10: // chew, then a short pause holding the last frame
            if (done(gp::artOf(sp).eatStyle))
            {
                if (--r.eatChews > 0)
                {
                    play(gp::artOf(sp).eatStyle, false, false);
                    chewSound();
                }
                else
                {
                    r.eatPause = gp::bowlPauseTicks(1 + std::rand() % 3); // RandRange(1,4) / 20 seconds
                    r.eatStep = 11;
                }
            }
            break;
        case 11:
            if (--r.eatPause > 0)
                break;
            if (r.eatRepeat < 1)
            {
                r.eatSX = (gp::kRoomFloorY - r.y) * gp::artOf(sp).bowlSlope + r.x; // back to the floor
                r.eatStep = 12;
            }
            else
                bite();
            break;
        case 12: // down the diagonal
            if (r.y < gp::kRoomFloorY)
            {
                r.faceLeft = r.eatSX < r.x;
                walk();
                r.x += std::clamp(r.eatSX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                r.y += 1;
                break;
            }
            r.x = r.eatSX;
            r.y = gp::kRoomFloorY;
            play(gp::artOf(sp).turn90Style, !r.faceLeft, false); // art faces left, so mirrored for right
            r.eatStep = 13;
            break;
        case 13: // turn to the front
            if (done(gp::artOf(sp).turn90Style))
                r.eatStep = 17;
            break;
        case 40: // a first-time message: the pet turns to the front first (TickActor_SettleFacing)
            if (done(gp::artOf(sp).turn90Style))
                tutorialBegin(r);
            break;
        case 41: // marked shown and idle: after 0.25 s the message opens (TickFirstNPCEncounterSequence)
            if (r.animTicks >= gp::kTutorialWaitTicks)
            {
                const int event = r.tutorialEvent;
                r.phase = RoomView::Phase::Idle;
                r.eatStep = 0;
                r.animTicks = 0;
                r.tutorialEvent = -1;
                dialogue.show(gp::messageBox(db.textOr("base:text/" + std::to_string(gp::tutorialText(event)))));
            }
            break;
        case 30: // the yawn (the bored reaction), then off to the bed
            if (done(gp::artOf(sp).boredStyle))
            {
                r.eatSX = gp::bedApproachX(r.y);
                r.eatStep = 31;
            }
            break;
        case 31: // along the floor to the foot of the diagonal
            if (r.x == r.eatSX)
            {
                r.eatStep = 32;
                break;
            }
            r.faceLeft = r.eatSX < r.x;
            walk();
            r.x += std::clamp(r.eatSX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
            break;
        case 32: // up the diagonal to the bed
            if (r.y > gp::kBedY)
            {
                r.faceLeft = gp::kBedX < r.x;
                walk();
                r.x += std::clamp(gp::kBedX - r.x, -gp::kRoomStepPx, gp::kRoomStepPx);
                r.y -= 1;
                break;
            }
            r.x = gp::kBedX;
            r.y = gp::kBedY;
            play(gp::artOf(sp).bedGetIn, false, false);
            r.eatStep = 33;
            break;
        case 33:
            if (done(gp::artOf(sp).bedGetIn))
            {
                r.eatRepeat = gp::kSleepCycles;
                play(gp::artOf(sp).sleepCycle, false, false, gp::sleepCycleFps(sp));
                sfx(6, 0xbe); // the sleeping sound: sound 6 (browser #11); GUESS: once per cycle
                r.eatStep = 34;
            }
            break;
        case 34: // sleep cycles: +10 Tiredness each (not when quitting), on until 3 are done and the pet is rested
            if (done(gp::artOf(sp).sleepCycle))
            {
                if (!r.sleepQuit)
                    player.adjustCare(gp::Tiredness, gp::kSleepTiredness);
                --r.eatRepeat;
                if (!gp::sleepOver(r.eatRepeat, player.care[gp::Tiredness], r.sleepQuit))
                {
                    play(gp::artOf(sp).sleepCycle, false, false, gp::sleepCycleFps(sp));
                    sfx(6, 0xbe);
                }
                else if (r.sleepQuit) // the ROM halts here; the game saves and goes back to the menu
                {
                    saveGame();
                    S.flow.backToMenu = true;
                    S.flow.running = false;
                }
                else
                {
                    play(gp::artOf(sp).bedGetOut, false, false);
                    r.eatStep = 35;
                }
            }
            break;
        case 35: // out of bed, back down the diagonal (step 12), turn to the front (13) and the happy hop (14)
            if (done(gp::artOf(sp).bedGetOut))
            {
                r.eatSX = (gp::kRoomFloorY - r.y) * -2 + r.x;
                r.eatWash = true; // the same +10 Happiness top-up as after a wash
                r.eatStep = 12;
            }
            break;
        case 20: // the wash bubble plays once at the pet; then the wash itself
        {
            const int frames = int(animRefById(reactAnimId(gp::kWashBubbleAnim, 0x28c)).size());
            if (r.overTicks < gp::bubbleTicks(frames))
                break;
            r.overId = -1;
            gp::applyWash(player); // +100 hygiene, original colour back
            const int pal = gp::washedPalette(player, petPal);
            if (pal != petPal) // the potion colour comes off
            {
                petPal = pal;
                importPetAnims();
            }
            saveGame();
            r.eatStep = 17;
            break;
        }
        case 17: // PlayPositiveReactionAnim(pet, 3, 2) begins: the panda's intro first, the others go straight to the hop
            r.eatHappy = 0;
            if (gp::artOf(sp).hopIntro >= 0)
            {
                play(gp::artOf(sp).hopIntro, false, false);
                r.eatStep = 15;
            }
            else
            {
                play(gp::artOf(sp).positiveStyle, false, false), petSound(4, 0xbe); // PlayPositiveReactionAnim: the happy panting
                r.eatStep = 14;
            }
            break;
        case 15: // the panda's intro is over
            if (done(gp::artOf(sp).hopIntro))
            {
                play(gp::artOf(sp).positiveStyle, false, false), petSound(4, 0xbe); // PlayPositiveReactionAnim: the happy panting
                r.eatStep = 14;
            }
            break;
        case 14: // the hop, three times
            if (done(r.eatAnim))
            {
                if (++r.eatHappy < 3)
                    play(r.eatAnim, false, false), petSound(4, 0xbe);
                else if (gp::artOf(sp).hopIntro >= 0) // then the panda's outro: its intro played last frame to first
                {
                    play(gp::artOf(sp).hopIntro, false, false);
                    r.eatReverse = true;
                    r.eatStep = 16;
                }
                else
                    finishHappy();
            }
            break;
        case 16:
            if (done(gp::kPandaFrameStyle))
                finishHappy();
            break;
        }
    }

    // Put the item in the slot (SetInventorySlot) and show the room with it. Only the player's own home has equipment to
    // change; it is saved with the player.
    auto equipSlot(RoomView &r, int s, int item)
    {
        r.slots[size_t(s)] = item;
        if (item)
            player.equip[s] = item;
        else
            player.equip.erase(s);
        saveGame();
        refreshRoom(r);
    }

    // Back to the item screen, on the tab it was left on (while the screen is black, from requestScreen).
    // How many of each item the pet in this home has (LoadHeldItemQuantitiesFromSave(slot, 1)).
    auto loadTheirItems(int location)
    {
        giveTheirs.clear();
        gp::SaveData s;
        if (!gp::loadFromFile(gp::slotPath(location), s))
            return;
        if (s.has("itemIds"))
        {
            const std::map<std::string, int> ids = s.getIdMap("itemIds");
            for (const gp::ItemDef &d : itemDefs)
                if (auto it = ids.find(d.id); it != ids.end())
                    giveTheirs[d.index] = it->second;
        }
        else
            for (const auto &[n, c] : s.getMap("inventory"))
                giveTheirs[n] = c;
    }

    // Z on the visiting pet: the item screen in give mode (HandlePetStageEvents sets the item menu mode to 2).
    auto startGive(RoomView &r)
    {
        giveMode = true;
        giveLocation = r.location;
        loadTheirItems(r.location);
        requestScreen([&]
                      {
            im = {};
            im.open = true;
            im.category = barCategory;
            im.filtered = buildFiltered(im.category);
            std::printf("give: %zu items they have room for\n", im.filtered.size()); });
    }

    // The room stays in its placing view until the screen is black, so the pet never shows during the fade out.
    auto reopenItemScreen()
    {
        if (room)
        {
            room->phase = RoomView::Phase::Idle;
            room->animTicks = 0;
        }
        im = {};
        im.open = true;
        im.category = roomMenuCategory;
        im.filtered = buildFiltered(im.category);
    }

    auto tickRoom(uint8_t dirs, uint8_t newly, bool zPressed, bool xPressed)
    {
        RoomView &r = *room;
        using P = RoomView::Phase;
        constexpr int kSettleTicks = 5; // the walk keeps playing for 0.15 s after the keys are released
        // The diagonal in and out of the door meets the floor line here (StartPetActivity(3) and Pet_WalkOutAndBack).
        const int floorX = gp::kRoomDoorX + (gp::kRoomFloorY - gp::kRoomDoorY) * 2;
        ++r.visitTicks; // the visiting pet's idle keeps playing whatever the player is doing
        if (r.phase == P::Placed) // the item is down: hold the room, then fade back to the inventory
        {
            if (++r.animTicks >= gp::kPlacedHoldTicks)
                requestScreen([this] { reopenItemScreen(); }); // the phase goes back to Idle once the screen is black
            return;
        }
        if (r.phase == P::Placing) // UpdatePetWalkToItem / HandleMapNavigationInput
        {
            if (xPressed) // cancel: the ROM switches state straight back to the item screen
            {
                requestScreen([this] { reopenItemScreen(); });
                return;
            }
            if (!r.placeConfirmed)
            {
                int to = r.placeSlot;
                if (newly & gp::DirLeft)
                    to = gp::neighbourSlot(rom, to, gp::SlotDir::Left);
                else if (newly & gp::DirRight)
                    to = gp::neighbourSlot(rom, to, gp::SlotDir::Right);
                else if (newly & gp::DirUp)
                    to = gp::neighbourSlot(rom, to, gp::SlotDir::Up);
                else if (newly & gp::DirDown)
                    to = gp::neighbourSlot(rom, to, gp::SlotDir::Down);
                r.placeSlot = to >= 0 && to < gp::kInventorySlots ? to : r.placeSlot;
                if (zPressed)
                {
                    sfx(gp::kPlaceSound);
                    r.placeConfirmed = true;
                }
            }
            // The marker flies to the slot at 4 px per tick on each axis.
            const gp::SlotPos g = gp::slotPosition(rom, r.placeSlot);
            r.curX += std::clamp(g.x - r.curX, -gp::kPlaceFlightStep, gp::kPlaceFlightStep);
            r.curY += std::clamp(g.y - r.curY, -gp::kPlaceFlightStep, gp::kPlaceFlightStep);
            // The background scrolls to keep the marker near the middle of the screen.
            const int wantScroll = std::clamp(r.curX - 0x9f - (W - 320) / 2, 0, gp::kRoomWidthTiles * gp::kRoomTilePx - W);
            r.scroll += std::clamp(wantScroll - r.scroll, -gp::kPlaceFlightStep, gp::kPlaceFlightStep);
            if (r.placeConfirmed && r.curX == g.x && r.curY == g.y)
            {
                equipSlot(r, r.placeSlot, r.placeItem);
                r.phase = P::Placed; // SetInventorySlot, then back to the item screen after a moment
                r.animTicks = 0;
            }
            return;
        }
        if (r.phase == P::Eating)
        {
            if (r.overId >= 0)
                ++r.overTicks;
            tickEat(r);
            r.nearby = -1;
            r.scroll = gp::roomScroll(r.scroll, r.x, gp::kRoomWidthTiles, W);
            return;
        }
        if (r.phase == P::Entering)
        {
            r.faceLeft = false;
            ++r.walkTicks;
            r.x += gp::kRoomStepPx;
            r.y += 1;
            if (r.y >= gp::kRoomFloorY)
            {
                r.x = floorX;
                r.y = gp::kRoomFloorY;
                r.phase = P::Idle;
                r.animTicks = 0;
            }
        }
        else if (r.phase == P::Leaving)
        {
            ++r.walkTicks;
            if (r.y == gp::kRoomFloorY && r.x != floorX) // along the floor to the foot of the diagonal
            {
                const int d = floorX - r.x;
                r.faceLeft = d < 0;
                r.x += std::clamp(d, -gp::kRoomStepPx, gp::kRoomStepPx);
            }
            else // then up and across to the door
            {
                r.faceLeft = true;
                r.x -= gp::kRoomStepPx;
                r.y -= 1;
                if (r.y <= gp::kRoomDoorY)
                {
                    r.x = gp::kRoomDoorX;
                    r.y = gp::kRoomDoorY;
                    sfx(gp::kRoomEnterSound);
                    requestScreen([this] { leaveRoom(); });
                }
            }
        }
        else
        {
            const bool left = (dirs & gp::DirLeft) != 0, right = (dirs & gp::DirRight) != 0;
            if (left || right) // up and down do nothing here; left wins when both are held
            {
                r.faceLeft = left;
                r.x = gp::roomClampX(r.x + (left ? -gp::kRoomStepPx : gp::kRoomStepPx));
                if (r.phase != P::Walking)
                    r.walkTicks = 0; // a new walk starts on frame 0
                r.phase = P::Walking;
                ++r.walkTicks;
            }
            else if (r.phase == P::Walking)
            {
                r.phase = P::Settling;
                r.settleTicks = 0;
            }
            if (r.phase == P::Settling)
            {
                ++r.walkTicks;
                if (++r.settleTicks >= kSettleTicks)
                {
                    r.phase = P::Turning;
                    r.animTicks = 0;
                }
            }
            else if (r.phase == P::Turning)
            {
                const int n = int(getAnim(gp::artOf(sp).turn90Style).size());
                if (n == 0 || ++r.animTicks * kFrameRate / 30 >= n)
                {
                    r.phase = P::Idle;
                    r.animTicks = 0;
                }
            }
            else if (r.phase == P::Idle)
                ++r.animTicks;
            // Z next to something (the engine system "room.interact": target "door", "bowl", "visitor" or "bed"; cancel, or replace, and nothing happens, or the mod does its own thing)
            const char *target = !zPressed || r.phase == P::Walking ? nullptr
                                 : r.nearby == gp::kDecorDoor      ? "door"
                                 : r.nearby == gp::kDecorBowl      ? "bowl"
                                 : r.nearby == gp::kDecorVisitor   ? "visitor"
                                 : r.nearby == gp::kDecorBed       ? "bed"
                                                                   : nullptr;
            if (target)
            {
                gp::SystemCtx ic;
                ic.set("location", r.location).set("target", target).set("x", r.x);
                bool ran = false;
                scripts.system("room.interact", ic, [&]
                               {
                    ran = true;
                    if (r.nearby == gp::kDecorDoor) // Z at the door walks the pet out (HandlePetStageEvents: the nearest decoration is the door -> activity 4)
                    {
                        r.phase = P::Leaving;
                        r.walkTicks = 0;
                    }
                    else if (r.nearby == gp::kDecorBowl) // Z at the food bowl: eat (ApplyItemCategoryEffect(0x1c), then activity 6)
                        startEating(r, true);
                    else if (r.nearby == gp::kDecorVisitor) // Z on the visiting pet opens the item screen in mode 2 to give it items
                        startGive(r);
                    else if (r.nearby == gp::kDecorBed) // Z at the bed: sleep (activity 5)
                        startSleep(r, false); });
                if (ran && r.nearby != gp::kDecorDoor)
                    return;
            }
        }
        // The decoration next to the pet, for the selector arrow and for Z (FindDecorationNearPetX). Only while the pet is
        // free to act: not walking in or out, and not while something else has the buttons.
        const bool free = r.phase == P::Idle || r.phase == P::Walking || r.phase == P::Settling || r.phase == P::Turning;
        r.nearby = free ? gp::nearbyDecoration(r.table, r.x,
                                               [&](const gp::RoomDecoration &e)
                                               {
                                                   const AnimRef D = animRefById(uiIdP(e.sprite, e.palette));
                                                   return D.size() ? D.anim->frames[0].anchorX - D.anim->frames[0].dx : 0;
                                               })
                       : -1;
        if (free)
            r.nearby = gp::visitRules(r.nearby, r.x, r.hasVisitor, r.forRent); // the visiting pet; a house for rent has only the door
        r.scroll = gp::roomScroll(r.scroll, r.x, gp::kRoomWidthTiles, W);
    }

    // The room, then the pet in it (the indoor "style" art: no up or down walk, the front idle for any facing).
    auto drawRoom()
    {
        RoomView &r = *room;
        using P = RoomView::Phase;
        // smooth mode glides the panning and the pet's walk between the 30 Hz steps (a jump of more than 16 px never glides)
        const float scrollF = smoothAt(r.pscroll, r.scroll), rx = smoothAt(r.px, r.x), ry = smoothAt(r.py, r.y);
        gp::clearFrame(ren);
        const SDL_FRect dst{-scrollF, 0.f, float(gp::kRoomWidthTiles * gp::kRoomTilePx),
                            float(gp::kRoomHeightTiles * gp::kRoomTilePx)};
        SDL_RenderCopyF(ren, r.tex, nullptr, &dst);
        auto drawLayer = [&](const std::vector<gp::RoomSprite> &layer)
        {
            for (const gp::RoomSprite &d : layer)
            {
                const AnimRef D = animRefById(uiIdP(d.sprite, d.palette));
                if (D.size() == 0)
                    continue;
                const gp::SpriteFrame &s = D.anim->frames[0];
                SDL_FRect rc{float(d.x) - scrollF - s.anchorX + s.dx, float(d.y) - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
                SDL_RenderCopyF(ren, (*D.tex)[0], nullptr, &rc);
            }
        };
        drawLayer(r.sprites.behind); // the order EnterActivityAndRunLoop draws in
        drawLayer(r.sprites.items);
        // The flag-0 pieces are split by the pet's y every frame: those at or above its feet go behind it, those below
        // are drawn over it (the panda stands at y 0xa1, above the bowl food at 0xad, so the bowl is in front of it).
        auto petSide = [&](bool inFront)
        {
            std::vector<gp::RoomSprite> v;
            for (const gp::RoomSprite &s : r.sprites.beforePet)
                if ((s.y > r.y) == inFront)
                    v.push_back(s);
            return v;
        };
        if (r.phase == P::Placing || r.phase == P::Placed)
            drawLayer(r.sprites.beforePet);
        else
            drawLayer(petSide(false));

        if (r.phase == P::Placed) // the finished room, no pet and no markers
        {
            drawLayer(r.sprites.afterPet);
            return;
        }
        if (r.phase == P::Placing) // RenderPetWalkToItem: a marker on every slot that takes the item, and the cursor
        {
            drawLayer(r.sprites.afterPet);
            const int type = r.placeItem > 0 && r.placeItem < int(itemDefs.size()) ? itemDefs[size_t(r.placeItem)].type : 0;
            const int markerSprite = type == 6 ? 0x319 : type == 5 ? 0x3b0 : 0x33d;
            const int dy = type == 6 ? 0x14 : 0;
            for (int s = 0; s < gp::kInventorySlots; ++s)
                if (gp::slotCategory(rom, s) == type)
                {
                    const gp::SlotPos p = gp::slotPosition(rom, s);
                    drawLayer({{p.x, p.y + dy, markerSprite, 0x28c}});
                }
            if (const AnimRef C = animRefById(reactAnimId(gp::kMarkerIndoorItemAnim, gp::kMarkerPalette)); C.size() > 0)
            {
                const gp::SpriteFrame &s = C.anim->frames[size_t(gp::markerFrame(gameTicks, int(C.size())))];
                SDL_FRect rc{smoothAt(r.pcurX, r.curX) - scrollF - s.anchorX + s.dx, smoothAt(r.pcurY, r.curY) + float(dy) - s.anchorY + s.dy + 1, float(s.w),
                             float(s.h)};
                SDL_RenderCopyF(ren, (*C.tex)[size_t(gp::markerFrame(gameTicks, int(C.size())))], nullptr, &rc);
            }
            return;
        }

        // The pet whose home this is, in its settled-idle pose (drawn just before the player's pet, like the ROM does).
        if (r.hasVisitor)
        {
            const gp::AnimChoice ac = gp::settledIdle(r.visitSpecies, true);
            const AnimRef V = animRefById(reactAnimId(ac.id, r.visitPal));
            if (V.size() > 0)
            {
                const int f = (r.visitTicks * ac.rate / 30) % int(V.size());
                const gp::SpriteFrame &s = V.anim->frames[size_t(f)];
                drawShadow(s.w, float(gp::kVisitorX), float(gp::kVisitorY), r.visitSpecies == 2, scrollF, 0.f);
                SDL_FRect rc{float(gp::kVisitorX) - scrollF - s.anchorX + s.dx + offX, float(gp::kVisitorY) - s.anchorY + s.dy + offY,
                             float(s.w), float(s.h)};
                SDL_RenderCopyF(ren, (*V.tex)[size_t(f)], nullptr, &rc);
            }
        }

        const bool walking = r.phase == P::Entering || r.phase == P::Walking || r.phase == P::Settling || r.phase == P::Leaving;
        int id = gp::artOf(sp).idleStyle;
        bool flip = false;
        int frame = 0;
        auto frameOf = [&](int anim, int ticks, bool loop, int rate = 14) // 14 is kFrameRate
        {
            const int n = int(getAnim(anim).size());
            if (n <= 0)
                return 0;
            const int k = ticks * rate / 30;
            return loop ? k % n : std::min(k, n - 1);
        };
        if (walking)
        {
            id = gp::artOf(sp).walkSideStyle; // art faces right: mirrored for left
            flip = r.faceLeft;
            frame = frameOf(id, r.walkTicks, true);
        }
        else if (r.phase == P::Turning)
        {
            id = gp::artOf(sp).turn90Style; // art faces left: mirrored for right
            flip = !r.faceLeft;
            frame = frameOf(id, r.animTicks, false);
        }
        else if (r.phase == P::Eating && r.eatAnim >= 0)
        {
            id = r.eatAnim;
            flip = r.eatFlip;
            frame = frameOf(id, r.animTicks, r.eatLoop, r.eatFps);
            if (r.eatReverse) // last frame to first
                frame = std::max(0, int(getAnim(id).size()) - 1 - frame);
        }
        else
            frame = frameOf(id, r.animTicks, true);
        if (const AnimRef A = getAnim(id); A.size() > 0)
        {
            const int n = int(A.size());
            const int f = std::min(frame, n - 1);
            const gp::SpriteFrame &s = A.anim->frames[size_t(f)];
            drawShadow(s.w, rx, ry, gp::artOf(sp).wideShadow, scrollF, 0.f);
            const float px = rx - scrollF;
            float x = flip ? px + s.anchorX - s.dx - s.w : px - s.anchorX + s.dx;
            float y = ry - s.anchorY + s.dy;
            x += offX;
            y += offY;
            SDL_FRect rc{x, y, float(s.w), float(s.h)};
            SDL_RenderCopyExF(ren, (*A.tex)[size_t(f)], nullptr, &rc, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
        if (r.overId >= 0) // the cure's bubbles over the pet
        {
            const AnimRef A = animRefById("base:anim/" + std::to_string(r.overId) + "@28c");
            if (A.size())
            {
                const int fps = r.overId == gp::kWashBubbleAnim ? gp::kWashBubbleFps : kBubbleFps;
                const int f = std::min(r.overTicks * fps / 30, int(A.size()) - 1);
                const gp::SpriteFrame &s = A.anim->frames[size_t(f)];
                SDL_FRect rc{rx - scrollF - s.anchorX + s.dx, ry - s.anchorY + s.dy, float(s.w), float(s.h)};
                SDL_RenderCopyF(ren, (*A.tex)[size_t(f)], nullptr, &rc);
            }
        }
        drawLayer(petSide(true));
        drawLayer(r.sprites.afterPet);

        // The selector arrow over the decoration the pet is next to (anim 0x11 at 18 fps, as for objects outside), hidden
        // while the bar or a box has the buttons.
        if (r.nearby >= 0 && barBusy == 0 && !dialogue.open())
        {
            // The arrow goes over the sprite it marks: a table piece, or the visiting pet (index 5).
            bool have = false;
            gp::SelectorPos mp{0, 0};
            if (r.nearby == gp::kDecorVisitor && r.hasVisitor)
            {
                const gp::AnimChoice ac = gp::settledIdle(r.visitSpecies, true);
                const AnimRef V = animRefById(reactAnimId(ac.id, r.visitPal));
                if (V.size() > 0)
                {
                    const int f = (r.visitTicks * ac.rate / 30) % int(V.size());
                    mp = gp::selectorPosition({gp::kVisitorX, gp::kVisitorY, 0, 0, 0, 0}, V.anim->frames[size_t(f)].h);
                    have = true;
                }
            }
            else if (r.nearby < int(r.table.size()))
            {
                const gp::RoomDecoration &e = r.table[size_t(r.nearby)];
                const AnimRef D = animRefById(uiIdP(e.sprite, e.palette));
                if (D.size() > 0)
                {
                    mp = gp::selectorPosition(e, D.anim->frames[0].h);
                    have = true;
                }
            }
            const AnimRef M = animRefById(reactAnimId(gp::kMarkerArrowAnim, gp::kMarkerPalette));
            if (have && M.size() > 0)
            {
                const int f = gp::markerFrame(gameTicks, int(M.size()));
                const gp::SpriteFrame &s = M.anim->frames[size_t(f)];
                SDL_FRect rc{float(mp.x) - scrollF - s.anchorX + s.dx, float(mp.y) - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
                SDL_RenderCopyF(ren, (*M.tex)[size_t(f)], nullptr, &rc);
            }
        }
    }
};
