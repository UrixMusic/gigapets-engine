// records.hpp - engine-native content. Nothing here knows the ROM exists.
// Ids are strings ("base:npc/12", "mymod:npc/shopkeeper"); no 16-bit limits, no fixed table sizes.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>

namespace gp
{

    // UI sprite numbers at or above this never come from the ROM: the art is registered in the db by a mod.
    constexpr int kModSpriteBase = 0x10000;
    constexpr bool isModSprite(int n) { return n >= kModSpriteBase; }

    // Palette numbers at or above this belong to a mod's palette records (PaletteDef); they are handed out in load order, so they
    // are only good for one session. Saves must keep the palette's id, never its number.
    constexpr int kModPaletteBase = 0x20000;
    constexpr bool isModPalette(int n) { return n >= kModPaletteBase; }

    using Id = std::string;

    // The id the ROM import gives base item n. The only place that format is written.
    inline Id legacyItemId(int n) { return "base:item/" + std::to_string(n); }
    // A scripted (table) quest's id for scripts: QuestDef::id is the quest's place in the chain. Generated quests have none.
    inline Id legacyQuestId(int n) { return "base:quest/" + std::to_string(n); }
    // "base:item/107" -> 107. -1 for anything that isn't a base item id.
    inline int legacyItemNumber(const Id &id)
    {
        static const std::string p = "base:item/";
        if (id.compare(0, p.size(), p) != 0 || id.size() == p.size())
            return -1;
        return std::atoi(id.c_str() + p.size());
    }
    struct IPoint
    {
        int x, y;
    };

    // One animation frame, already coloured. Draw (unflipped) at:
    //   x = ownerX - anchorX + dx,  y = ownerY - anchorY + dy
    // dr = the ROM's "flipped x" word, kept for the flip experiments in walk.cpp.
    struct SpriteFrame
    {
        int w = 0, h = 0;
        int anchorX = 0, anchorY = 0;
        int dx = 0, dy = 0, dr = 0;
        std::vector<uint8_t> rgba; // w*h*4
    };

    struct Anim
    {
        Id id;
        std::vector<SpriteFrame> frames;
    };

    struct TextDef
    {
        std::string id;
        std::string text;
    }; // "base:text/N"

    struct StarterItem
    {
        Id item; // item id, e.g. "base:item/107"
        int count = 1;
    };

    // What makes a pet species a species at new-game time. A mod adds one by adding a record.
    struct SpeciesDef
    {
        Id id;                  // "base:species/0"
        int legacyIndex = -1;   // 0..2 in the base game, -1 for mod species
        int defaultPalette = 0; // used when the save slot has no chosen colour
        int ownIllness = 0;     // what rollRandomIllnessId falls back to for this species
        int startHealth = 0, startHunger = 0, startHappiness = 0, startCleanliness = 0, startTiredness = 0;
        int startMoneyMin = 0, startMoneyMax = 0;
        std::vector<StarterItem> starterItems;
        // the starter room (the 12 pets of a new game): a species is offered only when the record says so
        bool starterRoom = false;
        std::vector<int> starterColors, starterRareColors; // palette numbers it may have there; none = the ROM's tables (base species) or its default palette
        std::vector<std::string> starterRequires;          // profile keys that must be set before it is offered ("key" of its own mod or "mod:key")
        int kitSpecies = 0;                                // whose home furnishings (SetInventorySlot kit) it starts with: a base species, 0..2
    };

    // A colour set a mod adds for sprites that come from the ROM (the roaming pets). Either `colors` (a full list, 5-5-5 words, 0x8000
    // = transparent) or `basedOn` (a ROM palette number) with `set` (index, word) changes on top of it. `number` is what the sprite
    // code is given (kModPaletteBase + order); the colours are filled in once the ROM is at hand (resolveModPalettes).
    struct PaletteDef
    {
        Id id;
        int basedOn = -1;
        std::vector<std::pair<int, uint16_t>> set;
        std::vector<uint16_t> colors;
        int number = 0;
        int species = -1; // -1 = any; 0 dog, 1 monkey, 2 panda: the colour machine only offers it to that species
    };

