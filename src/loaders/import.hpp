// import.hpp - the ONLY bridge between the ROM and the engine. Everything after this reads ContentDB.
#pragma once
#include "rom.hpp"
#include "sprite.hpp"
#include "npc.hpp"
#include "objects.hpp"
#include "exits.hpp"
#include "../content/db.hpp"
#include "../game/npc_anim_ids.hpp" // pure constants; add at the top of import.hpp
#include "../game/fixed_spawn.hpp"
#include "../game/quest_strings.hpp"
#include "../game/quest_types.hpp"
#include "../game/befriend_rules.hpp"
#include "../game/minigame_defs.hpp"
#include "quest_import.hpp"
#include "../game/item.hpp"
#include "../game/item_effects.hpp"
#include "../game/stat_gate.hpp"
#include "../loaders/fixed_spots.hpp"
#include <array>
#include <cstdio>
#include "text.hpp"
#include "font.hpp"

namespace gp
{

    // Decode an animation (anim table word 0x8ad1, 5 words/record) into coloured frames.
    // palOverride < 0 uses the animation's own default palette (record word 2).
    inline Id importAnim(const Rom &rom, ContentDB &db, int animId, int palOverride = -1, int fallbackPal = -1)
    {
        char buf[48];
        std::snprintf(buf, sizeof buf, palOverride >= 0 ? "base:anim/%d@%x" : "base:anim/%d", animId, palOverride);
        const Id id = buf;
        if (db.anim(id))
            return id;

        const uint32_t rec = kAnimTable + uint32_t(animId) * 5;
        if (rec + 5 > rom.sizeWords())
            return {};
        const int start = rom.u16(rec), end = rom.u16(rec + 1);
        if (end <= start || end - start > 256)
            return {};

        // if (animId == 48 || animId == 55)
        //  std::printf("anim %d: start %d end %d pal %x w3 %x w4 %x\n",
        // animId, start, end, rom.u16(rec + 2), rom.u16(rec + 3), rom.u16(rec + 4));

        Palette pal;
        bool hasPal = readPalette(rom, palOverride >= 0 ? palOverride : rom.u16(rec + 2), pal);
        if (!hasPal && fallbackPal >= 0)
            hasPal = readPalette(rom, fallbackPal, pal); // the pet falls back to palette 0x1e1
        const uint32_t tbl = rom.u16(rec + 4);

        Anim a;
        a.id = id;
        for (int sid = start; sid < end; ++sid)
        {
            DecodedSprite s;
            if (!decodeSprite(rom, sid, s))
                continue;
            SpriteFrame f;
            f.w = s.w;
            f.h = s.h;
            f.anchorX = s.anchorX;
            f.anchorY = s.anchorY;
            const int k = sid - start;
            f.dx = int16_t(rom.u16(tbl + k * 4));
            f.dy = int16_t(rom.u16(tbl + k * 4 + 1));
            f.dr = int16_t(rom.u16(tbl + k * 4 + 2));
            toRGBA(s, hasPal ? &pal : nullptr, false, f.rgba);
            a.frames.push_back(std::move(f));
        }
        db.put(std::move(a));
        return id;
    }

    inline void importPetAnims(const Rom &rom, ContentDB &db, int sp)
    {
        for (int id : {kWalkUp[sp], kWalkDown[sp], kWalkSide[sp], kIdleBase[sp], kIdleSide[sp],
                       kTurn180[sp], kTurn90[sp], kSettled[sp], kCallFront[sp], kCallSide[sp]})
            importAnim(rom, db, id, -1, 0x1e1);
        for (int rid : reactIdsForTier(sp))
            importAnim(rom, db, rid, -1, 0x1e1);
    }

    // Pet and other animations by id, with their own default palette (record word 2). Bad records are skipped.
    inline void importAnimRange(const Rom &rom, ContentDB &db, int from, int to, int fallbackPal = 0x1e1,
                                int playerSpecies = 0)
    {
        for (int n = from; n < to; ++n)
            importAnim(rom, db, n, -1, fallbackPal);
        for (int rid : reactIdsForTier(playerSpecies))
            importAnim(rom, db, rid, -1, fallbackPal);
    }

