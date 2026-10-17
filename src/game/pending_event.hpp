// pending_event.hpp - the ROM's 56-slot pending-event table: buried dig spots, hidden quest items,
// ground items and shop stock. Pure logic: no SDL, no ROM access, no rendering. The RNG is injected.
//
// Port of PickRandomEventItemId (0x3f633), SpawnPendingEvent (0x3f792), SpawnPendingEventItems,
// FindPendingEventNearPosition (0x3f93b), the hole ring (g_eventRefs) and the pickup branch of
// ProcessStoryModeObjectInteraction (0x13546). Spec: dig_notes.md. "ASSUMED" marks anything the notes
// do not pin down.
//
// Deliberate differences from the ROM:
//  * Events live in a vector in insertion order (like the ROM's compacting table), but the hole ring
//    refers to events by a unique id, so deleting a slot can never leave a stale ring entry.
//  * Trigger ids >= 32 (including 0xffff) and slot ids >= 0xff are normalised to "none".
//  * Random dig spots come from AreaDef::spawnPoints / AreaDef::maxEvents (filled by importAreas).
//  * Events are keyed by AreaKey = AreaDef::legacyId for now, like NpcDef::area. Mod areas with
//    legacyId -1 would all share one key; switch AreaKey to an Id (or a runtime index) when areas migrate.
#pragma once
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <random>
#include <vector>
#include "../content/records.hpp"

namespace gp
{

    // RandRange(lo, hi): hi is EXCLUSIVE, like the ROM's.
    using RandFn = std::function<int(int lo, int hi)>;

    inline RandFn makeRandFn(std::mt19937 &g)
    { // g must outlive the returned function
        return [&g](int lo, int hi)
        { return std::uniform_int_distribution<int>(lo, hi - 1)(g); };
    }

    constexpr int kEventTableMax = 56; // 0x37 + 1
    constexpr int kEventCategoryMax = 7;
    constexpr int kEventNoTrigger = 63;
    constexpr int kEventNoSlot = 0xff;
    constexpr int kDigReach = 30; // px
    constexpr int kHoleRingSize = 5;
    constexpr int kRandomSpawnBatch = 6; // SpawnPendingEventItems(area, 6, 0) on area entry
    constexpr int kSpawnRetries = 200;

    enum class EventState : uint8_t
    {
        BuriedRandom = 0, // flower marker (type 5)
        HiddenQuest = 1,  // drawn as nothing
        Visible = 2,      // pickup marker (type 2); also the state after digging
        Shop = 3          // pickup marker; opens the purchase dialogue
    };

    struct PendingEvent
    {
        uint32_t uid = 0;             // engine-side unique id (not in the ROM)
        uint16_t item = 0;            // 0 = dud hole
        int16_t x = 0, y = 0;         // current position (moves when dug up)
        int16_t homeX = 0, homeY = 0; // position at spawn, restored if a hole is filled in
        int area = 0;
        EventState state = EventState::BuriedRandom;
        bool persistent = false; // survives area change
        bool category = false;
        int spawnSlot = kEventNoSlot;  // index into the area's spawn-point list, kEventNoSlot = fixed
        int trigger = kEventNoTrigger; // < 32: bit set in g_eventTriggeredFlags when dug
    };

    struct Hole
    {
        int16_t x, y;
        int area;
        uint32_t uid;
    }; // uid of the event that was dug here

    using AreaKey = int;
    inline AreaKey areaKey(const AreaDef &a) { return a.legacyId; }

    // PickRandomEventItemId: 15% dud, otherwise a weighted pick (total weight 243).
    // ASSUMED: entry order is the order the spec lists them. The distribution is right either way; only
    // RNG-exact replay of the ROM depends on the real order at word 0xf9db.
    struct WeightedItem
    {
        int item;
        int weight;
    };
    inline constexpr std::array<WeightedItem, 15> kEventItemTable = {{{2, 25}, {6, 25}, {10, 25}, {14, 25}, {3, 5}, {7, 5}, {11, 5}, {15, 5}, {4, 2}, {8, 2}, {12, 2}, {16, 2}, {265, 100}, {266, 5}, {267, 10}}};
    constexpr int kEventItemTotalWeight = 243;

    inline int pickRandomEventItemId(const RandFn &rnd)
    {
        if (rnd(1, 101) < 16)
            return 0;
        int r = rnd(0, kEventItemTotalWeight);
        for (const WeightedItem &w : kEventItemTable)
        {
            if (r < w.weight)
                return w.item;
            r -= w.weight;
        }
        return kEventItemTable.back().item;
    }

