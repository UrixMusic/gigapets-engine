// records.hpp - engine-native content. Nothing here knows the ROM exists.
// Ids are strings ("base:npc/12", "mymod:npc/shopkeeper"); no 16-bit limits, no fixed table sizes.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gp
{

    using Id = std::string;
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

    struct NpcDef
    {
        Id id;
        Id nameText;
        std::string name; // resolved text
        int area = 0;     // becomes an Id once areas are migrated
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
        std::vector<Id> lines; // rotating dialogue for stationary NPCs: 3 text ids
        int portraitPalette = 0x28c;
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
        int legacySlot = -1; // original table index; the game keyed progress bits by it (save parity)
        bool interactive() const { return interactId >= 0; }
    };

    struct AreaDef
    {
        Id id;
        int maxEvents = 0; // refill target for random dig spots (area record word 6)
        int legacyId = -1;
        int tilesW = 0, tilesH = 0;  // 32 px tiles
        std::vector<uint16_t> tiles; // row-major
        Id tileset, collision;
        int collisionYOffset = 0; // the game adds a RAM word (0x1afa) to y before testing
        int marginL = 0, marginR = 0, marginT = 0, marginB = 0;
        std::vector<ObjectDef> objects;  // import keeps ROM order (y-sorted); stable-sort by y after mods are applied
        std::vector<IPoint> spawnPoints; // flowers / random items
        int widthPx() const { return tilesW * 32; }
        int heightPx() const { return tilesH * 32; }
    };
    struct FixedSpotDef
    {
        Id id;
        int area = 0; // legacy area id, like NpcDef::area
        int item = 0; // legacy item number
        int x = 0, y = 0;
        int state = 1;      // 1 = buried quest item, 2 = lying on the ground
        int prereqItem = 0; // 0 = none; otherwise this item must be held
        int trigger = -1;   // 0-31; -1 = none. Spawns only while unset; digging sets it
        bool notWhileHeld = false;
        bool notOffered = false;    // skip once returned to the King
        bool notLegacyFlag = false; // skip once the legacy collected flag is set
        int spawnOnGrantItem = 0;   // also spawn at once when this item is granted in `area`
    };

    // A door. Replaces the findStoryExit / findInteractExit if-chains: a mod adds a door by adding a record.
    struct ExitDef
    {
        Id id;
        Id fromArea, object; // object = ObjectDef id inside fromArea
        Id toArea;
        bool onTouch = false;          // true: entering the reach box is enough (story exit); false: needs the interact button
        int flag1a9d = -1;             // value the original wrote to RAM 0x1a9d, -1 = untouched
        bool consumeAndSpawn = false;   // ConsumeAllInactivePendingEvents + SpawnPendingEventItems
        bool hasSpawnOverride = false; // the hardcoded 0<->6 landings
        IPoint spawnOverride{0, 0};
    };

    // Where you appear when going from -> to (room table 0xe06b).
    struct TransitionDef
    {
        Id from, to;
        IPoint spawn{0, 0};
        int flags = 0;
    };

} // namespace gp