    // The three starter species (CreateNewPet). Values are from the decompilation; none of it is read from the ROM.
    // Not ported: SetInventorySlot(slot, item) per species, because there is nowhere to store it yet.
    //   species 0: (0x1b,0x6b) (0x1c,0x6f) (3,0x73) (9,0x83)   (1,0x9e) (0,0x93)
    //   species 1: (0x1b,0x6e) (0x1c,0x6f) (3,0x75) (0xc,0x89) (1,0xa0) (0,0x96)
    //   species 2: (0x1b,0x6d) (0x1c,0x6f) (3,0x74) (6,0x86)   (1,0x9f) (0,0x9c)
    inline void importSpecies(ContentDB &db)
    {
        struct Starter
        {
            int item, count;
        };
        struct Row
        {
            int pal, illness;
            std::vector<Starter> items;
        };
        static const Row kRows[3] = {
            {0x1df, 0, {{1, 1}, {0x6b, 1}, {0x6f, 1}, {0x73, 1}, {0x83, 1}, {0x1c, 2}, {0x9e, 1}, {0x93, 1}}},
            {0x29e, 1, {{1, 1}, {0x6e, 1}, {0x6f, 1}, {0x75, 1}, {0x89, 1}, {0x21, 2}, {0xa0, 1}, {0x96, 1}}},
            {0x31e, 2, {{1, 1}, {0x6d, 1}, {0x6f, 1}, {0x74, 1}, {0x86, 1}, {0x23, 2}, {0x9f, 1}, {0x9c, 1}}},
        };
        for (int i = 0; i < 3; ++i)
        {
            SpeciesDef s;
            s.id = "base:species/" + std::to_string(i);
            s.legacyIndex = i;
            s.defaultPalette = kRows[i].pal;
            s.ownIllness = kRows[i].illness;
            s.startHealth = 0x41;
            s.startHunger = 0x34;
            s.startHappiness = 0x50;
            s.startCleanliness = 0x4b;
            s.startTiredness = 0x4b;
            s.startMoneyMin = 0x28;
            s.startMoneyMax = 0x3d;
            for (const auto &it : kRows[i].items)
                s.starterItems.push_back({legacyItemId(it.item), it.count});
            db.put(std::move(s));
        }
    }

    inline void importNpcs(const Rom &rom, ContentDB &db, int playerSpecies = 0)
    {
        TextTable tt{};
        const bool haveText = findTextTable(rom, tt);
        if (!haveText)
            std::printf("import: text table not found, NPC names will be empty\n");

        for (int i = 0; i < kNpcCount; ++i)
        {
            if (kNpcTable + uint32_t(i + 1) * kNpcWords > rom.sizeWords())
                break;
            const NpcRecord r = readNpc(rom, i);
            NpcDef d;
            d.id = "base:npc/" + std::to_string(i);
            d.nameText = "base:text/" + std::to_string(r.nameId);
            if (i >= 0x33 && r.stationary) // 3-word record per stationary NPC at 0xdfc7
            {
                const uint32_t a = 0xdfc7 + uint32_t(i - 0x33) * 3;
                if (a + 3 <= rom.sizeWords())
                    for (int k = 0; k < 3; ++k)
                        d.lines.push_back("base:text/" + std::to_string(rom.u16(a + k)));
            }
            d.area = r.area;
            d.x = r.x;
            d.y = r.y;
            d.index = i;
            d.stationary = r.stationary;
            d.halos = r.halos;
            d.pitchforks = r.pitchforks;
            d.stars = r.stars;
            d.lightbulbs = r.lightbulbs;
            d.befriendOdds = r.befriendOdds;
            d.pingPong = r.wanderFlags == 1;
            for (auto &p : npcPath(rom, r))
                d.path.push_back({p.x, p.y});
            d.behaviorFlags = r.behaviorFlags;
            int portrait = r.portraitSprite, portraitPal = r.appearancePalette;
            if (i >= 0x30 && i <= 0x32) // ResolveStarterOrSpeciesDialogueTextId / ResolveSpeciesTextId
            {
                static const int kTile[3] = {0x8ae, 0x8ba, 0x8bc};
                static const int kPal[3][3] = {{0x1e3, 0x2a2, 0x322}, {0x1e4, 0x2a3, 0x323}, {0x1fe, 0x2bd, 0x33d}};
                const int sp = std::clamp(playerSpecies, 0, 2);
                portrait = kTile[sp];
                portraitPal = kPal[i - 0x30][sp];
            }
            d.portraitSprite = portrait;
            d.portraitPalette = portraitPal;
            if (portrait > 0 && portrait != 0xffff)
                importUiSprite(db, rom, portrait, portraitPal);
            d.speciesOrAnim = r.speciesOrAnim;
            d.appearancePalette = r.appearancePalette;
            const int an = npcAnimId(r, playerSpecies);
            if (an >= 0)
                d.anim = importAnim(rom, db, an, r.appearancePalette);
            if (!r.stationary)
            {
                const int sp = r.speciesOrAnim == 0xfffe ? playerSpecies : r.speciesOrAnim;
                if (sp >= 0 && sp <= 2)
                {
                    for (int rid : reactIdsForTier(sp))
                        importAnim(rom, db, rid, r.appearancePalette);
                    d.walkAnim[0] = importAnim(rom, db, kWalkUp[sp], r.appearancePalette);
                    d.walkAnim[1] = importAnim(rom, db, kWalkDown[sp], r.appearancePalette);
                    d.walkAnim[2] = importAnim(rom, db, kWalkSide[sp], r.appearancePalette);
                    d.turn180Anim = importAnim(rom, db, kTurn180[sp], r.appearancePalette);
                    d.turn90Anim = importAnim(rom, db, kTurn90[sp], r.appearancePalette);
                    d.idleSideAnim = importAnim(rom, db, kIdleSide[sp], r.appearancePalette); // PlayIdleAnim_Scripted, sideways
                    d.sickIdleAnim = importAnim(rom, db, kIdleFlagB[sp], r.appearancePalette); // UpdateNPCIdleActivity: flag bit 1
                    d.sadIdleAnim = importAnim(rom, db, kIdleFlagA[sp], r.appearancePalette);  // flag bit 2
                }
            }
            if (i >= 0x33)
            {
                const uint32_t a = 0xdfc7 + uint32_t(i - 0x33) * 3;
                //  std::printf("npc %d stationary=%d lines=%zu words@%x: %x %x %x\n", i, int(r.stationary), d.lines.size(), a,
                // rom.u16(a), rom.u16(a + 1), rom.u16(a + 2));
            }
            db.put(std::move(d));
        }
    }

