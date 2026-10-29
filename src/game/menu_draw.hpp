// game/menu_draw.hpp
// Ports of three draw functions used by ItemMenuScreen:
//   DrawItemGridPage      -> drawItemGrid   (6x3 icon grid, selection, badges, arrows, name box)
//   DrawCategoryTabIcon   -> drawTabMarker  (tab bar backdrop + the current tab's icon)
//   DrawActiveSlotSprites -> drawSlotScene  (data-driven screen frame from the 9-word slot table)
// Draw only: no input, no state. Rom::u16 takes WORD addresses here, same as item.hpp.
// DrawTile/DrawTextBox parameter names are guesses from call sites until those are decompiled.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "item.hpp"
#include "rom.hpp"

namespace gp
{
    constexpr int kMenuPalette = 0x28c; // literal 2nd arg of nearly every menu DrawTile
    constexpr int kMenuDepth = 999;     // literal 6th arg of every DrawTile call seen

    struct Gfx
    {
        virtual ~Gfx() = default;
        virtual void drawTile(int sprite, int palette, int x, int y, int layer, int depth = kMenuDepth) = 0;
        // (style, palette, x, y, text, width, mode, layer); meanings guessed from call sites
        virtual void drawTextBox(int style, int palette, int x, int y, const std::string &text,
                                 int width, int mode, int layer) = 0;
        virtual int getSpriteWidth(int sprite) { return 0; } // the game's GetSpriteWidth
    };

    // ---------------------------------------------------------------- item grid
    struct GridView
    {
        const std::vector<int> *list = nullptr;   // item ids in the visible window (Item_BuildFilteredList)
        int count = 0;                            // valid entries in list
        int firstRow = 1, selRow = 1, selCol = 1; // rows 1-based, col 1..6
        int totalRows = 1;                        // (count-1)/6 + 1 over the whole filtered list
        int variant = 0;                          // param_7: 1 hides cols 2-5, 2 hides cols 3-4 of rows 1-2
        int category = 0;                         // g_itemMenuCategory (2 = medicine, has an empty-state text)
        int starterPalette = kMenuPalette;        // g_starterExtraField, used for items 0x5d..0x5f
        bool miniPetConnected = false;
    };

    // owned(id): the count shown in the badge (mode 4 uses Item_GetUniqueSlotCounter instead)
    // itemName(id): decoded name text (100-char buffer rules apply)
    inline void drawItemGrid(Gfx &g, const std::vector<ItemDef> &defs, const GridView &v,
                             const std::function<int(int)> &owned,
                             const std::function<std::string(int)> &itemName,
                             const std::string &emptyText /* string at 0xb2e4 */)
    {
        int idx = 0, y = 0x2f;
        for (int row = 1; row <= 3; ++row, y += 0x32)
        {
            int x = 0x0f;
            // hidden cells still consume a list position, exactly like the original
            for (int col = 1; col <= 6; ++col, x += 0x32, ++idx)
            {
                const bool hidden = (v.variant == 1 && col != 1 && col != 6) ||
                                    (v.variant == 2 && row <= 2 && col >= 3 && col <= 4);
                if (hidden)
                    continue;
                if (idx >= v.count)
                {
                    g.drawTile(0x40a, kMenuPalette, x, y, 0x1000); // empty cell
                    continue;
                }
                const int id = (*v.list)[idx];
                if (row == v.selRow - v.firstRow + 1 && col == v.selCol)
                    g.drawTile(0x400, kMenuPalette, x, y, 0x1000); // selection frame, under the icon
                const int pal = (id >= 0x5d && id <= 0x5f) ? v.starterPalette : kMenuPalette;
                g.drawTile(defs[id].iconSprite, pal, x, y, 0x1000);
                const int n = owned(id);
                if (n > 1 && !v.miniPetConnected) // format string lives at 0xb2e0; "%d" assumed
                    g.drawTextBox(7, kMenuPalette, x, y + 0x1b, "x" + std::to_string(n), 0x28, 2, 0x1000);
            }
        }
        if (v.firstRow > 1)
            g.drawTile(0x3f4, kMenuPalette, 0x118, 0x0f, 0x1000); // scroll up
        if (v.firstRow + 2 < v.totalRows)
            g.drawTile(0x3f2, kMenuPalette, 0x118, 0xde, 0x1000); // scroll down

        if (v.count < 1)
        {
            if (v.category == 2)
                g.drawTextBox(0, kMenuPalette, 0x32, 0xc5, emptyText, 0xdc, 1, 0x3000);
        }
        else
        {
            const int sel = (v.selRow - v.firstRow) * 6 + v.selCol - 1;
            if (sel >= 0 && sel < v.count)
                g.drawTextBox(0, kMenuPalette, 0x32, 0xc5, itemName((*v.list)[sel]), 0xdc, 1, 0x3000);
        }
    }

