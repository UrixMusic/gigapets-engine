// menu_bar.hpp -- port of the pet-room command bar (logic + draw hooks).
// Engine-agnostic: everything the original touched as a global goes through
// MenuBarHost, so you can wire it to your own state/draw code.
//
// Verified from the decompile: button map, confirm sequence, state mapping.
// NOT yet verified (marked TODO): cursor sprite layout, cursor move semantics
// for events 0/1, open/close animation timing, tricks panel.
#pragma once
#include <array>
#include <cstdint>

namespace menubar
{

    enum class Act
    {
        State,
        Activity,
        Tricks,
        GoHome,
        Dialogue,
        None
    };

    struct BarButton
    {
        Act act;
        int arg;       // game state / activity id / dialogue id
        int category;  // g_itemMenuCategory for State 0xe, else 0
        int prizeKind; // RollPetPrizeId(kind) first; -1 = no roll
        // Activity coords (house buttons 5 and 6 only)
        int x = 0, y = 0;
    };

    // Bar set 0 = outside (7 buttons), set 1 = in a house (8 buttons).
    // Buttons 0..4 are shared by both sets.
    inline constexpr std::array<BarButton, 7> kOutside = {{
        {Act::State, 0xf, 0, -1},     // 0 stats        (itemMenuMode = 0)
        {Act::State, 0xe, 0, -1},     // 1 items, cat 0
        {Act::State, 0xe, 1, 0},      // 2 items, cat 1 (prize roll 0)
        {Act::Activity, 0x27, 0, -1}, // 3 StartPetActivity(0x27)
        {Act::State, 0xe, 2, 2},      // 4 items, cat 2 (prize roll 2)
        {Act::Tricks, 0, 0, -1},      // 5 tricks panel
        {Act::GoHome, 0, 0, -1},      // 6 state = MapLocationToGameState(slot)
    }};

    inline constexpr std::array<BarButton, 8> kHouse = {{
        {Act::State, 0xf, 0, -1},
        {Act::State, 0xe, 0, -1},
        {Act::State, 0xe, 1, 0},
        {Act::Activity, 0x27, 0, -1},
        {Act::State, 0xe, 2, 2},
        {Act::Activity, 5, 0, 1, 0x39e, 0x93}, // 5
        {Act::Activity, 4, 0, 4, 0x182, 0x98}, // 6
        {Act::Dialogue, 0x20b7, 0, -1},        // 7
    }};

    // Original RemapZoneIndexToAudioId (really: location -> game state).
    constexpr int MapLocationToGameState(int idx)
    {
        switch (idx)
        {
        case 0:
            return 0xb;
        case 1:
            return 0xc;
        case 2:
            return 0xd;
        case 4:
            return 0x15;
        case 5:
            return 0x16;
        case 6:
            return 0x17;
        case 7:
            return 0x18;
        default:
            return 0xb;
        }
    }

    // Everything the bar needs from the rest of the game.
    struct MenuBarHost
    {
        virtual ~MenuBarHost() = default;
        virtual void switchGameState(int s) = 0;
        virtual void startPetActivity(int id, int param = 0, int x = 0, int y = 0) = 0;
        virtual int rollPetPrizeId(int kind) = 0; // 0 = no prize
        virtual void setPetBusy(int v) = 0;       // g_isPetBusy
        virtual void setLoopState(int v) = 0;     // *loopState
        virtual void setItemMenuMode(int m) = 0;  // g_itemMenuMode
        virtual void setItemMenuCategory(int c) = 0;
        virtual void startDialogue(int id) = 0;
        virtual void openTricksPanel() = 0;                       // TODO: HandleTrickAction
        virtual int currentSaveSlot() = 0;                        // 0..2
        virtual void requestRedraw() = 0;                         // g_needsRedraw
        virtual void startCloseAnim() = 0;                        // CloseMenuBar()
        virtual void moveCursor(int ev, int &sel, int count) = 0; // TODO: see note
        virtual void playOpenAnim(int which) = 0;                 // TODO: anims 0 / 2
        virtual void playSound(int /*id*/, int /*volume*/) {}     // PlaySoundEffect(id, volume, 0)
    };