    inline void importItems(const Rom &rom, ContentDB &db)
    {
        for (const ItemDef &raw : readItems(rom))
        {
            ItemDef d = raw;
            d.id = "base:item/" + std::to_string(raw.index);
            d.name = "base:text/" + std::to_string(raw.nameText);
            d.plural = raw.pluralText == 0xffff ? Id() : "base:text/" + std::to_string(raw.pluralText);
            d.use = legacyItemUse(d);
            db.put(std::move(d));
        }
    }

    // ---- tilesets, collision, object types, areas ----
    inline int tileCapacity(const Rom &rom, uint32_t seg, int tileWords)
    {
        uint32_t nx = 0xffffffffu;
        for (int k = 0; k < kFrameCount; ++k)
        {
            const uint32_t s = readFrame(rom, k).seg;
            if (s > seg && s < nx)
                nx = s;
        }
        const uint32_t endW = nx == 0xffffffffu ? rom.sizeWords() : nx * 0x40;
        if (endW <= seg * 0x40 || tileWords <= 0)
            return 1;
        return std::max(1, std::min<int>(2048, int((endW - seg * 0x40) / uint32_t(tileWords))));
    }

    // Map-tile colour: unlike sprites, index 0 is a real palette entry here (only the 0x8000 flag is see-through).
    inline void pushTilePixel(std::vector<uint8_t> &out, const Palette *pal, int v, int bpp)
    {
        uint8_t c[4];
        if (!pal || v >= int(pal->colors.size()))
        {
            c[0] = c[1] = c[2] = uint8_t(v * 255 / ((1 << bpp) - 1));
            c[3] = 255;
        }
        else
        {
            const int w = pal->colors[v];
            if (w & 0x8000)
            {
                c[0] = c[1] = c[2] = c[3] = 0;
            }
            else
            {
                c[0] = uint8_t(((w >> 10) & 31) * 255 / 31);
                c[1] = uint8_t(((w >> 5) & 31) * 255 / 31);
                c[2] = uint8_t((w & 31) * 255 / 31);
                c[3] = 255;
            }
        }
        out.insert(out.end(), c, c + 4);
    }

    inline Id importTileset(const Rom &rom, ContentDB &db, int frameIdx, int palId)
    {
        char buf[48];
        std::snprintf(buf, sizeof buf, "base:tileset/%d@%x", frameIdx, palId);
        const Id id = buf;
        if (db.tileset(id))
            return id;
        if (frameIdx < 0 || frameIdx >= kFrameCount)
            return {};
        const Frame f = readFrame(rom, frameIdx);
        const int bpp = 2 * ((f.attr & 3) + 1), tw = 8 << ((f.attr >> 4) & 3), th = 8 << ((f.attr >> 6) & 3); // tiles are square (32x32) in practice
        const int tileWords = tw * th * bpp / 16;
        Palette pal;
        const bool hasPal = readPalette(rom, palId, pal);
        Tileset ts;
        ts.id = id;
        ts.tileW = tw;
        ts.tileH = th;
        const uint32_t base = uint32_t(f.seg) * 0x40;
        const int cap = tileCapacity(rom, f.seg, tileWords);
        for (int t = 0; t < cap; ++t)
        {
            if (base + uint32_t(t + 1) * tileWords > rom.sizeWords())
                break;
            const auto px = unpackPixels(rom, base + uint32_t(t) * tileWords, tw * th, bpp);
            for (uint8_t v : px)
                pushTilePixel(ts.rgba, hasPal ? &pal : nullptr, v, bpp);
            ++ts.count;
        }
        db.put(std::move(ts));
        return id;
    }