    // ---------------------------------------------------------------- money
    // ItemMenuScreen draws this right after the slot scene, whenever g_itemMenuMode is not 3 or 4 (the mini-pet transfer
    // modes): the money icon (tile 0xacb), then the amount printed with "%ld" (string at 0xb2dc) in font 2, left aligned,
    // in a 0x32 px wide box. The amount is GetPlayerMoney(), a 32-bit value.
    constexpr int kMoneyIconSprite = 0xacb, kMoneyIconX = 0x14, kMoneyIconY = 0x23;
    constexpr int kMoneyTextFont = 2, kMoneyTextX = 0x20, kMoneyTextY = 0x14, kMoneyTextW = 0x32;
    inline void drawMoney(Gfx &g, int money)
    {
        g.drawTile(kMoneyIconSprite, kMenuPalette, kMoneyIconX, kMoneyIconY, 0x1000);
        g.drawTextBox(kMoneyTextFont, kMenuPalette, kMoneyTextX, kMoneyTextY, std::to_string(money), kMoneyTextW, 0, 0x1000);
    }

    // ---------------------------------------------------------------- tab marker
    // Per tab: 3 records x 4 words (sprite, palette, x, y) starting at 0xb070:
    //   +0 normal, +4 focused, +8 pressed.   5 tabs -> 60 words, directly after the potion table.
    constexpr int kTabTable = 0xb070;
    enum class MenuSub
    {
        Grid = 0,
        ToTabs = 1,
        Tabs = 2,
        ToGrid = 3
    }; // ItemMenuScreen's iStack_28

    struct TabRec
    {
        int sprite, palette, x, y;
    };

    inline TabRec tabRec(const Rom &rom, int tab, int which)
    {
        const int b = kTabTable + tab * 12 + which * 4;
        return {rom.u16(b), rom.u16(b + 1), rom.u16(b + 2), rom.u16(b + 3)};
    }

    // animField7: word 7 of the slide animation state (probably its frame index); the icon is
    // only drawn while it is <= 4 during the transitions. Pass -1 when not animating.
    inline void drawTabMarker(Gfx &g, const Rom &rom, int tab, bool pressed, MenuSub sub, int animField7)
    {
        TabRec r;
        if (sub == MenuSub::Tabs)
        {
            g.drawTile(0x40b, kMenuPalette, 0x5a, 0x0c, 0x3000); // focused backdrop
            r = tabRec(rom, tab, pressed ? 2 : 1);
        }
        else if (sub == MenuSub::ToTabs || sub == MenuSub::ToGrid)
        {
            // the slide animation (anim 0x5d in, 0x5c out) is drawn by the animation system
            if (animField7 > 4)
                return;
            r = tabRec(rom, tab, 0);
        }
        else
        {
            g.drawTile(0x40c, kMenuPalette, 0x5a, 0x0c, 0x3000); // unfocused backdrop
            r = tabRec(rom, tab, 0);
        }
        g.drawTile(r.sprite, r.palette, r.x, r.y, 0x3000);
    }

    // ---------------------------------------------------------------- slot scene
    // Slot table at 0x21db, 9 words per slot (only words 0-5 are read by DrawActiveSlotSprites):
    //   +0 default palette   +1 BG param   +2 -> record table (4 words each: sprite, palette|0xffff, x, y)
    //   +3 -> list of record indices (0xffff = skip)   +4 count   +5 BG shift (<<3)
    // Records are drawn from the last list entry down to the first.
    constexpr int kSlotTable = 0x21db;

    inline int slotLayer(int idx)
    {
        if (idx == 0x1a || idx == 0x3f || (idx >= 0x30 && idx <= 0x37))
            return 0x2000;
        if ((idx >= 0x14 && idx <= 0x16) || idx == 0x78 || (idx >= 0x4a && idx <= 0x50) ||
            idx == 0x19 || idx == 0x76 || idx == 0x2a || (idx >= 0x46 && idx <= 0x48))
            return 0x1000;
        return 0;
    }

    inline void drawSlotScene(Gfx &g, const Rom &rom, int slot)
    {
        const int b = kSlotTable + slot * 9;
        const int defPal = rom.u16(b + 0);
        const int recs = rom.u16(b + 2);
        const int list = rom.u16(b + 3);
        const int n = rom.u16(b + 4);
        // not ported: the BG layer setup done from words +1 and +5, and ResolveActiveSlotPalette(defPal)
        for (int i = n - 1; i >= 0; --i)
        {
            const int idx = rom.u16(list + i);
            if (idx == 0xffff)
                continue;
            const int r = recs + idx * 4;
            int pal = rom.u16(r + 1);
            if (pal == 0xffff)
                pal = defPal;
            g.drawTile(rom.u16(r), pal, rom.u16(r + 2), rom.u16(r + 3), slotLayer(idx));
        }
    }

