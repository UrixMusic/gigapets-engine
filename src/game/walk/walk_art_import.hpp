// walk_art_import.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of WalkArt.
// What they need from the rest of the walk session comes in through WalkArtEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct WalkArtEnv
{
    gp::ContentDB &db;
    const std::vector<gp::ItemDef>&itemDefs;
    gp::Rom&rom;
    const gp::TrickLayout &trickLay;
};

class WalkArt : public WalkArtEnv
{
public:
    explicit WalkArt(WalkArtEnv env) : WalkArtEnv(std::move(env)) {}

    // The pictures the walking world draws that are not part of an area: bubbles, icons, shadows, tilesets of the rooms, the bar cursor (imported once, up front)
    auto importWalkArt()
    {
    for (int a : {0xe, 0x10, 0x11, 0x12, 0x16, 0x17, 0x18, gp::kTabAnimIn, gp::kTabAnimOut, gp::kBlastAnim, gp::kStinkAnim, gp::kExclaimIntroAnim, gp::kExclaimOutroAnim, trickLay.questionBubble, trickLay.answerBubble, gp::kHaloBubble, gp::kPitchforkBubble, gp::kStarBubble}) // 0xe: the wash bubble; arrow, dig bubble; 0x17/0x18: the thought bubble
        try
        {
            gp::importAnim(rom, db, a, 0x28c);
        }
        catch (const std::exception &)
        {
        }
    for (int s = gp::kTrickIconFirst; s <= gp::kTrickIconLast; ++s) // the tricks panel's icons and placeholders
        try
        {
            gp::importUiSprite(db, rom, s, 0x28c);
        }
        catch (const std::exception &)
        {
        }
    for (int s : {gp::kMapMarkerTile, 0x9c1, 0x9c2, gp::kThoughtHungerIcon, gp::kThoughtTiredIcon, gp::kThoughtSickIcon, gp::kThoughtMoodIcon}) // flower, dug hole, the thought bubble icons
        try
        {
            gp::importUiSprite(db, rom, s, 0x28c);
        }
        catch (const std::exception &e)
        {
            std::printf("flower/hole sprite 0x%x import failed: %s\n", s, e.what());
        }
    for (const gp::StatNoticeSlot &sl : gp::kStatNoticeSlots) // the stat-change panel's icons and backdrop
        try
        {
            gp::importUiSprite(db, rom, sl.tile, sl.palette);
            gp::importUiSprite(db, rom, gp::kStatNoticeBg, 0x28c);
        }
        catch (const std::exception &)
        {
        }
    try
    {
        gp::importUiSprite(db, rom, gp::kMapTile, gp::kMapPalette); // the island map, full screen
    }
    catch (const std::exception &e)
    {
        std::printf("map sprite import failed: %s\n", e.what());
    }
    for (int s = 0x994; s <= 0x9ab; ++s) // DrawShadow blobs: 12 normal + 12 for species 2
        try
        {
            gp::importUiSprite(db, rom, s, 0x28c);
        }
        catch (const std::exception &e)
        {
            std::printf("shadow sprite 0x%x import failed: %s\n", s, e.what());
        }

    // Memory Match: card back / face / cursor are palette 0x28c (DrawTile(0x471, 0x28c, ...)); item icons use the
    // menu palette and are already imported by the iconSprite loop.
    for (int s : {gp::MemoryMatch::kSpriteBack, gp::MemoryMatch::kSpriteFace, gp::MemoryMatch::kSpriteCursor})
        try
        {
            gp::importUiSprite(db, rom, s, 0x28c);
        }
        catch (const std::exception &e)
        {
            std::printf("memory match sprite 0x%x import failed: %s\n", s, e.what());
        }
    try
    {
        gp::importAnim(rom, db, gp::MemoryMatch::kFlipAnim, gp::MemoryMatch::kFlipAnimPal);
    }
    catch (const std::exception &)
    {
    }

    gp::importRoomTilesets(rom, db); // the wall and floor tilesets of the home and hotel rooms
    {
        // The room's own pieces (door, hooks, ...) and the sprites of every furnishing item: sprite ids, not anim ids.
        auto importDecor = [&](int sprite, int palette)
        {
            if (sprite <= 0 || sprite == gp::kNoSprite || gp::isModSprite(sprite)) // (a mod item's picture is already registered)
                return;
            try
            {
                gp::importUiSprite(db, rom, sprite, palette);
            }
            catch (const std::exception &e)
            {
                std::printf("room sprite 0x%x import failed: %s\n", sprite, e.what());
            }
        };
        for (int loc : {0, 4})
            for (const gp::RoomDecoration &d : gp::roomDecorations(rom, loc))
                importDecor(d.sprite, d.palette);
        for (int loc = 0; loc <= 7; ++loc) // what a mod's `room` patch put in the tables
            if (db.room(loc))
                for (const gp::RoomDecoration &d : gp::roomDecorations(rom, loc, db.room(loc)))
                    importDecor(d.sprite, d.palette);
        for (int marker : {0x33d, 0x3b0, 0x319}) // the "put it here" markers while placing a furnishing
            importDecor(marker, 0x28c);
        for (int bowl : {0x95d, 0x95e, 0x963, 0x961}) // the food in the bowl, full to empty
            importDecor(bowl, 0x28c);
        for (const gp::ItemDef &it : itemDefs)
            if (it.type >= 3 && it.type <= 8)
            {
                importDecor(it.decorSprite, it.w7);
                importDecor(it.w9, it.w7);
            }
    }

    // Trick Teacher game: slot frame and cursor (0x478, 0x400) are already in; add the check and the cross.
    for (int s : {gp::TrickGuess::kSpriteSlot, gp::TrickGuess::kSpriteCursor, gp::TrickGuess::kSpriteRight, gp::TrickGuess::kSpriteWrong})
        try
        {
            gp::importUiSprite(db, rom, s, gp::TrickGuess::kSpritePal);
        }
        catch (const std::exception &e)
        {
            std::printf("trick guess sprite 0x%x import failed: %s\n", s, e.what());
        }

    // Bar cursor sprites: 8 words per button (normal rec, pressed rec), 4 words each: sprite, pal, x, y
    for (bool house : {false, true})
    {
        const int base = house ? gp::kBarCursorHouse : gp::kBarCursorOutside;
        const int buttons = house ? 8 : 7;
        for (int b = 0; b < buttons; ++b)
            for (int rec = 0; rec < 2; ++rec)
            {
                const int w = base + b * 8 + rec * 4;
                int pal = rom.u16(w + 1);
                if (pal == 0xffff)
                    pal = gp::kMenuPalette;
                gp::importUiSprite(db, rom, rom.u16(w), pal);
            }
    }

    {
        const int b = gp::kSlotTable + 0x1e * 9;
        const int defPal = rom.u16(b), recs = rom.u16(b + 2), list = rom.u16(b + 3), n = rom.u16(b + 4);
        std::printf("slot 0x1e: defPal 0x%x bg 0x%x shift 0x%x count %d\n", defPal, rom.u16(b + 1), rom.u16(b + 5), n);
        for (int i = n - 1; i >= 0; --i)
        {
            const int idx = rom.u16(list + i);
            if (idx == 0xffff)
                continue;
            const int r = recs + idx * 4;
            std::printf("  rec %d: sprite 0x%x pal 0x%x at (%d,%d) layer 0x%x\n",
                        idx, rom.u16(r), rom.u16(r + 1), rom.u16(r + 2), rom.u16(r + 3), gp::slotLayer(idx));
        }
    }
    }
};
