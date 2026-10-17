// fixed_spawn.hpp - the fixed placements Game_ProcessTick makes on area entry (hidden quest items, lost-and-found
// items, legacy ground items), the "item 196 granted -> spawn 194 now" hook, and the quest-count spawns from
// ProcessQuestTurnIn. Spots are data (FixedSpotDef in ContentDB), so mods add spots with the same gates.
//
// Order on area entry: events.enterArea(area, rnd) first (clear + random flowers), then spawnFixedSpots.
// The ROM only does the fixed spawns on area transitions (Game_ProcessTick param_1 != 0), never on a plain tick.
#pragma once
#include <array>
#include <functional>
#include <string>
#include <vector>
#include "../content/records.hpp"
#include "pending_event.hpp"
#include "player.hpp"

namespace gp {

// What the gates need to know about the player. Adapter for Player below.
struct FixedSpawnEnv {
    std::function<int(int)>  held;        // GetItemsHeld(item)
    std::function<bool(int)> offered;     // HasItemBeenOffered(item): returned to the King
    std::function<bool(int)> legacy;      // IsLegacyFlagSet(item)
    std::function<bool(int)> triggered;   // HasEventBeenTriggered(n)
};

inline bool fixedSpotAllowed(const FixedSpotDef& s, const FixedSpawnEnv& env) {
    if (s.prereqItem > 0 && env.held(s.prereqItem) <= 0) return false;
    if (s.notWhileHeld && env.held(s.item) > 0)          return false;
    if (s.notOffered && env.offered(s.item))             return false;
    if (s.notLegacyFlag && env.legacy(s.item))           return false;
    if (s.trigger >= 0 && env.triggered(s.trigger))      return false;
    return true;
}

// Game_ProcessTick(param_1 != 0) for one area. `spots` = db.fixedSpotsIn(area). Returns how many were placed.
// All are non-persistent (cleared and re-placed on each entry), category 0, no spawn slot. The trigger rides
// along so digging the spot sets it (PendingEventTable::dig).
inline int spawnFixedSpots(PendingEventTable& t, int area, const std::vector<const FixedSpotDef*>& spots,
                           const FixedSpawnEnv& env) {
    int placed = 0;
    for (const FixedSpotDef* s : spots) {
        if (!s || s->area != area) continue;
        if (!fixedSpotAllowed(*s, env)) continue;
        if (t.hasFixedEvent(area, s->item, s->x, s->y)) continue;
        if (t.spawn(area, 0xffff, s->item, s->x, s->y, static_cast<EventState>(s->state),
                    false, false, s->trigger) >= 0)
            ++placed;
    }
    return placed;
}

// GrantItemOrEvent hook (0x2d6bd): granting item 196 in area 1 spawns 194 at once, so no re-entry is needed.
// Generalised through FixedSpotDef::spawnOnGrantItem. Like the ROM it checks only the trigger, not the prereq.
// Call it from wherever walk.cpp grants items, after the grant.
inline int onItemGranted(PendingEventTable& t, int area, int grantedItem,
                         const std::vector<const FixedSpotDef*>& spots, const FixedSpawnEnv& env) {
    int placed = 0;
    for (const FixedSpotDef* s : spots) {
        if (!s || s->area != area || s->spawnOnGrantItem != grantedItem) continue;
        if (s->trigger >= 0 && env.triggered(s->trigger)) continue;
        if (t.hasFixedEvent(area, s->item, s->x, s->y)) continue;
        if (t.spawn(area, 0xffff, s->item, s->x, s->y, static_cast<EventState>(s->state),
                    false, false, s->trigger) >= 0)
            ++placed;
    }
    return placed;
}

// ProcessQuestTurnIn (0x197b9), g_questTypeMode == 5: exact-equality quest counts place a persistent item.
// Only item 262 is buried; the others lie on the ground. The random-slot rewards at counts 26, 55, 57, 94
// (SpawnItemAtRandomFreeSlot) are not covered here.
struct QuestCountSpawn { int count, area, item, x, y, state; };
inline constexpr std::array<QuestCountSpawn, 4> kQuestCountSpawns = {{
    {15, 3, 262, 2226, 136, 1},
    {32, 9, 168, 1376, 938, 2},
    {90, 5, 183, 1636, 617, 2},
    {100, 5, 236, 2380, 640, 2},
}};

// UNCONFIRMED: whether `questsCompleted` is the value before or after this turn-in is counted. Pass the
// value that makes quest 15's turn-in match 15 in your quest code.
inline int spawnQuestCountItems(PendingEventTable& t, int questsCompleted, int questTypeMode) {
    if (questTypeMode != 5) return 0;
    int placed = 0;
    for (const QuestCountSpawn& q : kQuestCountSpawns) {
        if (q.count != questsCompleted) continue;
        if (t.spawn(q.area, 0xffff, q.item, q.x, q.y, static_cast<EventState>(q.state),
                    true, false, 0xffff) >= 0)
            ++placed;
    }
    return placed;
}

// ---- Player adapter ----
// The only place that decides how legacy item numbers map onto Player's string keys. itemKey() must match
// whatever walk.cpp uses for Player::inventory keys; change it here if it differs.
inline std::string spawnItemKey(int item) { return "base:item/" + std::to_string(item); }
inline std::string offeredKey(int item) { return "base:offered/" + std::to_string(item); }   // SetItemOfferedFlag
inline std::string legacyKey(int item)  { return "base:legacy/" + std::to_string(item); }    // SetLegacyFlag

inline void markOffered(Player& p, int item) { p.setFlag(offeredKey(item)); }
inline void markLegacy(Player& p, int item)  { p.setFlag(legacyKey(item)); }

// The returned env holds a reference to `p`; keep the Player alive while it is used.
inline FixedSpawnEnv playerSpawnEnv(const Player& p) {
    FixedSpawnEnv e;
    e.held = [&p](int it) { return p.itemCount(spawnItemKey(it)); };
    e.offered   = [&p](int it) { return p.hasFlag(offeredKey(it)); };
    e.legacy    = [&p](int it) { return p.hasFlag(legacyKey(it)); };
    e.triggered = [&p](int n)  { return p.hasTrigger(n); };
    return e;
}

} // namespace gp