    struct NpcDef
    {
        Id id;
        Id nameText;
        int area = 0; // becomes an Id once areas are migrated
        int x = 0, y = 0;
        bool stationary = false;
        Id anim; // coloured anim to draw (idle for roamers)
        int halos = 0, pitchforks = 0, stars = 0, lightbulbs = 0;
        int befriendOdds = 0;
        bool pingPong = false;    // path style: true = ping-pong, false = loop
        std::vector<IPoint> path; // offsets from spawn; empty = stays put
        int behaviorFlags = 0;    // raw, until each bit is understood
        int portraitSprite = 0;   // raw ROM sprite id for now
        int speciesOrAnim = 0;    // raw, needed for "player's species" NPCs (0xfffe)
        int appearancePalette = 0;
        int index = 0;  // record number 0..77, the "pool entry" for the area-3 spawn rule
        Id walkAnim[3]; // up, down, sideways; empty = no walk art known, fall back to `anim`
        Id turn180Anim, turn90Anim;
        Id idleSideAnim; // standing idle facing left/right: what the NPC plays when it turns to look at the pet
        Id sickIdleAnim, sadIdleAnim; // the idles for behaviorFlags bit 1 (sick: PlayIdleAnim_FlagB) and bit 2 (sad: _FlagA)
        std::vector<Id> lines; // rotating dialogue for stationary NPCs: 3 text ids
        int portraitPalette = 0x28c;
        Id speciesId; // a roaming pet of this species (a mod's or the base game's): its art is the species' record (speciesOrAnim is its number once the mods are loaded)
        Id palette; // a mod's PaletteDef: recolours the roaming pet's animations (applyNpcPalettes); empty = the ROM's own colours
        int questMin = 0, questMax = 0x7fffffff; // in the world while questMin <= quests completed < questMax (the base game's area 3 pets: 17..)
        bool availableAt(int quests) const { return quests >= questMin && quests < questMax; }
    };

    // Classifier rules from ClassifyReactionCategory (player side) / ClassifyCompatibilityReaction (animal side).
    // pet / npc are personality deltas. Reaction codes: 1, 2 = friendly, 4, 5 = hostile, 6 = bored.
    struct EncounterRules
    {
        // same-sign deltas: ratio = (npc / pet) for the player side, (pet / npc) for the animal side.
        // The first band whose threshold the ratio EXCEEDS wins (list them from highest to lowest).
        std::vector<std::pair<double, int>> bands; // {0.9, 2}, {0.75, 1}
        int belowBands = 6;
        int oppositeSigns = 5;
        int playerPetZero = 1, playerNpcZero = 6;
        int animalNpcZero = 1, animalPetZeroNpcNeg = 4, animalPetZeroNpcPos = 6;
        std::vector<int> friendlyAnimal{1, 2}; // befriending needs the animal side to be one of these...
        std::vector<int> blockingPlayer{4, 5}; // ...and the player side to be none of these
    };

    // The numbers behind befriending. A mod can tune them or add greeting outcomes.
    struct BefriendRules
    {
        int deadZone = 9;            // personality deltas with |a - b| <= deadZone count as 0
        int hintStatCap = 9998;      // a stat above this is "at the cap" for the hint text
        int rollMax = 0x65;          // roll = RandRange(1, rollMax) (1..rollMax-1); success is roll < chance
        int meetMultiplier = 2;      // RollAnimalBefriendChance: base * this, always
        int meetStarMultiplier = 2;  // ... and again * this if stars >= the NPC's requirement
        int visitStarMultiplier = 2; // TryBefriendNPC friended branch: base * this only if stars >= requirement
        // GrantFriendship's outcome: one draw over the total weight. Base actions: hello, gift, game.
        std::vector<std::pair<std::string, int>> greetings;
        std::vector<Id> defaultLines; // GetDefaultNPCDialogueLine: a random text id of these
        EncounterRules encounter;
    };