    struct DigResult
    {
        enum class Kind
        {
            Miss,
            Dud,
            Item
        };
        Kind kind = Kind::Miss;
        int index = -1; // Kind::Item: index of the now-visible event (valid until the next mutation)
        int item = 0;
        bool triggerSet = false;        // a trigger bit was set: caller should save (SavePetStatusBlock)
        std::optional<Hole> filledHole; // oldest hole pushed out of the ring: play its fill-in bubble if in this area
    };

    // Marker queued by FindNearestMapMarker: 5 = flower, 2 = pickup, -1 = nothing drawn.
    inline int markerTypeFor(EventState s)
    {
        switch (s)
        {
        case EventState::BuriedRandom:
            return 5;
        case EventState::Visible:
        case EventState::Shop:
            return 2;
        default:
            return -1;
        }
    }

    // IsMissingRequiredQuestItem: legacy items need a quest item in the bag before they can be picked up.
    inline int requiredQuestItem(int item)
    {
        switch (item)
        {
        case 0xe0:
        case 0xe2:
        case 0xe7:
        case 0xe9:
            return 0x107;
        case 0xe1:
        case 0xe3:
        case 0xe4:
        case 0xea:
            return 0x108;
        default:
            return 0;
        }
    }

    enum class PickupResult
    {
        Invalid,
        ShopDialogue,
        NeedItem,
        AtCapacity,
        Granted
    };
    struct PickupOutcome
    {
        PickupResult result = PickupResult::Invalid;
        int needItem = 0;
    };

    struct PickupEnv
    {
        std::function<int(int)> held;     // GetItemsHeld(item)
        std::function<int(int)> capacity; // ItemDef::maxQty, already (1 << shift) - 1
        std::function<void(int)> grant;   // GrantItemOrEvent(item, 1, 1)
    };

    class PendingEventTable
    {
    public:
        const std::vector<PendingEvent> &events() const { return ev_; }
        const std::vector<Hole> &holes() const { return holes_; }
        int size() const { return int(ev_.size()); }

        int countForArea(int area) const
        {
            int n = 0;
            for (const PendingEvent &e : ev_)
                if (e.area == area)
                    ++n;
            return n;
        }

        // SpawnPendingEvent. Returns the new event's index, or -1 if the table is full of persistent events.
        // Pass slot 0xffff for fixed spots. Indices are invalidated by any later spawn/remove.
        int spawn(int area, int slot, int item, int x, int y, EventState st,
                  bool persist, bool category, int trigger)
        {
            if (int(ev_.size()) >= kEventTableMax &&
                !evictFirst([](const PendingEvent &e)
                            { return !e.persistent; }))
                return -1;
            // ASSUMED: "more than 7 category-flagged entries evicts by category" = evict the first
            // category-flagged entry once 7 already exist.
            if (category && countCategory() >= kEventCategoryMax)
                evictFirst([](const PendingEvent &e)
                           { return e.category; });
            PendingEvent e;
            e.uid = nextUid_++;
            e.item = uint16_t(item);
            e.x = e.homeX = int16_t(x);
            e.y = e.homeY = int16_t(y);
            e.area = area;
            e.state = st;
            e.persistent = persist;
            e.category = category;
            e.spawnSlot = (slot < 0 || slot >= kEventNoSlot) ? kEventNoSlot : slot;
            e.trigger = (trigger < 0 || trigger >= 32) ? kEventNoTrigger : trigger;
            ev_.push_back(e);
            return int(ev_.size()) - 1;
        }

        // True if a fixed (no spawn slot) event for this item already sits at this spot, wherever it has since
        // been dug to. Lets area-entry code call the fixed spawns twice without duplicating anything.
        bool hasFixedEvent(int area, int item, int x, int y) const
        {
            for (const PendingEvent &e : ev_)
                if (e.area == area && e.spawnSlot == kEventNoSlot && e.item == item && e.homeX == x && e.homeY == y)
                    return true;
            return false;
        }

        // Event_ConsumeTrigger (really "delete slot").
        void remove(int idx)
        {
            if (idx >= 0 && idx < int(ev_.size()))
                ev_.erase(ev_.begin() + idx);
        }
        void removeByUid(uint32_t uid)
        {
            for (size_t i = 0; i < ev_.size(); ++i)
                if (ev_[i].uid == uid)
                {
                    ev_.erase(ev_.begin() + long(i));
                    return;
                }
        }

        // First half of every area entry: delete all non-persistent events, whatever their state.
        void clearNonPersistent()
        {
            ev_.erase(std::remove_if(ev_.begin(), ev_.end(), [](const PendingEvent &e)
                                     { return !e.persistent; }),
                      ev_.end());
        }

