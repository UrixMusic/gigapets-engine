// walk_interactions.hpp - what pressing Z on an object does: the action registry (well, tent, signs, minigames),
// the minigame starters, and the onInteract script seam.
//
// These used to be lambdas inside runWalk(). Content (base game or a mod) names the action in its InteractionDef;
// the handler is looked up here. Include after walk_services.hpp, with the game headers (interactions.hpp,
// minigame_ui.hpp) already included.
#pragma once
#include "walk_services.hpp"
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace walk
{

// Object ids may be strings or numbers depending on the record; scripts only ever see strings.
inline std::string idText(const std::string &s) { return s; }
template <class T>
inline std::string idText(const T &v) { return std::to_string(v); }

// A sign-style message box.
inline void showSign(WalkServices &svc, std::string body)
{
    gp::DialogueBox b = gp::messageBox(std::move(body));
    b.kind = gp::BoxKind::Sign;
    svc.dialogue.show(std::move(b));
}

class Interactions
{
public:
    // Minigames by name. The "minigame" interaction looks its `game` string up here, so the next minigame (or a
    // mod's) registers a starter instead of editing the interaction. Tuning numbers: db.minigame(...).
    std::map<std::string, std::function<void(const gp::InteractionDef &)>> minigameStarters;

    Interactions(WalkServices &svc, Minigames &minigames) : svc_(svc), mg_(minigames)
    {
        minigameStarters["swift_pick"] = [this](const gp::InteractionDef &)
        { mg_.requestSwiftPick(); };
        registerBaseActions();
    }
    // The handlers capture `this`, so an Interactions must stay where it was built.
    Interactions(const Interactions &) = delete;
    Interactions &operator=(const Interactions &) = delete;

    // Z pressed near an object (no shop item or ground item in reach). Returns true if something handled it.
    //   1. the area's onInteract script: `return false` takes over
    //   2. the object's interaction action (well, tent, sign, minigame, ...)
    //   3. the object's context text, shown as a sign
    bool interact(const gp::Id &areaId, const gp::ObjectDef &obj)
    {
        if (svc_.scripts.fireAreaInteract(areaId, idText(obj.id)).suppress)
            return true;
        const gp::InteractionDef *inter = svc_.db.findInteraction(areaId, obj.id);
        if (inter && registry_.run(*inter))
            return true;
        if (!obj.contextText.empty())
        {
            showSign(svc_, gp::fmtAll(svc_.db.textOr(obj.contextText, "(missing text)"), svc_.player.name));
            return true;
        }
        return false;
    }

private:
    void registerBaseActions()
    {
        registry_.add("well", [this](const gp::InteractionDef &d)
                      { mg_.openWell(Minigames::Well{0, 0, d.num.at("cost"), d.num.at("x"), d.num.at("y"),
                                                     d.str.at("needText"), d.str.at("askText")}); });
        registry_.add("tent", [this](const gp::InteractionDef &d)
                      { mg_.openTent(Minigames::Tent{0, 0, d.num.at("x"), d.num.at("y"),
                                                     d.str.at("needText"), d.str.at("askText")}); });
        // Signs of the houses in area 0: "FOR RENT" until the save slot is used, then "Welcome to the home of <n>."
        registry_.add("house_sign", [this](const gp::InteractionDef &d)
                      {
            const gp::SlotInfo si = gp::peekSlot(d.num.at("saveSlot"));
            if (!si.full)
            {
                showSign(svc_, svc_.db.textOr(d.str.at("emptyText")));
                return;
            }
            std::string s = svc_.db.textOr(d.str.at("fullText"));
            if (const size_t p = s.find("%s"); p != std::string::npos)
                s.replace(p, 2, si.name);
            showSign(svc_, std::move(s)); });
        // Quest-count gates: locked text ("%d more quest%s") until questsCompleted >= needed, then the open text.
        registry_.add("gate_sign", [this](const gp::InteractionDef &d)
                      {
            const int left = d.num.at("needed") - svc_.player.questsCompleted;
            if (left <= 0)
            {
                showSign(svc_, svc_.db.textOr(d.str.at("openText")));
                return;
            }
            std::string s = svc_.db.textOr(d.str.at("lockedText"));
            if (const size_t p = s.find("%d"); p != std::string::npos)
                s.replace(p, 2, std::to_string(left));
            if (const size_t p = s.find("%s"); p != std::string::npos)
                s.replace(p, 2, left == 1 ? "" : "s");
            showSign(svc_, std::move(s)); });
        registry_.add("minigame", [this](const gp::InteractionDef &d)
                      {
            const std::string &game = d.str.at("game");
            if (auto it = minigameStarters.find(game); it != minigameStarters.end())
                it->second(d);
            else
            {
                const int code = d.num.count("resultCode") ? d.num.at("resultCode") : -1;
                std::printf("minigame: result code %d (not implemented)\n", code);
                svc_.dialogue.show(gp::messageBox("(minigame " + std::to_string(code) + " goes here)")); // placeholder text
            } });
    }

    WalkServices &svc_;
    Minigames &mg_;
    gp::InteractionRegistry registry_;
};

} // namespace walk