    // Collision sheets use their own depth rule (SampleTilePixelValue): 1 -> 4bpp, 3 -> 8bpp, else 2bpp.
    inline Id importCollision(const Rom &rom, ContentDB &db, int frameIdx)
    {
        const Id id = "base:collision/" + std::to_string(frameIdx);
        if (db.collision(id))
            return id;
        if (frameIdx < 0 || frameIdx >= kFrameCount)
            return {};
        const Frame f = readFrame(rom, frameIdx);
        const int d = f.attr & 3, bpp = d == 1 ? 4 : d == 3 ? 8
                                                            : 2;
        const int pw = 8 << ((f.attr >> 4) & 3), ph = 8 << ((f.attr >> 6) & 3);
        if (pw != 32 || ph != 32)
        {
            std::printf("import: collision frame %d is %dx%d, only 32x32 supported\n", frameIdx, pw, ph);
            return {};
        }
        const int tileWords = pw * ph * bpp / 16;
        const uint32_t base = uint32_t(f.seg) * 0x40;
        CollisionSet cs;
        cs.id = id;
        const int cap = tileCapacity(rom, f.seg, tileWords);
        for (int t = 0; t < cap; ++t)
        {
            if (base + uint32_t(t + 1) * tileWords > rom.sizeWords())
                break;
            for (uint8_t v : unpackPixels(rom, base + uint32_t(t) * tileWords, pw * ph, bpp))
                cs.blocked.push_back(v ? 1 : 0);
            ++cs.count;
        }
        db.put(std::move(cs));
        return id;
    }

    inline Id importObjectType(const Rom &rom, ContentDB &db, int t)
    {
        const Id id = "base:objtype/" + std::to_string(t);
        if (db.objectType(id))
            return id;
        const uint32_t a = kTypeTable + uint32_t(t) * 5;
        ObjectType ot;
        ot.id = id;
        if (a + 5 <= rom.sizeWords())
        {
            const int marker = rom.u16(a), spr = rom.u16(a + 1), pal = rom.u16(a + 2), ov = rom.u16(a + 4);
            ot.hasMarker = marker != 0;
            DecodedSprite d;
            if (decodeSprite(rom, spr, d))
            {
                Palette p;
                const bool has = readPalette(rom, pal, p);
                ot.hasArt = true;
                ot.art.w = d.w;
                ot.art.h = d.h;
                ot.art.anchorX = d.anchorX;
                ot.art.anchorY = d.anchorY;
                ot.art.dx = -d.originX;
                ot.art.dy = -d.originY + 1; // GigaEditr: +1, art sat 1px high otherwise
                toRGBA(d, has ? &p : nullptr, false, ot.art.rgba);
            }
            DecodedSprite m;
            SpriteHeader mh;
            if (ov != 0xffff && readSpriteHeader(rom, ov, mh) && decodeSprite(rom, ov, m))
            {
                ot.mask.w = mh.w;
                ot.mask.h = mh.h;
                ot.mask.ox = -mh.anchorX;
                ot.mask.oy = -mh.anchorY;
                ot.mask.blocked.assign(size_t(mh.w) * mh.h, 0);
                for (int y = 0; y < mh.h; ++y)
                    for (int x = 0; x < mh.w; ++x)
                    {
                        const size_t sx = size_t(x + m.originX), sy = size_t(y + m.originY);
                        if (sx < size_t(m.w) && sy < size_t(m.h))
                            ot.mask.blocked[size_t(y) * mh.w + x] = m.idx[sy * m.w + sx] != 0;
                    }
            }
        }
        db.put(std::move(ot));
        return id;
    }

