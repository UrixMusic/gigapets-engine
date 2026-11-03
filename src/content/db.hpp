// db.hpp - the single place game code reads content from. Base game is imported first, mods layer on top.
#pragma once
#include "records.hpp"
#include <map>
#include <vector>
#include <utility>
#include <algorithm>

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

        bool hasText(const Id &id) const { return texts_.count(id) > 0; }
        bool eraseText(const Id &id) { return texts_.erase(id) > 0; }

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

        bool hasAnim(const Id &id) const { return anims_.count(id) > 0; }
        bool eraseAnim(const Id &id) { return anims_.erase(id) > 0; }

        void put(SpeciesDef s)
        {
            const Id k = s.id;
            if (s.legacyIndex >= 0)
                legacySpecies_[s.legacyIndex] = k;
            species_[k] = std::move(s);
        }
        const SpeciesDef *species(const Id &id) const
        {
            auto i = species_.find(id);
            return i == species_.end() ? nullptr : &i->second;
        }
        const SpeciesDef *speciesByLegacy(int n) const
        {
            auto i = legacySpecies_.find(n);
            return i == legacySpecies_.end() ? nullptr : species(i->second);
        }
        std::vector<const SpeciesDef *> allSpecies() const
        {
            std::vector<const SpeciesDef *> v;
            for (const auto &kv : species_)
                v.push_back(&kv.second);
            return v;
        }

        void put(NpcDef n)
        {
            const Id k = n.id;
            if (!npcs_.count(k))
                npcOrder_.push_back(k);
            npcs_[k] = std::move(n);
        }
        NpcDef *npcMutable(const Id &id)
        {
            auto i = npcs_.find(id);
            return i == npcs_.end() ? nullptr : &i->second;
        }
        bool hasNpc(const Id &id) const { return npcs_.count(id) > 0; }
        int npcCount() const { return int(npcs_.size()); }
        bool eraseNpc(const Id &id)
        {
            if (!npcs_.erase(id))
                return false;
            npcOrder_.erase(std::find(npcOrder_.begin(), npcOrder_.end(), id));
            return true;
        }

        void put(ItemDef d)
        {
            if (auto it = itemIndex_.find(d.id); it != itemIndex_.end())
            {
                d.index = it->second; // a mod "replace" keeps the item's number
                items_[size_t(it->second)] = std::move(d);
                return;
            }
            d.index = int(items_.size()); // new item: next number
            itemIndex_[d.id] = d.index;
            items_.push_back(std::move(d));
        }
        const std::vector<ItemDef> &items() const { return items_; }
        const ItemDef *item(const Id &id) const
        {
            auto i = itemIndex_.find(id);
            return i == itemIndex_.end() ? nullptr : &items_[size_t(i->second)];
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

        void put(InteractionDef d)
        {
            const std::string k = d.area + "|" + d.object;
            interactions_[k] = std::move(d);
        }
        std::vector<const InteractionDef *> allInteractions() const
        {
            std::vector<const InteractionDef *> v;
            for (const auto &kv : interactions_)
                v.push_back(&kv.second);
            return v;
        }
        const InteractionDef *findInteraction(const Id &area, const Id &object) const
        {
            auto i = interactions_.find(area + "|" + object);
            return i == interactions_.end() ? nullptr : &i->second;
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

        // Everything of a kind, for tools that list or export the content (the base export, editors). Ordered by id.
        const std::map<Id, Tileset> &allTilesets() const { return tilesets_; }
        const std::map<Id, CollisionSet> &allCollisions() const { return collisions_; }
        const std::map<Id, ObjectType> &allObjectTypes() const { return objTypes_; }
        const std::map<Id, AreaDef> &allAreas() const { return areas_; }
        const std::map<Id, TextDef> &allTexts() const { return texts_; }
        std::vector<const NpcDef *> allNpcs() const
        {
            std::vector<const NpcDef *> v;
            for (const Id &k : npcOrder_)
                v.push_back(&npcs_.at(k));
            return v;
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
        void put(FindWeightDef f)
        {
            const Id k = f.id;
            if (!finds_.count(k))
                findOrder_.push_back(k);
            finds_[k] = std::move(f);
        }
        const FindWeightDef *findWeight(const Id &id) const
        {
            const auto it = finds_.find(id);
            return it == finds_.end() ? nullptr : &it->second;
        }
        FindWeightDef *findWeightMutable(const Id &id)
        {
            const auto it = finds_.find(id);
            return it == finds_.end() ? nullptr : &it->second;
        }
        void eraseFindWeight(const Id &id)
        {
            finds_.erase(id);
            findOrder_.erase(std::remove(findOrder_.begin(), findOrder_.end(), id), findOrder_.end());
        }
        // The random-find table in the order it was built (the base game's rows first, then the mods').
        std::vector<const FindWeightDef *> findWeights() const
        {
            std::vector<const FindWeightDef *> v;
            for (const Id &k : findOrder_)
                v.push_back(&finds_.at(k));
            return v;
        }
        // Every dig spot / quest reward in the order they were added (the base game's first).
        std::vector<const FixedSpotDef *> allSpots() const
        {
            std::vector<const FixedSpotDef *> v;
            for (const Id &k : spotOrder_)
                v.push_back(&spots_.at(k));
            return v;
        }
        std::vector<const QuestSpawnDef *> allQuestSpawns() const
        {
            std::vector<const QuestSpawnDef *> v;
            for (const Id &k : questSpawnOrder_)
                v.push_back(&questSpawns_.at(k));
            return v;
        }
        void put(QuestSpawnDef q)
        {
            const Id k = q.id;
            if (!questSpawns_.count(k))
                questSpawnOrder_.push_back(k);
            questSpawns_[k] = std::move(q);
        }
        const QuestSpawnDef *questSpawn(const Id &id) const
        {
            const auto it = questSpawns_.find(id);
            return it == questSpawns_.end() ? nullptr : &it->second;
        }
        QuestSpawnDef *questSpawnMutable(const Id &id)
        {
            const auto it = questSpawns_.find(id);
            return it == questSpawns_.end() ? nullptr : &it->second;
        }
        void eraseQuestSpawn(const Id &id)
        {
            questSpawns_.erase(id);
            questSpawnOrder_.erase(std::remove(questSpawnOrder_.begin(), questSpawnOrder_.end(), id), questSpawnOrder_.end());
        }
        // Everything turning in quest number `count` places, in the order the records were added.
        std::vector<const QuestSpawnDef *> questSpawnsFor(int count) const
        {
            std::vector<const QuestSpawnDef *> v;
            for (const Id &k : questSpawnOrder_)
                if (questSpawns_.at(k).count == count)
                    v.push_back(&questSpawns_.at(k));
            return v;
        }
        void put(GateDef g)
        {
            const Id k = g.id;
            if (!gates_.count(k))
                gateOrder_.push_back(k);
            gates_[k] = std::move(g);
        }
        const GateDef *gate(const Id &id) const
        {
            const auto it = gates_.find(id);
            return it == gates_.end() ? nullptr : &it->second;
        }
        GateDef *gateMutable(const Id &id)
        {
            const auto it = gates_.find(id);
            return it == gates_.end() ? nullptr : &it->second;
        }
        void eraseGate(const Id &id)
        {
            gates_.erase(id);
            gateOrder_.erase(std::remove(gateOrder_.begin(), gateOrder_.end(), id), gateOrder_.end());
        }
        // The gates whose barrier is in this area (legacy id), in the order they were added.
        std::vector<const GateDef *> gatesIn(int area) const
        {
            std::vector<const GateDef *> v;
            for (const Id &k : gateOrder_)
                if (gates_.at(k).area == area)
                    v.push_back(&gates_.at(k));
            return v;
        }
        // A mod's palette: its number follows the order they were added in.
        void put(PaletteDef p)
        {
            const auto it = palettes_.find(p.id);
            if (it != palettes_.end())
                p.number = it->second.number;
            else
            {
                p.number = kModPaletteBase + int(paletteOrder_.size()) + 1;
                paletteOrder_.push_back(p.id);
            }
            palettes_[p.id] = std::move(p);
        }
        const PaletteDef *palette(const Id &id) const
        {
            const auto it = palettes_.find(id);
            return it == palettes_.end() ? nullptr : &it->second;
        }
        const PaletteDef *paletteByNumber(int number) const
        {
            for (const Id &k : paletteOrder_)
                if (palettes_.at(k).number == number)
                    return &palettes_.at(k);
            return nullptr;
        }
        std::vector<const PaletteDef *> allPalettes() const
        {
            std::vector<const PaletteDef *> v;
            for (const Id &k : paletteOrder_)
                v.push_back(&palettes_.at(k));
            return v;
        }
        const FixedSpotDef *spot(const Id &id) const
        {
            const auto it = spots_.find(id);
            return it == spots_.end() ? nullptr : &it->second;
        }
        void eraseSpot(const Id &id)
        {
            spots_.erase(id);
            spotOrder_.erase(std::remove(spotOrder_.begin(), spotOrder_.end(), id), spotOrder_.end());
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

        // Insertion order is the quest chain order (QuestRuntime::lastId() reads .back()).
        // Same id replaces in place (a mod "replace"); a new id appends.
        void put(QuestDef q)
        {
            for (QuestDef &e : quests_)
                if (e.id == q.id)
                {
                    e = std::move(q);
                    return;
                }
            quests_.push_back(std::move(q));
        }
        const std::vector<QuestDef> &quests() const { return quests_; }

        // Same id replaces in place; a new id appends. Earlier types win when their roll ranges overlap.
        void put(QuestTypeDef t)
        {
            for (QuestTypeDef &e : questTypes_)
                if (e.id == t.id)
                {
                    e = std::move(t);
                    return;
                }
            questTypes_.push_back(std::move(t));
        }
        const std::vector<QuestTypeDef> &questTypes() const { return questTypes_; }

        void put(MinigameDef m)
        {
            const Id k = m.id;
            minigames_[k] = std::move(m);
        }
        const MinigameDef *minigame(const Id &id) const
        {
            auto i = minigames_.find(id);
            return i == minigames_.end() ? nullptr : &i->second;
        }
        MinigameDef *minigameMutable(const Id &id)
        {
            auto i = minigames_.find(id);
            return i == minigames_.end() ? nullptr : &i->second;
        }
        // Missing games give an empty record, so callers' fallbacks apply.
        const MinigameDef &minigameOr(const Id &id) const
        {
            static const MinigameDef none;
            const MinigameDef *m = minigame(id);
            return m ? *m : none;
        }

        void put(OfferSetDef s)
        {
            const Id k = s.id;
            if (!offerSets_.count(k))
                offerOrder_.push_back(k);
            offerSets_[k] = std::move(s);
        }
        const OfferSetDef *offerSet(const Id &id) const
        {
            auto i = offerSets_.find(id);
            return i == offerSets_.end() ? nullptr : &i->second;
        }
        // First set (in load order) whose npc matches.
        const OfferSetDef *offerSetForNpc(const Id &npc) const
        {
            for (const Id &k : offerOrder_)
            {
                const OfferSetDef &s = offerSets_.at(k);
                if (s.npc == npc)
                    return &s;
            }
            return nullptr;
        }
        bool hasOfferSet(const Id &id) const { return offerSets_.count(id) > 0; }
        bool eraseOfferSet(const Id &id)
        {
            if (!offerSets_.erase(id))
                return false;
            offerOrder_.erase(std::find(offerOrder_.begin(), offerOrder_.end(), id));
            return true;
        }

        const std::vector<Id> &offerSetIds() const
        {
            return offerOrder_;
        }

        void put(BefriendRules r) { befriend_ = std::move(r); }
        const BefriendRules &befriendRules() const { return befriend_; }
        BefriendRules &befriendRulesMutable() { return befriend_; }

        void put(QuestRomTables t) { questTables_ = std::move(t); }
        const QuestRomTables &questTables() const { return questTables_; }
        QuestRomTables &questTablesMutable() { return questTables_; } // a mod can append to itemWeights

        int allocModSprite() { return nextModSprite_++; }

    private:
        int nextModSprite_ = kModSpriteBase;
        std::map<std::string, ExitDef> exits_;
        std::map<std::string, InteractionDef> interactions_;
        std::map<std::string, TransitionDef> transitions_;
        std::map<Id, Tileset> tilesets_;
        std::map<Id, CollisionSet> collisions_;
        std::map<Id, ObjectType> objTypes_;
        std::map<Id, AreaDef> areas_;
        std::map<int, Id> legacyArea_;
        std::map<Id, Anim> anims_;
        std::map<Id, SpeciesDef> species_;
        std::map<int, Id> legacySpecies_;
        std::map<Id, NpcDef> npcs_;
        std::vector<Id> npcOrder_; // stable load order (base first, then mods)
        std::vector<ItemDef> items_;
        std::map<Id, int> itemIndex_;
        std::map<Id, TextDef> texts_;
        std::map<Id, FontDef> fonts_;
        std::map<Id, FixedSpotDef> spots_;
        std::map<Id, PaletteDef> palettes_;
        std::vector<Id> paletteOrder_;
        std::map<Id, FindWeightDef> finds_;
        std::map<Id, GateDef> gates_;
        std::vector<Id> gateOrder_;
        std::map<Id, QuestSpawnDef> questSpawns_;
        std::vector<Id> questSpawnOrder_;
        std::vector<Id> findOrder_;
        std::vector<Id> spotOrder_;
        std::vector<QuestDef> quests_;
        QuestRomTables questTables_;
        BefriendRules befriend_;
        std::map<Id, MinigameDef> minigames_;
        std::vector<QuestTypeDef> questTypes_;
        std::map<Id, OfferSetDef> offerSets_;
        std::vector<Id> offerOrder_;
    };

} // namespace gp