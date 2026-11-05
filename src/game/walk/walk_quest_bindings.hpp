// walk_quest_bindings.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of QuestBindings.
// What they need from the rest of the walk session comes in through QuestBindingsEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct QuestBindingsEnv
{
    std::function<void(const gp::StockEvent&)> applyStock;
    std::vector<Blast> &blasts;
    gp::CameraShake &cameraShake;
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    gp::WorldItems &events;
    unsigned int&gameTicks;
    int &gateBias;
    std::function<void()> &holdDone;
    gp::NpcActor *&holdNpc;
    int &holdTicks;
    std::function<std::string(int)> itemName;
    std::function<void()> loadNpcs;
    std::map<int, const gp::NpcDef*> &modPoolNpcs;
    std::function<gp::NpcActor*()> nearestNpc;
    std::function<const gp::NpcDef*(int)> npcAt;
    std::vector<const gp::NpcDef*> &npcByIndex;
    std::vector<gp::NpcActor>&npcs;
    gp::QuestNudge &nudge;
    std::function<int(int)> ownedCount;
    gp::Point&pos;
    gp::QuestHooks&qh;
    gp::QuestRuntime &quests;
    std::function<void()> &refreshBits;
    const std::function<int(int, int)> &rnd;
    std::function<void()> &saveGame;
    gp::ScriptHost &scripts;
    std::function<void(int, int)> sfxFn;
};