    // TryShowContextualStoryText table at word address 0xf83d: 29 entries of {object slot, area, text id}
    inline int contextTextId(const Rom &rom, int area, int slot)
    {
        for (int i = 0; i < 29; ++i)
        {
            const uint32_t a = 0xf83d + uint32_t(i) * 3;
            if (a + 3 > rom.sizeWords())
                break;
            if (int(rom.u16(a)) == slot && int(rom.u16(a + 1)) == area)
                return rom.u16(a + 2);
        }
        return -1;
    }
    inline void importAreas(const Rom &rom, ContentDB &db)
    {
        const int n = areaCount(rom);
        for (int id = 0; id < n; ++id)
        {
            Area ar;
            if (!readArea(rom, id, ar))
                continue;
            AreaDef a;
            a.id = "base:area/" + std::to_string(id);
            a.legacyId = id;
            a.name = "base:text/" + std::to_string(rom.u16(0xe303 + id));
            a.tilesW = ar.tilesW;
            a.tilesH = ar.tilesH;
            a.tiles = ar.tiles;
            a.marginL = kMarginLeft;
            a.marginR = kMarginRight;
            a.marginT = kMarginTop;
            a.marginB = kMarginBottom;
            RoomLink link;
            if (readRoomLink(rom, id, link))
            {
                a.tileset = importTileset(rom, db, link.tileFrame, link.paletteId);
                a.collision = importCollision(rom, db, link.collisionFrame);
            }
            else
                std::printf("import: area %d has no room-table row (no tileset/collision)\n", id);
            for (size_t k = 0; k < ar.objects.size(); ++k)
            {
                const ObjectRec &o = ar.objects[k];
                ObjectDef d;
                d.id = a.id + "/obj/" + std::to_string(k);
                d.type = importObjectType(rom, db, o.type);
                d.x = o.x;
                d.y = o.y;
                d.reachX = o.reachX;
                d.reachY = o.reachY;
                d.interactId = o.interactive() ? o.interactId : -1;
                d.boxTop = o.yTop;
                d.boxBottom = o.yBottom;
                d.boxLeft = o.xLeft;
                d.boxRight = o.xRight;
                d.legacySlot = int(k);
                if (const int t = contextTextId(rom, id, int(k)); t >= 0)
                    d.contextText = "base:text/" + std::to_string(t);
                a.objects.push_back(std::move(d));
            }
            const uint32_t rec = kAreaRecords + uint32_t(id) * 8; // [5] = point count, [7] = point list
            const int pc = rom.u16(rec + 5);
            const uint32_t pp = rom.u16(rec + 7);
            a.maxEvents = rom.u16(rec + 6);
            if (pc <= 500)
                for (int k = 0; k < pc && pp + uint32_t(k) * 2 + 2 <= rom.sizeWords(); ++k)
                    a.spawnPoints.push_back({int(rom.u16(pp + k * 2)), int(rom.u16(pp + k * 2 + 1))});
            db.put(std::move(a));
        }
    }

    inline Id areaIdOf(int legacy) { return "base:area/" + std::to_string(legacy); }

    // Room table (0xe06b, 73 x 8 words): from, to, tileFrame, palette, collisionFrame, spawnX, spawnY, flags.
    // Also checks that the (from,to) row agrees with the destination area's own tileset/collision; if a row ever
    // differed, the old loader would have used the row's frames, but areas here own a single tileset.
    inline void importTransitions(const Rom &rom, ContentDB &db)
    {
        int warn = 0;
        for (int k = 0; k < kRoomTableCount; ++k)
        {
            const uint32_t a = kRoomTable + uint32_t(k) * 8;
            if (a + 8 > rom.sizeWords())
                break;
            TransitionDef t;
            t.from = areaIdOf(rom.u16(a));
            t.to = areaIdOf(rom.u16(a + 1));
            t.spawn = {int(rom.u16(a + 5)), int(rom.u16(a + 6))};
            t.flags = rom.u16(a + 7);
            if (db.transition(t.from, t.to))
                continue; // duplicate: first row wins
            if (const AreaDef *dest = db.area(t.to))
            {
                char ts[48];
                std::snprintf(ts, sizeof ts, "base:tileset/%d@%x", int(rom.u16(a + 2)), int(rom.u16(a + 3)));
                const Id cs = "base:collision/" + std::to_string(rom.u16(a + 4));
                if (dest->tileset != ts || dest->collision != cs)
                {
                    std::printf("import: transition %d->%d uses tileset/collision different from area %d's own\n", int(rom.u16(a)), int(rom.u16(a + 1)), int(rom.u16(a + 1)));
                    ++warn;
                }
            }
            db.put(std::move(t));
        }
        if (!warn)
            std::printf("import: all transitions agree with their destination area's tileset/collision\n");
    }

