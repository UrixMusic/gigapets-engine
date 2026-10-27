// walk_npc_talk.hpp - talking to an NPC: the stats panel, friend greetings, offer hand-ins and the onTalk script seam.
//
// These used to be lambdas inside runWalk(). They are free functions now: everything they touch comes in through
// WalkServices (shared services) and WalkState (plain data), so a script hook or another file can call them.
// Include after the game headers in walk.cpp, like walk_state.hpp.
#pragma once
#include "walk_services.hpp"
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

// The stats panel (kind Stats). Not called yet; kept from the old lambda.
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
// GrantFriendship; again = its third argument (0 = "meet", 1 = "see you again")
inline void grantFriendship(WalkServices &svc, bool again)
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
    if (g == gp::FriendGreeting::Game)
    {
        b.kind = gp::BoxKind::Confirm;
        b.onYes = []
        { std::printf("game offer: yes (minigames not ported)\n"); };
        b.onNo = []
        { std::printf("game offer: no\n"); };
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
        const gp::Id it = gp::tryOffer(*s, player, [&](const gp::Id &i)
                                       { return player.itemCount(i); });
        if (!it.empty())
        {
            player.removeItem(it, 1);
            gp::DialogueBox b = gp::messageBox(gp::fmtAll(db.textOr(s->thanksText, "(missing text)"), player.name));
            b.portrait = gp::uiSpriteId(d.portraitSprite, d.portraitPalette);
            dialogue.show(std::move(b));
            refreshBits();
            saveGame();
            return;
        }
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
        // Trick teachers (0x30..0x32) and their 10-minute cooldown are not ported.
        if (!player.hasEncountered(d.id)) // first visit this session
        {
            player.adjustCare(gp::Happiness, 5);
            player.setEncountered(d.id);
            const int chance = gp::friendVisitChance(d.befriendOdds, player.personality[gp::Stars], d.stars);
            const bool hit = gp::befriendRoll(npcRng, chance);
            std::printf("friend visit %s: chance %d -> %s\n", d.id.c_str(), chance, hit ? "hit" : "miss");
            if (hit)
            {
                grantFriendship(svc, true);
                return;
            }
        }
        openFriendPanel(svc, n);
        return;
    }
    const int pet = gp::personalityStatDelta(player.personality[gp::Halos], player.personality[gp::Pitchforks]);
    const int npc = gp::personalityStatDelta(d.halos, d.pitchforks);
    player.setEncountered(d.id);
    encNpc = &n;
    enc = std::make_unique<gp::EncounterSequence>(pet, npc);
}

} // namespace walk