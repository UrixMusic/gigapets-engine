// walk_area_flow.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of AreaFlow.
// What they need from the rest of the walk session comes in through AreaFlowEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct AreaFlowEnv
{
    const int &H;
    WalkState &S;
    const int &W;
    std::function<AnimRef(const std::string&)> animRefById;
    menubar::MenuBar &bar;
    int&barBusy;
    WalkBarHost &barHost;
    bool&barInHouse;
    int &barkStep;
    std::function<void(int)> beginRoom;
    std::string &blockedExit;
    std::function<std::vector<int>(int)> buildFiltered;
    int &colorStep;
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    gp::Dialogue &dialogue;
    std::unique_ptr<gp::EncounterSequence>&enc;
    gp::NpcActor*&encNpc;
    std::function<void(const gp::AreaDef&, bool)> enterAreaEvents;
    std::function<void(const gp::AreaDef&)> enterShopStock;
    bool&exitsArmed;
    Fade&fade;
    std::function<void(int)> &goHome;
    int &holdTicks;
    ItemMenu&im;
    std::function<std::string(int)> itemName;
    int &itemSlot;
    std::function<void()> loadNpcs;
    std::vector<gp::NpcActor>&npcs;
    std::function<int(int)> ownedCount;
    gp::PendingList &pending;
    std::optional<gp::ExitDef>&pendingExit;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    std::function<void(int)> playBarAnim;
    int &playerSpecies;
    gp::Point&pos;
    int &potionStep;
    gp::Point&prevPos;
    int &questionStep;
    gp::QuestRuntime &quests;
    std::function<void()> &refreshBits;
    SDL_Renderer *&ren;
    gp::ModReport &rep;
    std::function<void(std::function<void()>)> requestScreen;
    std::unique_ptr<RoomView> &room;
    std::function<void()> &saveGame;
    const std::string &savePath;
    int &scriptLock;
    gp::ScriptHost &scripts;
    gp::SaveData &sd;
    std::function<void(int, int)> sfxFn;
    bool &showCollision;
    gp::NpcActor *&showNpc;
    int &sp;
    const gp::SpeciesDef*speciesDef;
    bool &statsOpen;
    SDL_Texture *&tex;
    int &trickStep;
    int &tutStep;
    std::unique_ptr<AreaWorld> &world;
};