        // SpawnPendingEventItems(area, n, 0). Returns how many were placed.
        // ASSUMED: "current events" in the clamp means events already in this area.
        int spawnRandom(const AreaDef &def, int n, const RandFn &rnd)
        {
            const int area = areaKey(def);
            const int total = int(def.spawnPoints.size());
            if (total == 0)
                return 0;
            n = std::min(n, total - countForArea(area));
            int placed = 0;
            for (int k = 0; k < n; ++k)
            {
                const int item = pickRandomEventItemId(rnd);
                for (int attempt = 0; attempt < kSpawnRetries; ++attempt)
                {
                    const int slot = rnd(0, total);
                    if (slotInUse(area, slot))
                        continue;
                    const IPoint &p = def.spawnPoints[size_t(slot)];
                    if (holeNear(area, p.x, p.y))
                        continue;
                    if (spawn(area, slot, item, p.x, p.y, EventState::BuriedRandom, false, false, kEventNoTrigger) >= 0)
                        ++placed;
                    break;
                }
            }
            return placed;
        }

        // SpawnItemAtRandomFreeSlot(area, item, state, persist): up to 200 tries, gives up silently.
        // ROM: persist = 1, category = 0. ASSUMED: HasNearbyEventOfType is the same "hole near" test spawnRandom uses.
        bool spawnAtFreeSlot(const AreaDef &def, int item, EventState st, bool persist, const RandFn &rnd)
        {
            const int area = areaKey(def), total = int(def.spawnPoints.size());
            if (total == 0)
                return false;
            for (int attempt = 0; attempt <= kSpawnRetries; ++attempt)
            {
                const int slot = rnd(0, total);
                if (slotInUse(area, slot))
                    continue;
                const IPoint &p = def.spawnPoints[size_t(slot)];
                if (holeNear(area, p.x, p.y))
                    continue;
                return spawn(area, slot, item, p.x, p.y, st, persist, false, kEventNoTrigger) >= 0;
            }
            return false;
        }

        // SpawnQuestItemsInWorld (persist 1) / SpawnScatteredItemCluster (persist 0): fixed position, visible, category = 1.
        int placeAt(int area, int item, int x, int y, bool persist)
        {
            return spawn(area, 0xffff, item, x, y, EventState::Visible, persist, true, kEventNoTrigger);
        }

        // ConsumeAllFlaggedPendingEvents: flag bit 0 = persist, so this deletes every persistent event.
        void consumePersistent()
        {
            ev_.erase(std::remove_if(ev_.begin(), ev_.end(), [](const PendingEvent &e)
                                     { return e.persistent; }),
                      ev_.end());
        }

        // Area entry (ExitDef::consumeAndSpawn): clear non-persistent events, then roll the random dig spots.
        // Fixed hidden items (Game_ProcessTick, param_1 != 0) spawn after this.
        void enterArea(const AreaDef &def, const RandFn &rnd)
        {
            clearNonPersistent();
            spawnRandom(def, kRandomSpawnBatch, rnd);
        }

        // TickCareStatWatchdog condition 3: top up one spot when no quest is active and the area is below max.
        // The caller owns the timer (the real interval is not decoded).
        bool refillOne(const AreaDef &def, int questPhase, const RandFn &rnd)
        {
            if (questPhase != 0)
                return false;
            if (countForArea(areaKey(def)) >= def.maxEvents)
                return false;
            return spawnRandom(def, 1, rnd) > 0;
        }

        // FindPendingEventNearPosition. (px, py) is the dig point in world pixels.
        // First matching event in table order wins. Scans states 0 and 1 only: the spec says "state is
        // not 2", which would also let a dig hit shop stock (state 3); that looks unintended, so it is excluded.
        DigResult dig(int area, int px, int py, uint32_t &triggerFlags, const RandFn &rnd)
        {
            DigResult r;
            for (size_t i = 0; i < ev_.size(); ++i)
            {
                PendingEvent &e = ev_[i];
                if (e.area != area)
                    continue;
                if (e.state != EventState::BuriedRandom && e.state != EventState::HiddenQuest)
                    continue;
                if (std::abs(px - e.x) >= kDigReach || std::abs(py - e.y) >= kDigReach)
                    continue;

                const Hole hole{int16_t(px), int16_t(py), e.area, e.uid};
                // ASSUMED: roll 0 of 3 = +20, otherwise -20 ("one in three chance of +20").
                e.x = int16_t(px + (rnd(1, 3) == 1 ? 20 : -20));
                e.y = int16_t(py + 7);
                e.state = EventState::Visible;

                if (e.item != 0)
                {
                    if (e.trigger < 32)
                    {
                        triggerFlags |= (1u << e.trigger);
                        r.triggerSet = true;
                    }
                    r.kind = DigResult::Kind::Item;
                    r.index = int(i);
                    r.item = e.item;
                    r.filledHole = addHole(hole);
                }
                else
                {
                    r.kind = DigResult::Kind::Dud;
                    ev_.erase(ev_.begin() + long(i));
                    r.filledHole = addHole(hole);
                }
                return r;
            }
            // Miss: UpdateDigHoleVisuals runs on every dig, so a miss still leaves a hole at the dig point.
            // uid 0 = no event behind it (nextUid_ starts at 1), so resetUnclaimed finds nothing to reset.
            r.filledHole = addHole(Hole{int16_t(px), int16_t(py), area, 0});
            return r;
        }