    // One minigame's tunable numbers. `handler` names the starter the game registers (walk.cpp), so a mod adds a
    // minigame by registering a starter and a record. Keys are per handler; see baseMinigames() for the base ones.
    struct MinigameDef
    {
        Id id;
        std::string handler;
        std::map<std::string, int> num;
        std::map<std::string, std::vector<int>> lists;
        std::map<std::string, std::vector<Id>> idLists; // lists of text ids (a mod's too), e.g. a friend's advice lines
        std::set<std::string> paletteLists;             // the keys of `lists` whose entries are colours: an offset from the species' base colour, or a mod palette's number
        std::set<std::string> itemLists;                // the keys of `lists` whose entries are item numbers (a mod's must exist)
        int get(const std::string &k, int fallback = 0) const
        {
            auto i = num.find(k);
            return i == num.end() ? fallback : i->second;
        }
        const std::vector<Id> &idList(const std::string &k) const
        {
            static const std::vector<Id> none;
            auto i = idLists.find(k);
            return i == idLists.end() ? none : i->second;
        }
        const std::vector<int> &list(const std::string &k) const
        {
            static const std::vector<int> none;
            auto i = lists.find(k);
            return i == lists.end() ? none : i->second;
        }
    };

    // "Using this item does X." `action` picks a handler from the item action registry (item_use.hpp).
    // Empty action = derive it from the legacy type/w9 fields, so items imported before this existed keep working.
    struct ItemUseDef
    {
        std::string action;
        std::map<std::string, int> num;
        std::map<std::string, std::string> str;
    };

    struct ItemDef
    {
        Id id;         // "base:item/12", "mymod:item/pie"
        Id name;       // text id, e.g. "base:text/340" or "mymod:text/pie"
        Id plural;     // empty = no plural text; callers fall back to name + "s"
        int index = 0; // position in db.items(); set by ContentDB::put, never by hand
        int type = 0;
        int nameText = 0, pluralText = 0; // legacy raw ROM text ids, only meaningful for base items
        int maxQty = 0, price = 0;
        int decorSprite = 0, worldSprite = 0, w7 = 0, iconSprite = 0, w9 = 0;
        ItemUseDef use; // what "use" does; a mod item fills this in instead of relying on type/w9
    };

    // ---- quests ----
    struct QuestEntry
    {
        int count = -1;
        Id item; // item id, e.g. "base:item/205"
        int w2 = 0;
    };

    struct QuestDef
    {
        int id = -1;      // objectKeyId == questsCompleted
        int giver = 0;    // g_questTargetId
        int receiver = 0; // g_contentPoolTable
        int typeMode = 0;
        std::vector<int> offerText, completionText; // >= 0 text id, < 0 activity code
        int timeLimit = 0;
        std::vector<QuestEntry> spawn, objectives, bonus;
        int areaTier = 0; // DAT_001e70
        bool generic = false;
    };

    // Tables the generic quest generator reads from the ROM.
    struct QuestRomTables
    {
        std::vector<std::pair<Id, int>> itemWeights; // (item id, weight) x 0x43, word 0xc4c5, stride 2
        int distance[10][10] = {};                   // word 0xe339, [from][to]
    };