class AreaFlow : public AreaFlowEnv
{
public:
    explicit AreaFlow(AreaFlowEnv env) : AreaFlowEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, next); }

    // NPC animation key: 0 = idle (def->anim), 1 = walk up, 2 = walk down, 3 = walk sideways
    auto npcAnim(const gp::NpcActor &n, int key)
    {
        const gp::Id *w = &n.def->anim;
        if (key >= 1 && key <= 3)
            w = &n.def->walkAnim[key - 1];
        else if (key == 4)
            w = &n.def->turn180Anim;
        else if (key == 5)
            w = &n.def->turn90Anim;
        else if (key == 7)
            w = &n.def->idleSideAnim;
        else if (key == 8)
            w = &n.def->sickIdleAnim;
        else if (key == 9)
            w = &n.def->sadIdleAnim;
        else if (key == 6)
            return animRefById(n.forcedAnimId);
        return animRefById(w->empty() ? n.def->anim : *w);
    }

    auto updateNpcAnim(gp::NpcActor &n)
    {
        const int key = n.animKey();
        const int frames = int(npcAnim(n, key).size());
        // Keys 0..3, 7, 8 and 9 (idles and walks) loop; 4/5 (turns) and 6 (reactions) are one-shots. The sick and sad idles
        // run at 7 fps (the dog's sick one at 14), like the pet's.
        const int npcSp = n.def->speciesOrAnim == 0xfffe ? playerSpecies : n.def->speciesOrAnim;
        const int rate = key == 6 ? n.forcedRate : key == 9 ? 7 : key == 8 ? gp::artOf(npcSp).idleFlagBRate : 14;
        n.setAnim(key, frames, rate, key < 4 || key >= 7, key == 6 && n.forcedReverse);
        n.advanceAnim(frames);
    }

    // FindEligibleNPCForEncounter (0x03d27a): an NPC can be talked to when it is on screen and the pet is inside the
    // box around it: strictly less than 0x3c (60) px away on BOTH axes, so a 120 x 120 square, not a circle.
    // When several qualify the LAST one in instance order wins (the ROM keeps overwriting its selection).
    gp::NpcActor * nearestNpc()
    {
        constexpr int kTalkBox = 0x3c;
        const float camXl = clampCamF(float(pos.x) - W / 2, cur->img.w, W);
        const float camYl = clampCamF(float(pos.y) - gp::tuning().camY, cur->img.h, H);
        gp::NpcActor *pick = nullptr;
        for (gp::NpcActor &n : npcs)
        {
            const float sx = float(n.x()) - camXl, sy = float(n.y()) - camYl;
            if (sx < 0 || sx >= W || sy <= 0 || sy > H) // not on screen: the ROM skips it entirely
                continue;
            if (std::abs(n.x() - pos.x) < kTalkBox && std::abs(n.y() - pos.y) < kTalkBox)
                pick = &n;
        }
        return pick;
    }

    const gp::ShopEvent * nearestShopItem()
    {
        const gp::ShopEvent *b = nullptr;
        long bd = 28L * 28L;
        for (const gp::ShopEvent &e : pending.v)
        {
            if (e.type != 3 || e.area != cur->def->legacyId)
                continue;
            const long dx = e.x - pos.x, dy = e.y - pos.y, d = dx * dx + dy * dy;
            if (d <= bd)
            {
                bd = d;
                b = &e;
            }
        }
        return b;
    }

    auto openBar(bool house)
    {
        if (bar.isOpen())
        {
            bar.cancel(barBusy == 2); // same gate as X: ignored while sliding open or closing
            return;
        }
        if (barBusy != 0 || dialogue.open() || enc || im.open || pendingExit)
            return;
        // Not while the pet is busy with something: feeding, digging, a swift-pick drop, or a wash.
        if (S.pet.feedStep != 0 || S.pet.digStep != 0 || S.pet.swiftDropStep != 0 || S.pet.washStep != 0 || showNpc || tutStep != 0 || potionStep != 0 || barkStep != 0 || trickStep != 0 || questionStep != 0 || colorStep != 0 || holdTicks > 0)
            return;
        if (room) // in a room it is always the house bar, and not while the pet is walking in or out or the item is placed
        {
            using P = RoomView::Phase;
            if (room->phase == P::Entering || room->phase == P::Leaving || room->phase == P::Placing || room->phase == P::Placed ||
                room->phase == P::Eating)
                return;
            house = true;
        }
        barBusy = 1;
        barInHouse = house;
        bar.open(house);
        gp::audio().playSfx(menubar::kSfxOpen, menubar::kSfxOpenVol); // OpenMenuBar: PlaySoundEffect(0x59, 0xbe)
        playBarAnim(house ? 0 : 2);
    }

    auto runBarState(int s)
    {
        // the engine system "panel.open": the player opened a panel from the bar (panel "stats", "items", "tricks" or "map"); cancel or replace and it does not open (a mod may
        // open its own screen instead, scene.open)
        auto panel = [&](const char *name, std::function<void()> open)
        {
            gp::SystemCtx pc;
            pc.set("panel", name);
            scripts.system("panel.open", pc, [&]
                           { open(); });
        };
        if (s == kStatsState)
        {
            panel("stats", [&]
                  { requestScreen([&]
                                  { statsOpen = true; }); });
            return;
        }
        if (s >= 0xb && s <= 0xd && goHome) // MapLocationToGameState(slot): Go Home
        {
            goHome(s - 0xb);
            return;
        }
        if (s != 0xe)
        {
            std::printf("bar: SwitchGameState(0x%x)\n", s);
            return;
        }
        panel("items", [&]
              { requestScreen([&]
                              {
            im = {};
            im.open = true;
            im.category = barHost.category;
            im.filtered = buildFiltered(im.category);
            std::printf("item menu: category %d, %zu items, slot %d\n", im.category, im.filtered.size(), itemSlot); }); });
    }

    bool goTo(const gp::ExitDef &e)
    {
        enc.reset();
        encNpc = nullptr;
        const gp::TransitionDef *tr = db.transition(cur->def->id, e.toArea);
        if (!tr && !e.hasSpawnOverride)
        {
            std::printf("no room-table row for %s -> %s\n", cur->def->id.c_str(), e.toArea.c_str());
            return false;
        }
        auto next = loadArea(db, e.toArea, showCollision);
        if (!next)
        {
            std::printf("area %s could not be loaded\n", e.toArea.c_str());
            return false;
        }
        const gp::IPoint s = e.hasSpawnOverride ? e.spawnOverride : tr->spawn;
        const int trFlags = tr ? tr->flags : 0; // (a trip a script asked for, area.go, has no door row)
        scriptLock = 0;                          // a script's hold on the pet ends with the area
        std::printf("area %d -> %d, spawn (%d,%d)\n", cur->def->legacyId, next->def->legacyId, s.x, s.y);
        const int fromLegacy = cur->def->legacyId;
        const gp::Id fromId = cur->def->id;
        scripts.fireAreaLeave(fromId, next->def->id); // (the area has not changed yet)
        cur = std::move(next);
        pos = {s.x, s.y}; // before the save below, which remembers where the pet is inside a mod area
        if ((trFlags & 2) || db.shopForArea(cur->def->id))
            enterShopStock(*cur->def); // destination is a shop
        else
        {
            if ((trFlags & 1) || db.shopForArea(fromId))
                pending.consumeUnflaggedForArea(fromLegacy); // leaving a shop
            enterAreaEvents(*cur->def, e.consumeAndSpawn);
            refreshBits();
            saveGame();
        }
        world = std::make_unique<AreaWorld>(*cur->ground, *cur->runtime, cur->room);
        pos = {s.x, s.y};
        prevPos = pos; // no gliding across the map after a door
        SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cur->img.w, cur->img.h);
        SDL_UpdateTexture(tex, nullptr, cur->img.rgba.data(), cur->img.w * 4);
        cur->runtime->updateVisible(clampCam(pos.x - W / 2, cur->img.w, W),
                                    clampCam(pos.y - gp::tuning().camY, cur->img.h, H), W, H);
        loadNpcs();
        exitsArmed = false;
        scripts.fireAreaEnter(cur->def->id, fromId); // Area.hooks(id).onEnter and Game.hooks().onEnterArea
        return true;
    }

    // The outdoor bar's Go Home (HandleMenuBarInput, button 6): the sounds stop, the area becomes area 0 (where the houses are) and
    // the game state becomes the house's room state (MapLocationToGameState), the same state the door sends the pet to: the pet
    // walks in from the door into its home. Leaving the room puts it back at the door in area 0. Unlike the door there is no
    // door sound; the bar's click was the only one.
    auto goHomeRun(int location)
    {
        requestScreen([&, location]
                      {
            const gp::RoomExitPoint p = gp::roomReturnPoint(location, db.room(location));
            enc.reset();
            encNpc = nullptr;
            if (cur->def->legacyId != 0)
            {
                const gp::AreaDef *home = db.areaByLegacy(0);
                auto next = home ? loadArea(db, home->id, showCollision) : nullptr;
                if (!next)
                {
                    std::printf("go home: area 0 could not be loaded\n");
                    return;
                }
                const int from = cur->def->legacyId;
                const gp::Id fromId = cur->def->id;
                scripts.fireAreaLeave(fromId, next->def->id);
                cur = std::move(next);
                if (db.shopForArea(fromId))
                    pending.consumeUnflaggedForArea(from); // leaving a shop
                enterAreaEvents(*cur->def, true);
                refreshBits();
                saveGame();
                world = std::make_unique<AreaWorld>(*cur->ground, *cur->runtime, cur->room);
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cur->img.w, cur->img.h);
                SDL_UpdateTexture(tex, nullptr, cur->img.rgba.data(), cur->img.w * 4);
                loadNpcs();
                scripts.fireAreaEnter(cur->def->id, fromId);
            }
            pos = {p.x, p.y}; // (leaving the room puts it here again)
            prevPos = pos;
            cur->runtime->updateVisible(clampCam(pos.x - W / 2, cur->img.w, W), clampCam(pos.y - gp::tuning().camY, cur->img.h, H), W, H);
            exitsArmed = false;
            if (!room)
                beginRoom(location); });
    }

    auto requestGoTo(const gp::ExitDef &e)
    {
        if (pendingExit || fade.a > 0.0)
            return;
        if (e.onTouch && e.id == blockedExit)
            return;
        if (scripts.fireBeforeExit(cur->def->id, e.toArea, e.id).suppress)
        {
            if (e.onTouch)
                blockedExit = e.id;
            return;
        }
        blockedExit.clear();
        pendingExit = e;
        fade.go(1.0, 8.0 / 30); // fade out
    }

    auto saveGameRun()
    {
        player.questsCompleted = quests.counter;
        gp::SaveData s = sd; // keep keys we don't own (whatever createNewSave wrote: colour, starter stats...)
        s.setInt("version", 3);
        s.setInt("statsInit", 1);
        gp::saveSpecies(s, sp);
        s.kv.erase("kingFlags");
        s.kv.erase("gardenFlags");
        gp::savePlayer(player, s);
        gp::saveColourIds(s, player.baseColour, player.appearance, speciesDef->defaultPalette, [&](int n) -> gp::Id
                          { const gp::PaletteDef *p = db.paletteByNumber(n); return p ? p->id : gp::Id(); });
        s.kv.erase("inventory");
        gp::saveResume(s, cur->def->id, cur->def->resume, pos.x, pos.y); // only an area that asks ("resume": true) is come back to
        // what the mods keep (save.set): they get a say first (Game onSave), then their keys replace the old ones
        static bool inSave = false;
        if (!inSave)
        {
            inSave = true;
            scripts.fireGameSave();
            inSave = false;
        }
        for (auto it = s.kv.begin(); it != s.kv.end();)
            it = it->first.rfind("mod.", 0) == 0 ? s.kv.erase(it) : std::next(it);
        for (const auto &[k, v] : scripts.modSaveKv())
            s.kv[k] = v;
        s.kv["mods"] = gp::modListString(rep.loaded, rep.versions); // the mods this save was made with
        gp::saveToFile(savePath, s);
        if (scripts.profileDirty()) // a script changed what the mods keep for the whole game
        {
            gp::saveProfileFile(scripts.profileKv());
            scripts.profileSaved();
        }
    }
};
