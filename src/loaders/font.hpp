#pragma once
#include <cstdio>
#include "../content/db.hpp"
#include "rom.hpp"
#include "sprite.hpp"

namespace gp
{

    struct FontSpec
    {
        int tile0, wtab, glyphs, space;
    };
    constexpr FontSpec kFontSpec{0xae1, 0xe6c5, 95, 10}; // font n = glyph sprites n*95.., advances at wtab + n*95..
    constexpr int kLineHeightTable = 0xe6bd;             // DrawFormattedText: y += u16(0xe6bd + font) per line
    constexpr int kDialoguePalette = 0x28c;              // what the dialogue code passes to DrawTextBox

    inline Id fontId(int fontIndex, int palette = kDialoguePalette)
    {
        char b[48];
        std::snprintf(b, sizeof b, "base:font/%d@%x", fontIndex, palette);
        return b;
    }
    inline Id uiSpriteId(int spriteId, int palette = kDialoguePalette)
    {
        char b[48];
        std::snprintf(b, sizeof b, "base:sprite/%x@%x", spriteId, palette);
        return b;
    }

    inline bool decodeSpriteFrame(const Rom &rom, int spriteId, int paletteId, SpriteFrame &out,
                                  bool swapRB = false)
    {
        DecodedSprite s;
        if (!decodeSprite(rom, spriteId, s))
            return false;
        Palette pal;
        const Palette *pp = readPalette(rom, paletteId, pal) ? &pal : nullptr;
        out = SpriteFrame{};
        out.w = s.w;
        out.h = s.h;
        out.anchorX = s.anchorX;
        out.anchorY = s.anchorY;
        out.dx = -s.originX;
        out.dy = -s.originY;
        toRGBA(s, pp, swapRB, out.rgba);
        return true;
    }

    inline void importFont(ContentDB &db, const Rom &rom, int fontIndex,
                           int palette = kDialoguePalette, const FontSpec &s = kFontSpec)
    {
        FontDef f;
        f.id = fontId(fontIndex, palette);
        f.space = s.space;
        f.lineHeight = rom.u16(kLineHeightTable + fontIndex);
        for (int k = 0; k < s.glyphs; k++)
        {
            const int idx = fontIndex * s.glyphs + k;
            SpriteFrame fr;
            if (!decodeSpriteFrame(rom, s.tile0 + idx, palette, fr))
                fr = SpriteFrame{};
            f.glyph.push_back(std::move(fr));
            f.adv.push_back(rom.u16(s.wtab + idx));
        }
        db.put(std::move(f));
    }

    // One sprite as a one-frame Anim, so the existing anim texture cache in walk.cpp can draw it.
    inline void importUiSprite(ContentDB &db, const Rom &rom, int spriteId, int palette = kDialoguePalette)
    {
        SpriteHeader h;
        if (!readSpriteHeader(rom, spriteId, h))
        {
            return;
        }
       // std::printf("UI 0x%x: hdr w=%d h=%d first=%d count=%d\n", spriteId, h.w, h.h, h.firstPiece, h.pieceCount);
        SpriteFrame fr;
        if (!decodeSpriteFrame(rom, spriteId, palette, fr))
        {
          //  std::printf("UI 0x%x: decode FAILED\n", spriteId);
            return;
        }
       // std::printf("UI 0x%x: ok %dx%d anchor(%d,%d) d(%d,%d)\n", spriteId, fr.w, fr.h, fr.anchorX, fr.anchorY, fr.dx, fr.dy);
        Anim a;
        a.id = uiSpriteId(spriteId, palette);
        a.frames.push_back(std::move(fr));
        db.put(std::move(a));
    }

} // namespace gp