    // One quest type: how generic quests of this type are rolled, what they ask for, how they behave.
    // A mod adds a type with a new record. Any field left empty/unset inherits from the base type (mode 0).
    struct QuestPoolEntry
    {
        struct Variant // chosen by species; species -1 = any (used as the fallback)
        {
            int species = -1;
            Id item;
            int buried = 0;
            Id altItem; // empty = no alternative; otherwise a coin flip picks item (1 in 2) or altItem
            int altBuried = 0;
        };
        int weight = 1;
        Id item;                        // empty when bySpecies is used
        int buried = 0;                 // buried = QuestEntry::w2
        std::vector<Variant> bySpecies; // non-empty: ignore item/buried, pick a variant
    };
    struct QuestCountRange // inclusive range, used from `fromTotal` quests completed upward
    {
        int fromTotal = 0, lo = 1, hi = 1;
    };
    enum class QuestReceiver
    {
        Random,
        Self,
        Other,
        Fixed
    };
    struct QuestTypeDef
    {
        Id id;
        int mode = -1; // the typeMode number stored in quests and saves
        // objective items
        std::vector<QuestPoolEntry> pool;                        // empty = use the base type's pool, counts and species rule
        bool speciesFromReceiver = false;                        // variants pick by the receiver's species instead of the player's
        int countLo = 2, countHi = 5;                            // per-item count, inclusive; equal = no random draw
        std::vector<QuestCountRange> objectiveCount, bonusCount; // how many items / bonus items; empty = base
        std::vector<QuestEntry> spawn;                           // extra items dropped for the quest
        int fixedAreaTier = -1;                                  // -1 = rolled by progress
        std::vector<int> excludedTiers;
        bool timed = false;
        bool spawnsOnAccept = true; // false: nothing is scattered when the quest is accepted
        std::string reminderKey;    // empty = the rolled "you don't have everything" lines
        // how the generator picks this type: a roll r (1..9 in the base game) in [rollLo, rollHi] once
        // `rollMinTotal` quests are done. rollHi < rollLo = never rolled (scripted-only types).
        int rollLo = 0, rollHi = -1, rollMinTotal = 0;
        QuestReceiver receiver = QuestReceiver::Random;
        int fixedNpc = -1; // receiver == Fixed: giver and receiver are this NPC; skipped if just used
    };

    struct OfferSetDef
    {
        Id id;                       // base:offers/king
        Id npc;                      // who accepts the items
        std::vector<Id> items;       // order = old firstItem..lastItem
        Id thanksText;               // text record shown on hand-in
        int autoMarkAfterQuests = 0; // 0 = off; king: 0x11 (questCounter > 0x10)
        Id autoMarkItem;             // king: legacyItemId(0xaa)
        // What the NPC gives back for a hand-in: item number [n], n = how many items were already handed over (ReturnItemToKing
        // 0x01b922 reads RAM 0x1e36, GiveFlowerToGardenia 0x01b85a reads 0x1e2a). Empty / out of range = nothing.
        std::vector<int> rewardItems;
    };

    // A bitmap font: glyph k is character 0x21 + k. Space and unknown characters advance by `space`.
    struct FontDef
    {
        Id id; // "base:font/4"
        int space = 10;
        std::vector<SpriteFrame> glyph; // uses w, h, rgba, dx, dy (the editor ignores the anchor for fonts)
        std::vector<int> adv;           // advance in pixels, same indexing
        int lineHeight = 12;            // u16(0xe6bd + font)
    };

    // ---- world data ----
    // Tile art for one (sheet, palette) pair, all tiles baked to RGBA. Tile t starts at t*tileW*tileH*4.
    struct Tileset
    {
        Id id;
        int tileW = 32, tileH = 32, count = 0;
        std::vector<uint8_t> rgba;
    };

    // Ground collision, one byte per pixel per tile (non-zero = blocked). Tiles past `count` are blocked.
    struct CollisionSet
    {
        Id id;
        int tileW = 32, tileH = 32, count = 0;
        std::vector<uint8_t> blocked;
        bool isBlocked(int tile, int px, int py) const
        {
            if (tile < 0 || tile >= count)
                return true;
            return blocked[(size_t(tile) * tileH + py) * tileW + px] != 0;
        }
    };

    // Object footprint that blocks walking, clipped to the sprite header box like CheckObjectCollisionAtPoint.
    // Top-left sits at (objX + ox, objY + oy).
    struct CollisionMask
    {
        int w = 0, h = 0, ox = 0, oy = 0;
        std::vector<uint8_t> blocked; // w*h, empty = no mask
    };

    struct ObjectType
    {
        Id id;
        bool hasArt = false, hasMarker = false;
        SpriteFrame art; // draw at (x - anchorX + dx, y - anchorY + dy)
        CollisionMask mask;
        // The ground shadow. Default = the ROM's own marker for a base type, none for a mod's type.
        enum Shadow { ShadowDefault = 0, ShadowNone = 1, ShadowAuto = 2, ShadowPicture = 3 };
        int shadow = ShadowDefault; // Auto = the blob DrawShadow picks by the art's width (as for pets and items)
        // ShadowPicture: the picture is the db anim shadowAnim(id), drawn with its anchor on the object's feet
        static Id shadowAnim(const Id &typeId) { return typeId + "#shadow"; }
    };