    // Exits come from the two code if-chains (exits.hpp). Walk every object of every area through them.
    inline void importExits(const Rom &rom, ContentDB &db)
    {
        (void)rom;
        const int n = areaCount(rom);
        for (int id = 0; id < n; ++id)
        {
            const AreaDef *a = db.areaByLegacy(id);
            if (!a)
                continue;
            for (size_t k = 0; k < a->objects.size(); ++k)
            {
                if (const InteractExit *ie = findInteractExit(id, int(k)))
                {
                    ExitDef e;
                    e.id = a->id + "/exit/" + std::to_string(k) + "/use";
                    e.fromArea = a->id;
                    e.object = a->objects[k].id;
                    e.toArea = areaIdOf(ie->to);
                    e.onTouch = false;
                    db.put(std::move(e));
                }
                if (const StoryExit *se = findStoryExit(id, int(k)))
                {
                    ExitDef e;
                    e.id = a->id + "/exit/" + std::to_string(k) + "/touch";
                    e.fromArea = a->id;
                    e.object = a->objects[k].id;
                    e.toArea = areaIdOf(se->spawnReason); // walk.cpp treats spawnReason as the destination area
                    e.onTouch = true;
                    e.flag1a9d = se->flag1a9d;
                    e.consumeAndSpawn = se->consumeAndSpawn;
                    if (se->flag1a9d == 0 && id == 6 && se->spawnReason == 0)
                    {
                        e.hasSpawnOverride = true;
                        e.spawnOverride = {0x84c, 0xb9};
                    }
                    if (se->flag1a9d == 0 && id == 0 && se->spawnReason == 6)
                    {
                        e.hasSpawnOverride = true;
                        e.spawnOverride = {0x893, 0x34c};
                    }
                    db.put(std::move(e));
                }
            }
        }
    }

    // Object id for a legacy (area, slot) pair, or empty if the area/slot doesn't exist.
    inline Id objectIdAt(const ContentDB &db, int legacyArea, int slot)
    {
        if (const AreaDef *a = db.areaByLegacy(legacyArea))
            for (const ObjectDef &o : a->objects)
                if (o.legacySlot == slot)
                    return o.id;
        return {};
    }

    inline void putInteraction(ContentDB &db, int legacyArea, int slot, const char *action,
                               std::map<std::string, int> num = {}, std::map<std::string, std::string> str = {})
    {
        const AreaDef *a = db.areaByLegacy(legacyArea);
        const Id obj = objectIdAt(db, legacyArea, slot);
        if (!a || obj.empty())
        {
            std::printf("import: interaction at area %d slot %d has no object\n", legacyArea, slot);
            return;
        }
        InteractionDef d;
        d.id = obj + "/interact";
        d.area = a->id;
        d.object = obj;
        d.action = action;
        d.num = std::move(num);
        d.str = std::move(str);
        db.put(std::move(d));
    }