class QuestBindings : public QuestBindingsEnv
{
public:
    explicit QuestBindings(QuestBindingsEnv env) : QuestBindingsEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }

    auto qh_textRun(int id)
    { return gp::fmtAll(db.textOr("base:text/" + std::to_string(id), "?"), player.name); }; // key format: same as item names, unverified for quest text

    auto qh_heldRun(const gp::Id &id)
    { return player.itemCount(id); }

    auto qh_removeRun(const gp::Id &id, int n)
    {
        player.removeItem(id, n);
    }

    auto qh_spawnRun(int area, const gp::Id &id, int st, bool persist)
    {
        const gp::ItemDef *d = db.item(id);
        if (!d)
            return;
        if (const gp::AreaDef *a = db.areaByLegacy(area))
            events.spawnAtFreeSlot(*a, d->index, st == 2 ? gp::EventState::Visible : gp::EventState::BuriedRandom, persist, rnd);
    }

    auto qh_placeRun(const gp::Id &id, int x, int y, bool flag)
    {
        const gp::ItemDef *d = db.item(id);
        if (!d)
            return;
        events.placeAt(cur->def->legacyId, d->index, x, y, flag);
    }

    auto qh_randomEventRun()
    { events.spawnRandom(*cur->def, 1, rnd); }; // the ROM call's args are lost in the decompile

    auto qh_clearFlaggedRun()
    { events.consumePersistent(); }

    auto qh_holdRun(int ticks, std::function<void()> done)
    {
        holdTicks = std::max(ticks, 1);
        holdDone = std::move(done);
        // TickActor_BefriendSuccessRewardSequence / RunNPCBefriendEncounter: the NPC is locked for the wait, so it stands
        // still and does not wander off or start something new (UpdateNPCIdleActivity, SetAnimalEncounterLockFlag)
        if (holdNpc)
            holdNpc->locked = false;
        holdNpc = nullptr;
        const gp::NpcActor *near = nearestNpc();
        for (gp::NpcActor &n : npcs)
            if (&n == near && !n.locked && !n.def->stationary)
            {
                n.locked = true;
                holdNpc = &n;
            }
    }

    auto qh_camXRun()
    { return pos.x; }

    auto qh_camYRun()
    { return pos.y; }

    auto qh_receiverYRun()
    {
        for (const gp::NpcActor &n : npcs)
            if (n.def->index == quests.q.def.receiver)
                return n.y();
        return pos.y;
    }

    auto qh_nowTickRun()
    { return int(gameTicks); }

    auto qh_portraitRun(gp::DialogueBox &b, int p)
    {
        if (const gp::NpcDef *d = npcAt(p))
            b.portrait = gp::uiSpriteId(d->portraitSprite, d->portraitPalette);
    }

    auto questInfo()
    {
        const gp::QuestDef &d = quests.q.def;
        gp::QuestCompleteInfo info;
        info.questId = d.generic ? gp::Id() : gp::legacyQuestId(d.id);
        info.number = quests.counter;
        info.generic = d.generic;
        info.type = quests.world.type(d.typeMode).id;
        if (const gp::NpcDef *g = npcAt(d.giver))
            info.giverId = g->id;
        if (const gp::NpcDef *r = npcAt(d.receiver))
            info.receiverId = r->id;
        return info;
    }

    auto qh_onDeclineRun()
    {
        nudge.restart(long(gameTicks)); // DeclineQuest: the 30 s reminder timer
        scripts.fireQuestAnswer(false, questInfo());
    }

    auto qh_onAcceptRun()
    { scripts.fireQuestAnswer(true, questInfo()); }

    // StartQuestRewardActivity -8 / -10 (TickActor_MiniPetFoundItemSequence / HandleMiniPetGiftSequence, mis-named in the
    // decompile): the pet stands while the dialogue is closed, then the sound 0x5a, the explosion (bubble 0x13), the camera
    // shake, and the rock's object goes while the passage's comes in; a moment later the dialogue goes on.
    auto qh_storyBlastRun(int code, std::function<void()> done)
    {
        const gp::StoryBlast *sb = gp::storyBlastFor(code);
        if (!sb || cur->def->legacyId != sb->area)
        {
            done();
            return;
        }
        qh.hold(gp::kBlastLeadTicks, [&, sb, done]
                {
            sfx(0x5a, 200);
            blasts.push_back({sb->x, sb->y, 0});
            cameraShake.start();
            gateBias = 1;
            refreshBits();
            qh.hold(gp::kBlastTailTicks, done); });
    }

    auto qh_onNextQuestRun()
    {
        gateBias = 0; // the quest count has moved on and does the same job
        player.questsCompleted = quests.counter;
        loadNpcs(); // only ever runs from a dialogue callback, never inside talkToNpc's own NpcActor reference
        refreshBits();
        saveGame();
    }

    // the engine system "quest.load" (systems.hpp): the next quest is being set up. NPCs and the type go by their ids; objectives / bonus / spawn are "item_id*count" lists
    auto qh_shapeQuestRun(gp::QuestDef &d, int counter, bool scripted, const std::function<void()> &roll)
    {
        const auto npcId = [&](int p) -> std::string
        { const gp::NpcDef *n = p >= 0 ? npcAt(p) : nullptr; return n ? n->id : std::string(); };
        const auto poolOf = [&](const std::string &id) -> int
        {
            for (int p = 0; p < 0x4e; ++p)
                if (npcByIndex[size_t(p)] && npcByIndex[size_t(p)]->id == id)
                    return p;
            for (const auto &[p, n] : modPoolNpcs)
                if (n->id == id)
                    return p;
            return -1;
        };
        const auto typeOfId = [&](const std::string &id) -> int
        {
            for (const auto &t : quests.world.types)
                if (t.id == id)
                    return t.mode;
            return -1;
        };
        const auto fill = [&](gp::SystemCtx &c)
        {
            c.set("giver", npcId(d.giver)).set("receiver", npcId(d.receiver)).set("type", quests.world.type(d.typeMode).id).set("tier", d.areaTier).set("time", d.timeLimit)
                .set("objectives", gp::formatQuestEntries(d.objectives)).set("bonus", gp::formatQuestEntries(d.bonus)).set("spawn", gp::formatQuestEntries(d.spawn));
        };
        gp::SystemCtx c;
        c.set("number", counter).set("scripted", scripted ? 1 : 0);
        fill(c);
        scripts.system("quest.load", c, [&]
                       {
            roll();
            fill(c); });
        const auto itemKnown = [&](const gp::Id &i) { return db.item(i) != nullptr; };
        if (const int g = poolOf(c.str("giver")); g >= 0)
            d.giver = g;
        else if (!scripted && c.str("giver").empty())
            d.giver = -1; // (nothing chosen: the game rolls)
        if (const int r = poolOf(c.str("receiver")); r >= 0)
            d.receiver = r;
        if (const int t = typeOfId(c.str("type")); t >= 0)
            d.typeMode = t;
        d.areaTier = std::clamp(int(c.num("tier")), 0, 6);
        d.timeLimit = std::max(0, int(c.num("time")));
        // (a list a mod did not touch stays exactly as it was)
        if (c.str("objectives") != gp::formatQuestEntries(d.objectives))
            d.objectives = gp::parseQuestEntries(c.str("objectives"), itemKnown);
        if (c.str("bonus") != gp::formatQuestEntries(d.bonus))
            d.bonus = gp::parseQuestEntries(c.str("bonus"), itemKnown);
        if (c.str("spawn") != gp::formatQuestEntries(d.spawn))
            d.spawn = gp::parseQuestEntries(c.str("spawn"), itemKnown);
    }

    // the engine system "quest.text": a generated quest's line (request, accept, decline, reminder, complete, hint, fail) before it is shown
    auto qh_textSystemRun(const char *kind, std::string &text)
    {
        const gp::QuestCompleteInfo info = questInfo();
        gp::SystemCtx c;
        c.set("kind", kind).set("text", text).set("number", quests.counter).set("type", info.type).set("giver", info.giverId).set("receiver", info.receiverId);
        scripts.system("quest.text", c, [] {});
        text = c.str("text");
    }

    // the engine system "quest.reward" (systems.hpp): every reward activity of a quest goes through it
    auto qh_rewardSystemRun(int &code)
    {
        gp::SystemCtx c;
        const gp::QuestCompleteInfo info = questInfo();
        c.set("code", code).set("quest", info.questId).set("number", info.number).set("generic", info.generic ? 1 : 0).set("type", info.type).set("giver", info.giverId).set("receiver", info.receiverId);
        const bool run = scripts.system("quest.reward", c, [] {});
        code = int(c.num("code"));
        return run;
    }

    // Game.hooks().onQuestComplete: true = the script took the bonus rewards over (see QuestHooks::onComplete).
    auto qh_onCompleteRun()
    {
        const gp::QuestDef &d = quests.q.def;
        gp::QuestCompleteInfo info;
        info.questId = d.generic ? gp::Id() : gp::legacyQuestId(d.id);
        info.number = quests.counter;
        info.generic = d.generic;
        info.type = quests.world.type(d.typeMode).id;
        if (const gp::NpcDef *g = npcAt(d.giver))
            info.giverId = g->id;
        if (const gp::NpcDef *r = npcAt(d.receiver))
            info.receiverId = r->id;
        // What this quest's turn-in places in the world (quest_spawn records; the quest number is the one just handed in).
        {
            gp::StockEvent ev; // ProcessQuestTurnIn: a hand-in can put items into shops
            ev.kind = gp::StockEvent::TurnIn;
            ev.quest = quests.counter;
            ev.mode = d.typeMode;
            for (const gp::QuestEntry &e : d.objectives)
                ev.objectives.push_back(e.item);
            for (const gp::QuestEntry &e : d.spawn)
                ev.spawns.push_back(e.item);
            applyStock(ev);
        }
        gp::spawnQuestCountItems(events, db.questSpawnsFor(quests.counter), [&](const gp::Id &i) { return gp::legacyItemNumber(i); },
                                [&](int a) { return db.areaByLegacy(a); }, rnd);
        return scripts.fireQuestComplete(info).suppress;
    }

    auto quests_txt_typeLinesRun(int mode, int kind)
    {
        std::vector<std::string> out;
        for (const gp::QuestTypeDef &t : db.questTypes())
            if (t.mode == mode)
                for (const gp::Id &id : kind == 0 ? t.requestTexts : kind == 1 ? t.completeTexts : t.reminderTexts)
                    out.push_back(db.textOr(id, ""));
        return out;
    }

    auto quests_txt_strRun(const std::string &k)
    { return db.textOr("base:str/quest/" + k); }; // "" when missing: the builders then show "(key)"

    auto quests_txt_npcNameRun(int p)
    { const gp::NpcDef *d = npcAt(p); return d ? db.textOr(d->nameText, "?") : std::string("?"); }

    auto quests_txt_npcTierRun(int p)
    { const gp::NpcDef *d = npcAt(p); return d ? d->area : 0; }

    auto quests_txt_areaNameRun(int area)
    {
        const gp::AreaDef *a = db.areaByLegacy(area);
        return a ? db.textOr(a->name, "?") : std::string("?");
    }
};
