// walk_npc_talk.hpp - talking to an NPC: the stats panel, friend greetings, offer hand-ins and the onTalk script seam.
//
// These used to be lambdas inside runWalk(). They are free functions now: everything they touch comes in through
// WalkServices (shared services) and WalkState (plain data), so a script hook or another file can call them.
// Include after the game headers in walk.cpp, like walk_state.hpp.
#pragma once
#include "walk_services.hpp"
#include "../sale.hpp"
#include <cstdio>
#include <memory>

namespace walk
{

// The two places an NPC's scripts are started.
//
// onTalkStart: the player pressed Z on the NPC, before anything else (quests, hand-ins, dialogue, befriending).
//              A script that returns false takes over: the default handling is skipped. Returns true if so.
inline bool fireTalkStartHook(WalkServices &svc, const gp::NpcDef &d)
{
    return svc.scripts.fireNpcTalkStart(d.id, d.x, d.y).suppress;
}

// onTalk: the player pressed OK on the NPC's line (stationary NPCs). The script's return value is ignored.
inline void fireTalkHook(WalkServices &svc, const gp::NpcDef &d)
{
    svc.scripts.fireNpcTalk(d.id, d.x, d.y);
}

// The stats panel (kind Stats): what TryBefriendNPC shows for an NPC that is not a friend but was already met, with a
// hint about which personality stat to change.
inline void openNpcDialogue(WalkServices &svc, const gp::NpcActor &n)
{
    auto &db = svc.db;
    auto &player = svc.player;
    auto &dialogue = svc.dialogue;
    const gp::NpcDef &d = *n.def;
    gp::DialogueBox b;
    b.kind = gp::BoxKind::Stats;
    b.npcId = d.id;
    b.title = gp::fmtS(db.textOr("base:str/stats_title", "%s"), {db.textOr(d.nameText, "?")});
    const int delta = gp::personalityStatDelta(d.halos, d.pitchforks); // the NPC's own requirements
    const char *key = gp::statHintKey(delta, player.personality[gp::Halos], player.personality[gp::Pitchforks]);
    b.body = db.textOr(std::string("base:str/") + key);
    b.req = {d.halos, d.pitchforks, d.stars, d.lightbulbs};
    dialogue.show(std::move(b));
}
// ShowFriendAdviceText (0x017ccf): the "hello" greeting's follow-up, a random advice line from the friend.
inline void showFriendAdvice(WalkServices &svc, const gp::NpcDef &d)
{
    gp::DialogueBox b = gp::messageBox(gp::fmtAll(svc.db.textOr(gp::friendAdviceId(svc.S.world.npcRng), "..."), svc.player.name));
    b.portrait = gp::uiSpriteId(d.portraitSprite, d.portraitPalette);
    svc.dialogue.show(std::move(b));
}

// ReceiveCureGiftFromFriend (0x017d23): a sick pet gets the medicine for its illness, a healthy one a random item.
// It drops like a minigame prize, at the pet.
inline void giveFriendGift(WalkServices &svc)
{
    int item = -1;
    if (svc.player.illnessId != gp::kHealthy)
    {
        const gp::ItemDef *cure = svc.db.item(gp::illnessCureItem(svc.db.items(), svc.player.illnessId));
        item = cure ? cure->index : -1;
    }
    else
        item = gp::rollFriendGift(svc.S.world.npcRng);
    if (item >= 0 && svc.dropGift)
        svc.dropGift(item);
}

// GrantFriendship; again = its third argument (0 = "meet", 1 = "see you again").
// The box shows the NPC's portrait; pressing OK runs the greeting's follow-up (the handler the ROM hangs on it).
inline void grantFriendship(WalkServices &svc, const gp::NpcDef &d, bool again)
{
    auto &db = svc.db;
    auto &player = svc.player;
    auto &dialogue = svc.dialogue;
    auto &npcRng = svc.S.world.npcRng;
    const gp::FriendGreeting g = gp::rollFriendGreeting(npcRng);
    const char *key = nullptr;
    switch (g)
    {
    case gp::FriendGreeting::Hello:
        key = again ? "greet_again" : "greet_meet";
        break;
    case gp::FriendGreeting::Gift:
        key = again ? "gift_again" : "gift_meet";
        break;
    case gp::FriendGreeting::Game:
        key = again ? "game_again" : "game_meet";
        break;
    }
    gp::DialogueBox b = gp::messageBox(gp::fmtAll(db.textOr(std::string("base:str/") + key, "?"), player.name));
    b.portrait = gp::uiSpriteId(d.portraitSprite, d.portraitPalette);
    switch (g)
    {
    case gp::FriendGreeting::Hello:
        b.onYes = [&svc, dp = &d]
        { showFriendAdvice(svc, *dp); };
        break;
    case gp::FriendGreeting::Gift:
        b.onYes = [&svc]
        { giveFriendGift(svc); };
        break;
    case gp::FriendGreeting::Game:
        b.kind = gp::BoxKind::Confirm;
        b.onYes = [&svc]
        {
            if (svc.startRandomGame)
                svc.startRandomGame();
        };
        b.onNo = [] {};
        break;
    }
    dialogue.show(std::move(b));
}
inline void openFriendPanel(WalkServices &svc, const gp::NpcActor &n)
{
    auto &db = svc.db;
    auto &player = svc.player;
    auto &dialogue = svc.dialogue;
    auto &npcRng = svc.S.world.npcRng;
    const gp::NpcDef &d = *n.def;
    gp::DialogueBox b;
    b.kind = gp::BoxKind::StatsLow;
    b.npcId = d.id;
    const std::string name = db.textOr(d.nameText, "?");
    b.title = gp::fmtS(db.textOr("base:str/stats_title", "%s"), {name});
    const std::string line = gp::fmtAll(db.textOr(gp::defaultLineId(npcRng), "..."), player.name);
    b.body = gp::fmtS(db.textOr("base:str/fmt_name_and_line", "%s %s"), {name, line});
    b.req = {d.halos, d.pitchforks, d.stars, d.lightbulbs};
    b.portrait = gp::uiSpriteId(d.portraitSprite, d.portraitPalette);
    dialogue.show(std::move(b));
}
// The end of a trick lesson (the tail of AttemptTrickLevelN and TickTrickLearnedSuccessSequenceN). The real game also
// walks the pet up to the teacher and has it perform the trick before the last box; that is not ported.
inline void finishTrickLesson(WalkServices &svc, const gp::NpcDef &d, int level, bool won)
{
    auto &db = svc.db;
    auto &player = svc.player;
    const std::string n = std::to_string(level);
    gp::DialogueBox b = gp::messageBox(db.textOr(std::string("base:str/") + (won ? "trick_learned_" : "trick_failed_") + n, "?"));
    b.portrait = gp::uiSpriteId(d.portraitSprite, d.portraitPalette);
    if (won)
    {
        player.tricksKnown = level; // SetTricksKnownAndRerollTraits(level)
        player.rerollTrickCode([&](int lo, int hi)
                               { return svc.S.world.npcRng.range(lo, hi); });
        player.lastEncounterTick = 0; // a success clears the cooldown
    }
    else
        player.lastEncounterTick = svc.S.world.gameTicks; // a failure or a cancel starts it
    svc.dialogue.show(std::move(b));
    svc.saveGame();
}

// TryBefriendNPC's friended branch: teacher 0x30 / 0x31 / 0x32 teaches a trick when the pet knows exactly the tricks
// before it and the cooldown (10 minutes, shared by all three teachers) is over. The teacher shows the trick off, then
// offers the lesson. Returns true if it took the talk, or false to fall through to the normal friend visit.
inline bool tryTrickTeacher(WalkServices &svc, gp::NpcActor &n)
{
    auto &db = svc.db;
    auto &player = svc.player;
    const gp::NpcDef &d = *n.def;
    const gp::MinigameDef &mg = db.minigameOr("base:minigame/trick_guess");
    const int level = d.index - mg.get("firstTeacher", 0x30) + 1;
    if (level < 1 || level > 3 || player.tricksKnown != level - 1 || !svc.startTrickGame)
        return false;
    if (player.lastEncounterTick != 0) // 0 = never; else elapsed gameTicks / 59.94 must pass the cooldown
    {
        const double elapsed = double(svc.S.world.gameTicks - player.lastEncounterTick) / 59.94;
        if (elapsed <= mg.get("cooldownSeconds", 600))
            return false;
    }
    if (!player.trickCodeRolled) // a fresh save: SetTricksKnownAndRerollTraits(0) isn't modelled at game start
        player.rerollTrickCode([&](int lo, int hi)
                               { return svc.S.world.npcRng.range(lo, hi); });

    const std::string name = db.textOr("base:str/trick_" + std::to_string(level) + "_" + std::to_string(std::clamp(player.species, 0, 2)), "?");
    // "Would you like to learn this trick?" (TickLearnTrickEncounterSequenceN's last step), after the showcase.
    auto offerLesson = [&svc, dp = &d, level, name]
    {
        gp::DialogueBox b = gp::messageBox(svc.db.textOr("base:str/trick_learn_prompt", "?"));
        b.kind = gp::BoxKind::Confirm;
        b.portrait = gp::uiSpriteId(dp->portraitSprite, dp->portraitPalette);
        b.onYes = [&svc, dp, level, name]
        {
            svc.startTrickGame(*dp, level, name, [&svc, dp, level](bool won)
                               { finishTrickLesson(svc, *dp, level, won); });
        };
        b.onNo = [] {};
        svc.dialogue.show(std::move(b));
    };
    if (svc.startTrickShowcase)
        svc.startTrickShowcase(n, level, offerLesson);
    else
        offerLesson();
    return true;
}

// TryBefriendNPC, first meeting only: resolve instantly (the real game walks up and plays reactions first).
// Friended NPCs and NPCs already met just open the stats panel for now.
inline void talkToNpc(WalkServices &svc, gp::NpcActor &n)
{
    auto &db = svc.db;
    auto &player = svc.player;
    auto &dialogue = svc.dialogue;
    auto &quests = svc.quests;
    auto &npcRng = svc.S.world.npcRng;
    auto &enc = svc.S.world.enc;
    auto &encNpc = svc.S.world.encNpc;
    auto &refreshBits = svc.refreshBits;
    auto &saveGame = svc.saveGame;
    const gp::NpcDef &d = *n.def;
    if (fireTalkStartHook(svc, d)) // a script returned false from onTalkStart: it handles this talk itself
        return;
    if (quests.talk(d.index))
        return;
    if (const gp::OfferSetDef *s = db.offerSetForNpc(d.id))
    {
        const int reward = gp::rewardFor(*s, gp::offeredCount(*s, player)); // by how many were handed over before this one
        const gp::Id it = gp::tryOffer(*s, player, [&](const gp::Id &i)
                                       { return player.itemCount(i); });
        if (!it.empty())
        {
            player.removeItem(it, 1);
            gp::DialogueBox b = gp::messageBox(gp::fmtAll(db.textOr(s->thanksText, "(missing text)"), player.name));
            b.portrait = gp::uiSpriteId(d.portraitSprite, d.portraitPalette);
            // ReturnSpecialItem, run when the box closes: the pet turns to face front and the reward drops in front of it.
            if (reward >= 0)
                b.onYes = [&svc, reward]
                {
                    if (svc.dropGift)
                        svc.dropGift(reward);
                };
            dialogue.show(std::move(b));
            refreshBits();
            saveGame();
            return;
        }
    }
    if (d.index == gp::kSellerNpc && svc.startSell) // Jeremy buys things: Yes opens the item screen in sell mode
    {
        gp::DialogueBox b = gp::messageBox(db.textOr("base:str/sell_offer", "(missing text)"));
        b.kind = gp::BoxKind::Confirm;
        // No portrait: the ROM's PrepareDialogueMessage box has none.
        b.onYes = [&svc]
        { svc.startSell(); };
        b.onNo = [] {};
        dialogue.show(std::move(b));
        return;
    }
    // std::printf("talk: %s stationary=%d index=%d\n", d.id.c_str(), int(d.stationary), d.index);
    if (d.stationary || d.index > 50) // not friendable: the real interaction (shop, quest, ...) isn't ported yet
    {
        if (d.stationary)
        {
            if (d.lines.size() < 3)
            {
                std::printf("npc %s: no rotating lines\n", d.id.c_str());
                return;
            }
            player.dialogueRotation = (player.dialogueRotation + 1) % 3;
            const int k = player.dialogueRotation == 1 ? 0 : player.dialogueRotation == 2 ? 1
                                                                                          : 2;
            // std::printf("stationary %s: line %d = %s\n", d.id.c_str(), k, d.lines[k].c_str());
            gp::DialogueBox b = gp::messageBox(gp::fmtAll(db.textOr(d.lines[k], "(missing text)"), player.name));
            b.onYes = [&svc, dp = n.def]
            { fireTalkHook(svc, *dp); };
            dialogue.show(std::move(b));
            return;
        }
    }
    if (player.isFriended(d.id))
    {
        if (tryTrickTeacher(svc, n))
            return;
        if (!player.hasEncountered(d.id)) // first visit this session
        {
            player.adjustCare(gp::Happiness, 5);
            player.setEncountered(d.id);
            const int chance = gp::friendVisitChance(d.befriendOdds, player.personality[gp::Stars], d.stars);
            const bool hit = gp::befriendRoll(npcRng, chance);
            std::printf("friend visit %s: chance %d -> %s\n", d.id.c_str(), chance, hit ? "hit" : "miss");
            if (hit)
            {
                grantFriendship(svc, d, true);
                return;
            }
        }
        openFriendPanel(svc, n);
        return;
    }
    // TryBefriendNPC: an NPC that is not a friend but was already met only shows its stats panel. Meeting them again
    // needs a fresh intro, which changing Halos or Lightbulbs gives (Player::adjustPersonality clears "encountered").
    if (player.hasEncountered(d.id))
    {
        openNpcDialogue(svc, n);
        return;
    }
    const int pet = gp::personalityStatDelta(player.personality[gp::Halos], player.personality[gp::Pitchforks]);
    const int npc = gp::personalityStatDelta(d.halos, d.pitchforks);
    player.setEncountered(d.id);
    encNpc = &n;
    enc = std::make_unique<gp::EncounterSequence>(pet, npc);
}

} // namespace walk