    // A ROM string by Ghidra word address (one char per word), stored as "base:str/rom_<hex>".
    // Same read as the old readRomString in walk.cpp, so output is identical.
    inline Id importRomString(const Rom &rom, ContentDB &db, uint32_t addr)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "base:str/rom_%x", addr);
        const Id id = buf;
        if (!db.text(id))
        {
            std::string s;
            for (int c; (c = rom.u16(addr++) & 0xff) != 0 && s.size() < 100;)
                s += char(c);
            db.put(TextDef{id, std::move(s)});
        }
        return id;
    }

    // The (area, slot) tables from ProcessStoryModeObjectInteraction, now as records.
    inline void importInteractions(const Rom &rom, ContentDB &db)
    {
        auto str = [&](uint32_t addr)
        { return importRomString(rom, db, addr); };

        // Wishing wells (Three in a Row): cost, prize landing point, and the two prompts
        auto well = [&](int area, int slot, int cost, int x, int y)
        {
            const bool big = cost >= 100;
            putInteraction(db, area, slot, "well", {{"cost", cost}, {"x", x}, {"y", y}},
                           {{"needText", str(big ? 0xd552 : 0xd4ff)}, {"askText", str(big ? 0xd575 : 0xd520)}});
        };
        well(0, 17, 5, 0x1ea, 0x159);
        well(0, 78, 5, 0x79a, 0x3fc);
        well(1, 2, 5, 0x50c, 0x9a);
        well(6, 15, 100, 0x10f, 0x17c);

        // Game tents (Memory Match)
        for (const auto &t : {std::array<int, 4>{3, 8, 0x8ea, 0x69}, {7, 50, 0x4f5, 0x550}})
            putInteraction(db, t[0], t[1], "tent", {{"x", t[2]}, {"y", t[3]}},
                           {{"needText", str(0xd5a6)}, {"askText", str(0xd5e4)}});

        // Swift Pick
        putInteraction(db, 0, 98, "minigame", {}, {{"game", "swift_pick"}});

        // The player homes and the hotels. These were never minigames: the door sets g_storyInteractionResultCode to the
        // location (0..2 homes, 4..7 hotels) and switches to the room state (ProcessStoryModeObjectInteraction).
        struct Door
        {
            int area, slot, location;
        };
        for (const Door &s : {Door{0, 63, 0}, {0, 96, 1}, {0, 81, 2}, {2, 17, 4}, {3, 47, 5}, {6, 48, 6}, {1, 81, 7}})
            putInteraction(db, s.area, s.slot, "room", {{"location", s.location}});

        // House signs in area 0: object slot -> save slot
        for (const auto &h : {std::array<int, 2>{62, 0}, {94, 1}, {75, 2}})
            putInteraction(db, 0, h[0], "house_sign", {{"saveSlot", h[1]}},
                           {{"emptyText", str(0xd2b7)}, {"fullText", str(0xd2c0)}}); // "FOR RENT" / "Welcome to the home of %s."

        // The colour machine in the spaceship (area 8, object 1), and the island map sign in Gigatown (area 0, object 0x38).
        putInteraction(db, 8, 1, "color_machine");
        putInteraction(db, 0, 0x38, "map");

        // The five stat / money guards in area 5 (stat_gate.hpp): the guard talks, its barrier goes once the condition holds.
        for (const StatGate &g : kStatGates)
        {
            putInteraction(db, kGateArea, g.guard, "message", {}, {{"text", "base:text/" + std::to_string(g.text)}});
            if (const AreaDef *a = db.areaByLegacy(kGateArea))
                for (ObjectDef &o : db.areaMutable(a->id)->objects)
                    if (o.legacySlot == g.barrier)
                        o.goneOnFlag = gateFlag(kGateArea, g.barrier);
        }

        // Quest-count gates: unlocked text once questsCompleted >= needed
        struct Gate
        {
            int area, slot, needed;
            uint32_t locked, open;
        };
        for (const Gate &g : {Gate{3, 57, 17, 0xd4b0, 0xd4dd},  // Vacation Island
                              Gate{7, 56, 61, 0xd328, 0xd350},  // Bridge
                              Gate{1, 12, 90, 0xd413, 0xd449},  // Magma Mountain
                              Gate{0, 8, 91, 0xd4b0, 0xd4dd},   // GUESS: Vacation Island, strings not yet seen
                              Gate{0, 46, 4, 0xd2db, 0xd316},   // Gigatown
                              Gate{0, 33, 42, 0xd368, 0xd39a},  // Gigalympia tunnel
                              Gate{1, 29, 16, 0xd3b0, 0xd3d9},  // Giga Castle
                              Gate{1, 91, 14, 0xd463, 0xd48d}}) // Beach
            putInteraction(db, g.area, g.slot, "gate_sign", {{"needed", g.needed}},
                           {{"lockedText", str(g.locked)}, {"openText", str(g.open)}});
    }

    // Scripted quests and the generator tables, read once from the ROM.
    inline void importQuests(const Rom &rom, ContentDB &db)
    {
        auto rw = [&](uint32_t a)
        { return int(rom.u16(int(a))); };
        for (QuestDef &q : importQuestTable(rw))
            db.put(std::move(q));
        db.put(importQuestTables(rw));
    }

    inline void importQuestTypes(ContentDB &db)
    {
        for (QuestTypeDef &t : baseQuestTypes())
            db.put(std::move(t));
    }

    inline void importMinigames(ContentDB &db)
    {
        for (MinigameDef &m : baseMinigames())
            db.put(std::move(m));
    }

    inline void importBefriendRules(ContentDB &db) { db.put(baseBefriendRules()); }

    inline void importQuestStrings(ContentDB &db)
    {
        for (const auto &kv : kQuestStrings)
            db.put(TextDef{std::string("base:str/quest/") + kv.first, kv.second});
    }

    // Objects that exist only while questMin <= quests completed < questMax (was applyQuestGates).
    inline void importQuestGates(ContentDB &db)
    {
        constexpr int kAny = 0x7fffffff;
        struct Gate
        {
            int area, slot, minQ, maxQ;
        };
        static const Gate kGates[] = {
            {0, 0x32, 4, kAny},
            {0, 0x17, 0x2a, kAny},
            {0, 6, 0x5b, kAny},
            {0, 0x19, 0x2a, kAny},
            {0, 0x1a, 0, 0x2a},
            {0, 4, 0x5b, kAny},
            {0, 5, 0, 0x5b},
            {1, 5, 0, 0x5a},
            {1, 4, 0x5a, kAny},
            {1, 0x17, 0x10, kAny},
            {1, 0x5d, 0xe, kAny},
            {1, 3, 0x5a, kAny},
            {3, 0x3a, 0x11, kAny},
            {7, 0x3b, 0, 0x3d},
            {9, 0xb8, 0x43, kAny},
            {9, 0xb9, 0, 0x43},
        };
        for (const Gate &g : kGates)
        {
            const AreaDef *a = db.areaByLegacy(g.area);
            AreaDef *m = a ? db.areaMutable(a->id) : nullptr;
            bool found = false;
            if (m)
                for (ObjectDef &o : m->objects)
                    if (o.legacySlot == g.slot)
                    {
                        o.questMin = g.minQ;
                        o.questMax = g.maxQ;
                        found = true;
                    }
            if (!found)
                std::printf("import: quest gate at area %d slot %d has no object\n", g.area, g.slot);
        }
    }

    inline void importBaseOfferSets(ContentDB &db)
    {
        auto text = [&](const char *id, const char *s)
        { TextDef t; t.id = id; t.text = s; db.put(std::move(t)); };
        text("base:str/offer/king_thanks", "Oh, thank you returning this. It will be placed in my royal vault.");
        text("base:str/offer/gardenia_thanks", "Oh, thank you for this beautiful flower. It will go right in my garden.");

        auto range = [](int a, int b)
        { std::vector<Id> v; for (int i = a; i <= b; ++i) v.push_back(legacyItemId(i)); return v; };

        OfferSetDef k;
        k.id = "base:offers/king";
        k.npc = "base:npc/73"; // 0x49, check the id format
        k.items = range(0xaa, 0xb5);
        k.thanksText = "base:str/offer/king_thanks";
        k.autoMarkAfterQuests = 0x11;
        k.autoMarkItem = legacyItemId(0xaa);
        k.rewardItems = {0x10a, 3, 0xb, 0xf, 4, 0xc, 0x10, 0xd, 0x11, 0x5e, 0x14, 0x108}; // RAM 0x1e36..0x1e41, read in MAME
        db.put(std::move(k));

        OfferSetDef g;
        g.id = "base:offers/gardenia";
        g.npc = "base:npc/64"; // 0x40
        g.items = range(0xdf, 0xea);
        g.thanksText = "base:str/offer/gardenia_thanks";
        g.rewardItems = {0x10a, 0x107, 3, 0xb, 0xf, 4, 0xc, 0x10, 0xd, 0x11, 0x5f, 0x15}; // RAM 0x1e2a..0x1e35, read in MAME
        db.put(std::move(g));
    }

    inline void importOfferGates(ContentDB &db)
    {
        // object row (legacySlot) hidden when item i of the set has been handed over
        static constexpr int kGardenia[12] = {0x18, 0x16, 0xa, 0xe, 0x17, 0xf, 0x12, 0xd, 0x19, 0x1a, 0x13, 9};
        static constexpr int kKing[12] = {8, 0xb, 0xa, 0xc, 9, 5, 3, 4, 1, 2, 7, 6};
        struct G
        {
            int area;
            const char *set;
            const int *rows;
        };
        for (const G &g : {G{0x1c, "base:offers/king", kKing}, G{0x22, "base:offers/gardenia", kGardenia}})
        {
            const AreaDef *a0 = db.areaByLegacy(g.area);
            const OfferSetDef *s = db.offerSet(g.set);
            if (!a0 || !s)
                continue;
            AreaDef *a = db.areaMutable(a0->id);
            for (int i = 0; i < 12 && i < int(s->items.size()); ++i)
                for (ObjectDef &o : a->objects)
                    if (o.legacySlot == g.rows[i])
                        o.requiresOffered = s->items[size_t(i)];
        }

        int n = 0;
        for (const AreaDef *a : {db.areaByLegacy(0x1c), db.areaByLegacy(0x22)})
            if (a)
                for (const ObjectDef &o : a->objects)
                    if (!o.requiresOffered.empty())
                        ++n;
        std::fprintf(stderr, "offer gates set: %d (expect 24)\n", n);
    }

    inline void importRom(const Rom &rom, ContentDB &db, int playerSpecies = 0)
    {
        importAreas(rom, db);
        importQuestGates(db);
        importQuests(rom, db);
        importQuestTypes(db);
        importBefriendRules(db);
        importMinigames(db);
        importQuestStrings(db);
        importTransitions(rom, db);
        importExits(rom, db);
        importInteractions(rom, db);
        importAnimRange(rom, db, 0, 0x120, 0x1e1, playerSpecies);
        if (auto t = findTextPool(rom))
            importText(db, rom, *t);
        else
            std::printf("warning: name table not found, no text imported\n");
        importWideStrings(db, rom);
        importFont(db, rom, 0);        // dialogue text
        importFont(db, rom, 1, 0x11f); // sign text: font 1, palette 0x11f
        importFont(db, rom, 2);        // stat numbers
        importFont(db, rom, 3, 0x104); // Swift Pick title: font 3, palette 0x104
        for (int f = 0; f < 8; ++f)    // import all fonts in-case
            importFont(db, rom, f);
        for (int s : {0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4e, 0x4f, 0x27, 0xac2, 0xace, 0xad6, 0xac6, 0x400})
            importUiSprite(db, rom, s);
        importUiSprite(db, rom, 0x17, 0x11f); // sign frame
        importSpecies(db);
        importNpcs(rom, db, playerSpecies);
        importBaseOfferSets(db);
        importOfferGates(db);
        importItems(rom, db);
        for (const FixedSpotDef &s : baseFixedSpots())
            db.put(s);
    }

} // namespace gp