    // A placed object. No fixed count per area: mods can add as many as they like.
    struct ObjectDef
    {
        Id id; // "base:area/3/obj/12"; mods use their own ids
        Id type;
        int x = 0, y = 0;           // feet anchor, px
        int reachX = 0, reachY = 0; // interaction reach
        int interactId = -1;        // -1 = scenery (ROM 0xffff)
        int boxTop = 0, boxBottom = 0, boxLeft = 0, boxRight = 0;
        int legacySlot = -1;                     // original table index; the game keyed progress bits by it (save parity)
        Id contextText;                          // text id from the 0xf83d sign table; empty if this object isn't in it
        int questMin = 0, questMax = 0x7fffffff; // present while questMin <= quests completed < questMax
        bool availableAt(int quests) const { return quests >= questMin && quests < questMax; }
        bool interactive() const { return interactId >= 0; }
        Id requiresOffered; // empty = no condition; object exists only once offeredKey(this id) is set
        std::string goneOnFlag; // empty = no condition; the object disappears once this player flag is set (stat gates)
    };

    struct AreaDef
    {
        Id id;
        int maxEvents = 0; // refill target for random dig spots (area record word 6)
        int legacyId = -1;
        Id name;                     // text id of the area's name
        int tilesW = 0, tilesH = 0;  // 32 px tiles
        std::vector<uint16_t> tiles; // row-major
        Id tileset, collision;
        int collisionYOffset = 0; // the game adds a RAM word (0x1afa) to y before testing
        int music = -1;           // a song (0..8) to start on entering; -1 = the zone's own (base areas) or keep the current one
        bool resume = false;      // a save made here starts here next time (the base game never does; a mod area can ask for it)
        bool diggable = false;    // Z on bare ground digs (the base game's outdoor areas 0..9); a mod area can ask for it
        int marginL = 0, marginR = 0, marginT = 0, marginB = 0;
        std::vector<ObjectDef> objects;  // import keeps ROM order (y-sorted); stable-sort by y after mods are applied
        std::vector<IPoint> spawnPoints; // flowers / random items
        int widthPx() const { return tilesW * 32; }
        int heightPx() const { return tilesH * 32; }
    };
    // What a shop can stock (the ROM's tables at word 0xb303: 22 shops, up to 12 items each, three words an item). The stock is not a table: at the start
    // `rolls` weighted picks are made (InitZoneItemPools), and a timer adds one more to every shop (RollShopItemStock). `cap` is the most of the item the
    // shop offers at once (the word the ROM keeps beside the weight).
    struct ShopItem
    {
        Id item;
        int weight = 1;
        int cap = 5;
    };
    struct ShopDef
    {
        Id id;   // "base:shop/<area number>" or "mymod:shop/x"
        Id area; // the area (a base one or a mod's) that is the shop: its spawn points are where the stock stands
        int rolls = 10;
        std::vector<ShopItem> items;
    };

    // Something that puts an item into a shop's stock when something happens (the ROM's ClaimLRUCacheSlot calls: a full shop drops its oldest entry).
    //   on "area":    the pet walks into `area` (the ROM: item 193 into the second shop once the pet reaches Gigatown...)
    //   on "quest":   quest number `quest` is handed in (a quest of one of `modes`, default 5 = the story quests)
    //   on "type":    a quest of one of `modes` is handed in (the ROM's modes 2, 4 and 6)
    //   on "illness": the pet falls ill
    // `from` says what goes in: "item" (the fixed `item`), "first_objective" / "each_objective" (the quest's objective items, up to 3),
    // "first_spawn" (the first item the quest scattered), "cure" (the item that cures the illness). `only_item` / `except_item` narrow
    // "each_objective". `unless_held` / `unless_offered` (on "area") skip the rule when the pet holds the item or has handed it in.
    struct StockRuleDef
    {
        Id id;
        Id shop; // a ShopDef id
        std::string on = "area";
        Id area;
        int quest = 0;
        std::vector<int> modes;
        std::string from = "item";
        Id item, onlyItem, exceptItem;
        bool unlessHeld = false, unlessOffered = false;
    };