    // HandleMenuBarInput / OpenMenuBar / AdvancePetBusyStateTo3
    constexpr int kSfxOpen = 0x59, kSfxOpenVol = 0xbe, kSfxMove = 0x2d, kSfxMoveVol = 0xaf, kSfxConfirm = 0x59, kSfxConfirmVol = 0xbe, kSfxCancel = 0x59, kSfxCancelVol = 0xbe;

    class MenuBar
    {
    public:
        explicit MenuBar(MenuBarHost &h) : host_(h) {}

        void open(bool inHouse)
        {
            inHouse_ = inHouse;
            selected_ = 0;
            open_ = true;
            host_.requestRedraw();
        }
        bool isOpen() const { return open_; }
        void forceClose() { open_ = false; } // the tricks panel took over and has finished with the bar
        int selected() const { return selected_; }
        int buttonCount() const { return inHouse_ ? 8 : 7; }

        // Call once per input event while g_isPetBusy == 2 (original gate).
        // ev: 0/1 = press pass (cursor), 4 = confirm. Event 0xb (power button)
        // is NOT a bar button: handle it outside (SwitchGameState(2), loop 3).
        void handleEvent(int ev, bool petBusyIs2)
        {
            if (!open_ || !petBusyIs2)
                return;
            if (ev == 0 || ev == 1)
            {
                host_.moveCursor(ev, selected_, buttonCount());
                host_.requestRedraw();
                host_.playSound(kSfxMove, kSfxMoveVol);
            }
            else if (ev == 4)
            {
                // AdvanceMenuCursorSelection(4,0) clears the pressed flag, then:
                confirm();
                host_.playSound(kSfxConfirm, kSfxConfirmVol);
            }
        }

        // Cancel button: close without running any action.
        void cancel(bool petBusyIs2)
        {
            if (!open_ || !petBusyIs2)
                return;
            host_.setPetBusy(3);
            host_.startCloseAnim(); // walk.cpp plays anim 1 (house) or 3 (outside)
            open_ = false;
            host_.playSound(kSfxCancel, kSfxCancelVol);
        }

    private:
        const BarButton &button(int i) const
        {
            return inHouse_ ? kHouse[i] : kOutside[i];
        }

        void finish()
        { // shared tail of every action button
            host_.setPetBusy(3);
            host_.startCloseAnim();
            host_.setLoopState(4);
            open_ = false;
        }

        void confirm()
        {
            const BarButton &b = button(selected_);

            if (b.act == Act::Tricks)
            {                            // outside button 5
                host_.openTricksPanel(); // TODO: sequence not verified
                return;
            }

            if (b.prizeKind >= 0)
            {
                int prize = host_.rollPetPrizeId(b.prizeKind);
                if (prize != 0)
                {
                    host_.startPetActivity(0x10, prize);
                    finish();
                    return;
                }
            }

            switch (b.act)
            {
            case Act::State:
                host_.setItemMenuMode(0); // button 0 sets mode 0 explicitly
                if (b.arg == 0xe)
                    host_.setItemMenuCategory(b.category);
                host_.switchGameState(b.arg);
                break;
            case Act::Activity:
                host_.startPetActivity(b.arg, 0, b.x, b.y);
                break;
            case Act::GoHome:
                host_.switchGameState(MapLocationToGameState(host_.currentSaveSlot()));
                break;
            case Act::Dialogue:
                host_.startDialogue(b.arg);
                break;
            default:
                return;
            }
            finish();
        }

        MenuBarHost &host_;
        bool inHouse_ = false, open_ = false;
        int selected_ = 0;
    };

} // namespace menubar