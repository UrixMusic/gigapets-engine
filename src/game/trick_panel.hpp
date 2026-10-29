// trick_panel.hpp - the tricks panel on the outdoor menu bar (button 5): HandleTrickAction (0x04a862),
// RenderPetTrickMenuOverlay (0x04a95a) and the busy-4 branch of HandleMenuBarInput (0x04a342).
//
// Choosing the button: click 0x59 (volume 0xbe), the bar closes (anim 3 at (0, 0xb4)) and the panel opens (anim 4 at
// (0x73, 0xb4)); when the bar's animation is over the panel is up and takes the keys. Three icons sit like a triangle:
//   left   (known: 0x73,0xdc;  unknown: tile 0x5f at 0x69,0xdc)   Left  = trick 1
//   top    (0x9d,0xb4;         unknown: tile 0x5e)                 Up    = trick 2
//   right  (199,0xdc;          unknown: tile 0x5f)                 Right = trick 3
// A known trick's icon comes from the RAM table at 0x20ae (3 sprites per species, read in MAME). Z, X and Enter close the
// panel (click 0x59). Choosing a trick the pet has not learned runs activity 0x14: a question bubble (10), half a second,
// then bubble 9. Choosing a learned one runs activity 0xd, the pet performing it (trick_showcase.hpp), unless the prize roll
// (RollPetPrizeId(3)) gives an item first.
#pragma once

namespace gp
{

    constexpr int kTrickBarCloseAnim = 3, kTrickPanelAnim = 4;
    constexpr int kTrickPanelX = 0x73, kTrickPanelY = 0xb4;
    constexpr int kTrickSlots = 3;
    constexpr int kTrickClick = 0x59, kTrickClickVolume = 0xbe;
    constexpr int kTrickQuestionBubble = 10, kTrickAnswerBubble = 9; // activity 0x14: SpawnMessageBubble(10), 0.5 s, (9)
    constexpr double kTrickQuestionWait = 0.5;

    // g_tricksIcon (RAM 0x20ae), species-major.
    constexpr int kTrickIcons[3][kTrickSlots] = {{0x58, 0x5b, 0x5c}, {0x5d, 0x5a, 0x59}, {0x60, 0x56, 0x57}};
    constexpr int kTrickIconFirst = 0x56, kTrickIconLast = 0x60; // every sprite the panel can show (with the placeholders)
    constexpr int kTrickUnknownSprite[kTrickSlots] = {0x5f, 0x5e, 0x5f};

    struct TrickIcon
    {
        int sprite, x, y;
    };

    // The tile drawn for slot 0..2 (left, top, right). `known` is the pet's tricksKnown.
    constexpr TrickIcon trickIcon(int species, int slot, int known)
    {
        const int s = species < 0 ? 0 : species > 2 ? 2 : species;
        const bool have = known > slot;
        switch (slot)
        {
        case 0:
            return have ? TrickIcon{kTrickIcons[s][0], 0x73, 0xdc} : TrickIcon{kTrickUnknownSprite[0], 0x69, 0xdc};
        case 1:
            return {have ? kTrickIcons[s][1] : kTrickUnknownSprite[1], 0x9d, 0xb4};
        default:
            return {have ? kTrickIcons[s][2] : kTrickUnknownSprite[2], 199, 0xdc};
        }
    }

    // The bar's key events: 0 left, 1 right, 2 up, 3 down, 4..6 Z, X and Enter. Returns the slot a direction picks, else -1.
    constexpr int trickSlotForEvent(int ev) { return ev == 0 ? 0 : ev == 2 ? 1 : ev == 1 ? 2 : -1; }
    constexpr bool trickClosesPanel(int ev) { return ev >= 4 && ev <= 6; }

} // namespace gp