    struct FixedSpotDef
    {
        Id id;
        int area = 0; // legacy area id, like NpcDef::area
        Id item;      // item id, e.g. "base:item/173"
        int x = 0, y = 0;
        int state = 1;    // 1 = buried quest item, 2 = lying on the ground
        Id prereqItem;    // empty = none; otherwise this item must be held
                          // records.hpp, FixedSpotDef: replace `int trigger`
        int trigger = -1; // < 0 = none; consumed marker is the flag triggerKey(trigger)
        bool notWhileHeld = false;
        bool notOffered = false;    // skip once returned to the King
        bool notLegacyFlag = false; // skip once the legacy collected flag is set
        Id spawnOnGrantItem;        // empty = none; also spawn at once when this item is granted in `area`
    };

    // Something placed in the world when a given quest is turned in (the ROM's ProcessQuestTurnIn: quest 15 buries item 262, ...).
    // It stays until picked up, wherever the pet goes (persistent), unlike a FixedSpotDef, which is placed again on every entry.
    struct QuestSpawnDef
    {
        Id id;
        int count = 0; // the quest number whose turn-in places it (quests.counter at that moment: quest 15 -> 15)
        int area = 0;  // legacy area id
        Id item;
        int x = 0, y = 0;
        int state = 1; // 1 = buried (dig it up), 2 = lying on the ground
        bool random = false; // true: x / y unused; it goes on a free one of the area's random spots (spawn_points), like the random finds
        int times = 1;       // how many copies (random only; each takes its own spot)
    };

    // A barrier that goes for good once the pet meets a condition (the five guards' barriers in Magma Mountain). The object named by
    // `barrier` disappears (ObjectDef::goneOnFlag = the gate's id, a player flag that stays set). Checked every step while the pet is in
    // the barrier's area.
    enum class GateKind
    {
        Tricks,      // tricks known >= need
        Personality, // any of `stats` (PersonalityStat numbers: 0 halos, 1 pitchforks, 2 stars, 3 lightbulbs) >= need
        Money,       // money >= need
        Quests       // quests done (the game's counter, which starts at 1) >= need
    };
    struct GateDef
    {
        Id id; // also the name of the player flag that records it opened ("base:gate/5/25" for the base game's, as in old saves)
        int area = 0; // legacy area id of the barrier object
        Id barrier;   // ObjectDef id
        GateKind kind = GateKind::Personality;
        std::vector<int> stats;
        int need = 1;
    };

    // One row of the table a random dig find is drawn from (the ROM's PickRandomEventItemId table): the item turns up with
    // chance weight / (sum of all weights), after the dud roll (tuning.findDudPercent).
    struct FindWeightDef
    {
        Id id;
        Id item;
        int weight = 1;
    };

    // A door. Replaces the findStoryExit / findInteractExit if-chains: a mod adds a door by adding a record.
    struct ExitDef
    {
        Id id;
        Id fromArea, object; // object = ObjectDef id inside fromArea
        Id toArea;
        bool onTouch = false;          // true: entering the reach box is enough (story exit); false: needs the interact button
        int flag1a9d = -1;             // value the original wrote to RAM 0x1a9d, -1 = untouched
        bool consumeAndSpawn = false;  // ConsumeAllInactiveWorldItems + SpawnWorldItemItems
        bool hasSpawnOverride = false; // the hardcoded 0<->6 landings
        IPoint spawnOverride{0, 0};
    };

    // "Interacting with this object does X." Replaces the (area, slot) tables that lived in walk.cpp.
    // `action` picks a handler from the InteractionRegistry; num/str carry its parameters.
    struct InteractionDef
    {
        Id id;
        Id area, object; // same keys as ExitDef
        std::string action;
        std::map<std::string, int> num;
        std::map<std::string, std::string> str;
    };

    // Where you appear when going from -> to (room table 0xe06b).
    struct TransitionDef
    {
        Id from, to;
        IPoint spawn{0, 0};
        int flags = 0;
    };

} // namespace gp