// db.hpp - the single place game code reads content from. Base game is imported first, mods layer on top.
#pragma once
#include "records.hpp"
#include <map>
#include <vector>

namespace gp
{

    class ContentDB
    {
    public:
        // add = insert or overwrite (a mod "replace" is the same call with an existing id)

        // with the other put() overloads
        void put(TextDef t)
        {
            const Id k = t.id;
            texts_[k] = std::move(t);
        }

        // with the other getters
        const TextDef *text(const Id &id) const
        {
            auto i = texts_.find(id);
            return i == texts_.end() ? nullptr : &i->second;
        }
        // Missing ids give the fallback instead of a null check at every call site.
        std::string textOr(const Id &id, const std::string &fallback = "") const
        {
            const TextDef *t = text(id);
            return t ? t->text : fallback;
        }

        void put(Tileset t)
        {
            const Id k = t.id;
            tilesets_[k] = std::move(t);
        }
        void put(Anim a)
        {
            const Id k = a.id;
            anims_[k] = std::move(a);
        }

        void put(NpcDef n)
        {
            const Id k = n.id;
            if (!npcs_.count(k))
                npcOrder_.push_back(k);
            npcs_[k] = std::move(n);
        }

        void put(CollisionSet c)
        {
            const Id k = c.id;
            collisions_[k] = std::move(c);
        }
        void put(ObjectType t)
        {
            const Id k = t.id;
            objTypes_[k] = std::move(t);
        }
        void put(AreaDef a)
        {
            const Id k = a.id;
            if (a.legacyId >= 0)
                legacyArea_[a.legacyId] = k;
            areas_[k] = std::move(a);
        }
        void put(FontDef f)
        {
            const Id k = f.id;
            fonts_[k] = std::move(f);
        }
        const FontDef *font(const Id &id) const
        {
            auto i = fonts_.find(id);
            return i == fonts_.end() ? nullptr : &i->second;
        }
        AreaDef *areaMutable(const Id &id)
        {
            auto i = areas_.find(id);
            return i == areas_.end() ? nullptr : &i->second;
        }

        void put(ExitDef e)
        {
            const std::string k = e.fromArea + "|" + e.object + (e.onTouch ? "|t" : "|u");
            exits_[k] = std::move(e);
        }
        void put(TransitionDef t)
        {
            const std::string k = t.from + ">" + t.to;
            if (!transitions_.count(k))
                transitions_[k] = std::move(t); // first row wins, like the game's search
        }
        const ExitDef *findExit(const Id &area, const Id &object, bool onTouch) const
        {
            auto i = exits_.find(area + "|" + object + (onTouch ? "|t" : "|u"));
            return i == exits_.end() ? nullptr : &i->second;
        }
        const TransitionDef *transition(const Id &from, const Id &to) const
        {
            auto i = transitions_.find(from + ">" + to);
            return i == transitions_.end() ? nullptr : &i->second;
        }

        const Tileset *tileset(const Id &id) const
        {
            auto i = tilesets_.find(id);
            return i == tilesets_.end() ? nullptr : &i->second;
        }
        const CollisionSet *collision(const Id &id) const
        {
            auto i = collisions_.find(id);
            return i == collisions_.end() ? nullptr : &i->second;
        }
        const ObjectType *objectType(const Id &id) const
        {
            auto i = objTypes_.find(id);
            return i == objTypes_.end() ? nullptr : &i->second;
        }
        const AreaDef *area(const Id &id) const
        {
            auto i = areas_.find(id);
            return i == areas_.end() ? nullptr : &i->second;
        }
        const AreaDef *areaByLegacy(int n) const
        {
            auto i = legacyArea_.find(n);
            return i == legacyArea_.end() ? nullptr : area(i->second);
        }

        const Anim *anim(const Id &id) const
        {
            auto i = anims_.find(id);
            return i == anims_.end() ? nullptr : &i->second;
        }
        const NpcDef *npc(const Id &id) const
        {
            auto i = npcs_.find(id);
            return i == npcs_.end() ? nullptr : &i->second;
        }

        std::vector<const NpcDef *> npcsInArea(int area) const
        {
            std::vector<const NpcDef *> v;
            for (const Id &k : npcOrder_)
            {
                const NpcDef &n = npcs_.at(k);
                if (n.area == area)
                    v.push_back(&n);
            }
            return v;
        }

        void put(FixedSpotDef s)
        {
            const Id k = s.id;
            if (!spots_.count(k))
                spotOrder_.push_back(k);
            spots_[k] = std::move(s);
        }
        std::vector<const FixedSpotDef *> fixedSpotsIn(int area) const
        {
            std::vector<const FixedSpotDef *> v;
            for (const Id &k : spotOrder_)
            {
                const FixedSpotDef &s = spots_.at(k);
                if (s.area == area)
                    v.push_back(&s);
            }
            return v;
        }

    private:
        std::map<std::string, ExitDef> exits_;
        std::map<std::string, TransitionDef> transitions_;
        std::map<Id, Tileset> tilesets_;
        std::map<Id, CollisionSet> collisions_;
        std::map<Id, ObjectType> objTypes_;
        std::map<Id, AreaDef> areas_;
        std::map<int, Id> legacyArea_;
        std::map<Id, Anim> anims_;
        std::map<Id, NpcDef> npcs_;
        std::vector<Id> npcOrder_; // stable load order (base first, then mods)
        std::map<Id, TextDef> texts_;
        std::map<Id, FontDef> fonts_;
        std::map<Id, FixedSpotDef> spots_;
        std::vector<Id> spotOrder_;
    };

} // namespace gp