        // g_eventRefs ring. When a sixth hole is added the oldest is dropped, and if its item is still lying
        // unclaimed it goes back to state 0 (ClearPendingEventMarkerForItem). The caller plays the fill-in
        // bubble when the returned hole is in the current area.
        std::optional<Hole> addHole(const Hole &h)
        {
            std::optional<Hole> filled;
            if (int(holes_.size()) >= kHoleRingSize)
            {
                filled = holes_.front();
                holes_.erase(holes_.begin());
                resetUnclaimed(*filled);
            }
            holes_.push_back(h);
            return filled;
        }

        // FindNearestMapMarker prompt target: nearest (Manhattan) visible/shop event within +-30 px, or -1.
        int nearestPrompt(int area, int px, int py) const
        {
            int best = -1, bestD = INT_MAX;
            for (size_t i = 0; i < ev_.size(); ++i)
            {
                const PendingEvent &e = ev_[i];
                if (e.area != area)
                    continue;
                if (e.state != EventState::Visible && e.state != EventState::Shop)
                    continue;
                const int dx = std::abs(px - e.x), dy = std::abs(py - e.y);
                if (dx >= kDigReach || dy >= kDigReach)
                    continue;
                if (dx + dy < bestD)
                {
                    bestD = dx + dy;
                    best = int(i);
                }
            }
            return best;
        }

        // Pickup branch of ProcessStoryModeObjectInteraction, in the ROM's order. On Granted the event is
        // already deleted; the caller still clears its prompt and runs the first-time tutorial (event 0xf).
        PickupOutcome pickup(int idx, const PickupEnv &env)
        {
            PickupOutcome o;
            if (idx < 0 || idx >= int(ev_.size()))
                return o;
            const PendingEvent e = ev_[size_t(idx)];
            if (e.state == EventState::Shop)
            {
                o.result = PickupResult::ShopDialogue;
                return o;
            }
            if (e.state != EventState::Visible || e.item == 0)
                return o;
            const int need = requiredQuestItem(e.item);
            if (need && env.held(need) <= 0)
            {
                o.result = PickupResult::NeedItem;
                o.needItem = need;
                return o;
            }
            if (env.held(e.item) < env.capacity(e.item))
            {
                env.grant(e.item); // may spawn events itself (item 196 -> 194), so delete by uid
                removeByUid(e.uid);
                o.result = PickupResult::Granted;
                return o;
            }
            o.result = PickupResult::AtCapacity;
            return o;
        }

    private:
        template <class Pred>
        bool evictFirst(Pred p)
        {
            auto it = std::find_if(ev_.begin(), ev_.end(), p);
            if (it == ev_.end())
                return false;
            ev_.erase(it);
            return true;
        }
        int countCategory() const
        {
            int n = 0;
            for (const PendingEvent &e : ev_)
                if (e.category)
                    ++n;
            return n;
        }
        bool slotInUse(int area, int slot) const
        {
            for (const PendingEvent &e : ev_)
                if (e.area == area && e.spawnSlot == slot)
                    return true;
            return false;
        }
        bool holeNear(int area, int x, int y) const
        {
            for (const Hole &h : holes_)
                if (h.area == area && std::abs(h.x - x) <= kDigReach && std::abs(h.y - y) <= kDigReach)
                    return true;
            return false;
        }
        // ASSUMED: only the event that made this hole is reset, back to its spawn position.
        void resetUnclaimed(const Hole &h)
        {
            for (PendingEvent &e : ev_)
            {
                if (e.uid != h.uid)
                    continue;
                if (e.state == EventState::Visible && e.area == h.area)
                {
                    e.state = EventState::BuriedRandom;
                    e.x = e.homeX;
                    e.y = e.homeY;
                }
                return;
            }
        }

        std::vector<PendingEvent> ev_;
        std::vector<Hole> holes_;
        uint32_t nextUid_ = 1;
    };

} // namespace gp