    // Bar cursor tables (unverified layout: 8 words per button = normal rec, pressed rec).
    constexpr int kBarCursorHouse = 0xf5e8, kBarCursorOutside = 0xf628;

    inline void drawBarCursor(Gfx &g, const Rom &rom, bool inHouse, int sel, bool pressed)
    {
        const int b = (inHouse ? kBarCursorHouse : kBarCursorOutside) + sel * 8 + (pressed ? 4 : 0);
        int pal = rom.u16(b + 1);
        if (pal == 0xffff)
            pal = kMenuPalette;
        g.drawTile(rom.u16(b), pal, rom.u16(b + 2), rom.u16(b + 3), 0x3000);
    }
    struct StatsView
    {
        int species = 0;               // g_playerSpecies
        int petPalette = kMenuPalette; // g_petDisplayAppearanceIdSaved
        std::string name, hunger, happiness, cleanliness, health, tiredness;
        int tricksKnown = 0;
        bool healthy = true;
        std::string illness;                                     // shown when !healthy
        int personality[4] = {};                                 // halos, pitchforks, stars, lightbulbs
        int questPortrait = -1, questPortraitPal = kMenuPalette; // -1 = text-only layout
        std::string questText;
    };

    inline constexpr const char *kIllnessNames[10] = {
        "Dog Breath",   // 0
        "Gone Bananas", // 1
        "Panda Pox",    // 2
        "Cold",         // 3
        "Molting",      // 4
        "Brain Freeze", // 5
        "Cold Feet",    // 6
        "Brain Ache",   // 7
        "The Oogies",   // 8
        "Cooties"       // 9
    };

    constexpr int kStatsSlot = 0x20, kStatsBgPalette = 0x5de;

    inline void drawPetStats(Gfx &g, const Rom &rom, const StatsView &v, const std::string &illnessHeader)
    {
        drawSlotScene(g, rom, kStatsSlot);
        g.drawTile(0x982 + (v.species > 2 ? 2 : v.species), v.petPalette, 0x3b, 0x5c, 0x3000);

        auto label = [&](int x, int y, const std::string &t, int w = 0x69)
        { g.drawTextBox(2, kMenuPalette, x, y, t, w, 1, 0x3000); };
        label(0, 0x62, v.name, 0x78);
        label(0x62, 0x20, v.hunger);
        label(0xd2, 0x20, v.happiness);
        label(0x62, 0x41, v.cleanliness);
        label(0xd2, 0x41, std::to_string(v.tricksKnown) + " Known"); // "%d Known" at 0xf100
        if (v.healthy)
            label(0x62, 0x62, v.health);
        else
        {
            label(0x62, 0x62, illnessHeader); // string at 0xf109
            label(0x62, 0x6e, v.illness);
        }
        label(0xd2, 0x62, v.tiredness);

        struct Cell
        {
            int icon, x, iconY, numY;
        };
        static const Cell cells[4] = {{0xac2, 0xc6, 0x85, 0x91}, {0xace, 0xc6, 0xa8, 0xb4}, {0xad6, 0x100, 0x85, 0x91}, {0xac6, 0x100, 0xa8, 0xb4}};
        for (int i = 0; i < 4; ++i)
        {
            const Cell &c = cells[i];
            g.drawTile(c.icon, kMenuPalette, c.x, g.getSpriteWidth(c.icon) + c.iconY, 0x1000);
            const int dx = v.personality[i] <= 999 ? 0x15 : 0x10;
            g.drawTextBox(2, kMenuPalette, c.x + dx, c.numY, std::to_string(v.personality[i]), 0x28, 0, 0x1000);
        }
        if (v.questPortrait >= 0)
        {
            g.drawTile(v.questPortrait, v.questPortraitPal, 0xf, 0x91, 0x3000);
            g.drawTextBox(2, kMenuPalette, 0x3c, 0x91, v.questText, 0x73, 0, 0x3000);
        }
        else if (!v.questText.empty())
            g.drawTextBox(2, kMenuPalette, 0xf, 0x91, v.questText, 0xa0, 0, 0x3000);
    }

    // ---- label tables: 3 words per record = min, max (inclusive), string pointer
    struct LabelTable
    {
        int base, count;
    };
    constexpr LabelTable kHealthLabels{0xef61, 7}, kHungerLabels{0xefab, 6},
        kHappinessLabels{0xefe6, 6}, kCleanlinessLabels{0xf01a, 6}, kTirednessLabels{0xf0b1, 7};

    inline std::string statLabel(const Rom &rom, LabelTable t, int value,
                                 const std::function<std::string(int)> &readStr)
    {
        for (int i = 0; i < t.count; ++i)
        {
            const int b = t.base + i * 3;
            if (rom.u16(b) <= value && value <= rom.u16(b + 1))
                return readStr(rom.u16(b + 2));
        }
        return "";
    }
}