// walk_script_world_bindings.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of ScriptWorldBindings.
// What they need from the rest of the walk session comes in through ScriptWorldBindingsEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct ScriptWorldBindingsEnv
{
    std::unique_ptr<Loaded> &cur;
    gp::ContentDB &db;
    Fade&fade;
    gp::EncounterHost &host;
    std::function<std::string(int)> itemName;
    std::function<int(int)> ownedCount;
    std::optional<gp::ExitDef>&pendingExit;
    std::function<void(int, bool, int, Pose, std::vector<AnimStep>)> playAnimFn;
    gp::Point&pos;
    gp::Point&prevPos;
    std::function<void(const gp::ExitDef&)> requestGoTo;
    std::unique_ptr<RoomView> &room;
    std::function<void(int, int)> sfxFn;
    std::function<int(int)> shopRnd;
    gp::ShopStock &shopStock;
    int &sp;
};

class ScriptWorldBindings : public ScriptWorldBindingsEnv
{
public:
    explicit ScriptWorldBindings(ScriptWorldBindingsEnv env) : ScriptWorldBindingsEnv(std::move(env)) {}
    void sfx(int id, int vol = 255) { sfxFn(id, vol); }
    void playAnim(int aid, bool loop, int rate, Pose p, std::vector<AnimStep> next = {}) { playAnimFn(aid, loop, rate, p, next); }

    auto wapi_getPetPosRun(int &x, int &y)
    {
        if (room)
            return false;
        x = int(pos.x);
        y = int(pos.y);
        return true;
    }

    auto wapi_setPetPosRun(int x, int y)
    {
        if (room || pendingExit)
            return false;
        pos = {std::clamp(x, 0, cur->def->widthPx()), std::clamp(y, 0, cur->def->heightPx())};
        prevPos = pos; // no gliding across the map
        return true;
    }

    auto wapi_petReactRun(int kind)
    {
        if (room || pendingExit || host.pet.busy())
            return false;
        static const gp::ReactAnim as[4] = {gp::ReactAnim::Positive, gp::ReactAnim::Emote, gp::ReactAnim::Shocked, gp::ReactAnim::Negative};
        return host.pet.play(as[std::clamp(kind, 0, 3)], 1);
    }

    int wapi_petAnimNumberRun(const std::string &what)
    {
        const gp::SpeciesArt &art = gp::artOf(sp);
        if (what.find(':') != std::string::npos) // an anim id: a mod's, or the game's "base:anim/34"
        {
            int n = -1;
            if (std::sscanf(what.c_str(), "base:anim/%d", &n) == 1 && n >= 0 && n <= 0x2ff && what.find('@') == std::string::npos)
                return n;
            return db.hasAnim(what) ? gp::modAnimNumber(what) : -1;
        }
        const auto &roles = gp::speciesRoles();
        for (const auto &r : roles)
            if (what == r.first)
                return art.*(r.second);
        for (int k = 0; k < 3; ++k) // the three tricks: "trick1" .. "trick3" (the main animation of each level)
            if (what == "trick" + std::to_string(k + 1))
                return art.trickMain[k];
        return -1;
    }

    auto wapi_petPlayRun(int anim, bool loop, int rate, const std::vector<int> &next)
    {
        if (room || pendingExit || host.pet.busy())
            return false;
        std::vector<AnimStep> rest;
        for (size_t i = 0; i < next.size(); ++i)
            rest.push_back({next[i], i + 1 == next.size() && loop, rate});
        playAnim(anim, loop && next.empty(), rate, Pose::Special, std::move(rest));
        return true;
    }

    auto wapi_travelRun(const std::string &area, int x, int y)
    {
        if (!db.area(area) || pendingExit || fade.a > 0.0 || room)
            return false;
        gp::ExitDef e;
        e.id = "script:go";
        e.fromArea = cur->def->id;
        e.toArea = area;
        e.consumeAndSpawn = true;
        e.hasSpawnOverride = true;
        e.spawnOverride = {x, y};
        requestGoTo(e);
        return pendingExit.has_value();
    }

    // // shop.*: the stock lives here, so the callbacks do too
    const gp::ShopDef * shopOf(const std::string &name)
    { const gp::ShopDef *s = db.shop(name); return s ? s : db.shopForArea(name); }

    auto sapi_shopStockRun(const std::string &name, std::vector<gp::ScriptApi::ShopLine> &out)
    {
        const gp::ShopDef *s = shopOf(name);
        if (!s)
            return false;
        if (const gp::ShopStock::Row *row = shopStock.rowOf(s->area))
            for (const gp::StockEntry &e : *row)
                if (e.present())
                    out.push_back({e.item, e.stock});
        return true;
    }

    auto sapi_shopAddRun(const std::string &name, const std::string &item, int n)
    {
        const gp::ShopDef *s = shopOf(name);
        if (!s || !db.item(item))
            return 0;
        for (int i = 0; i < n; ++i)
            shopStock.claim(s->area, item);
        return n;
    }

    auto sapi_shopRemoveRun(const std::string &name, const std::string &item)
    {
        const gp::ShopDef *s = shopOf(name);
        return s && shopStock.removeItem(s->area, item);
    }

    auto sapi_shopRestockRun(const std::string &name)
    {
        if (name.empty())
        {
            shopStock.restock(db.shops(), shopRnd);
            return !db.shops().empty();
        }
        const gp::ShopDef *s = shopOf(name);
        if (!s)
            return false;
        shopStock.rollOne(*s, shopRnd);
        return true;
    }
};
