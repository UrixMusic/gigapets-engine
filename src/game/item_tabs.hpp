// game/item_tabs.hpp
// The item screen's category tabs (ItemMenuScreen 0x02c610, TickMenuTransitionState 0x02c5d6, DrawCategoryTabIcon 0x02d0c7).
// The screen is in one of four states (MenuSub): the grid, sliding to the tab list (anim 0x5d), the tab list, or sliding back to the grid
// (anim 0x5c). From the grid the Menu button (event 6) opens the tab list; Up/Down (events 2/3) move through the 5 tabs; Select
// shows the tab pressed and, when released, picks that category and slides back; Cancel or Menu slide back without picking.
// Input is ignored while sliding. Pure logic: no drawing, no sound (the ROM plays none here).
#pragma once
#include "menu_draw.hpp"

namespace gp
{
    constexpr int kTabCount = 5;
    constexpr int kTabAnimIn = 0x5d, kTabAnimOut = 0x5c, kTabAnimFps = 14, kTabAnimX = 0x5a, kTabAnimY = 0x0c;

    struct ItemTabs
    {
        enum Event
        {
            Left = 0, Right = 1, Up = 2, Down = 3, Select = 4, Cancel = 5, Menu = 6
        };

        int tab = 0; // the tab shown (the category, except while the list is open and moving)
        MenuSub sub = MenuSub::Grid;
        bool pressed = false;
        int animTick = 0; // 30 Hz ticks since the slide began

        bool inGrid() const { return sub == MenuSub::Grid; }
        bool inTabs() const { return sub == MenuSub::Tabs; }
        bool sliding() const { return sub == MenuSub::ToTabs || sub == MenuSub::ToGrid; }

        // The frame of the slide animation on show.
        int animFrame() const { return animTick * kTabAnimFps / 30; }

        // Menu in the grid: open the tab list on the current category. True if it did.
        bool openList(int category, bool miniPetConnected = false)
        {
            if (!inGrid() || miniPetConnected)
                return false;
            tab = category >= 1 && category <= 4 ? category : 0;
            pressed = false;
            sub = MenuSub::ToTabs;
            animTick = 0;
            return true;
        }

        // A button went down while the tab list is open. Cancel and Menu give the list up (the tab goes back to the category).
        void press(int ev, int category)
        {
            if (!inTabs())
                return;
            if (ev == Up && tab > 0)
                --tab;
            else if (ev == Down && tab < kTabCount - 1)
                ++tab;
            else if (ev == Select)
                pressed = true;
            else if (ev == Cancel || ev == Menu)
            {
                tab = category;
                sub = MenuSub::ToGrid;
                animTick = 0;
            }
        }

        // Select came back up while the list is open and it had been pressed: the category is the tab. True if it did.
        bool release(int &category)
        {
            if (!inTabs() || !pressed)
                return false;
            category = tab >= 1 && tab <= 4 ? tab : 0;
            tab = category;
            pressed = false;
            sub = MenuSub::ToGrid;
            animTick = 0;
            return true;
        }

        // One 30 Hz tick; `frames` is the slide animation's length.
        void tick(int frames)
        {
            if (!sliding())
                return;
            ++animTick;
            if (animFrame() >= frames)
            {
                sub = sub == MenuSub::ToTabs ? MenuSub::Tabs : MenuSub::Grid;
                animTick = 0;
            }
        }

        // DrawCategoryTabIcon draws the tab's normal icon during the slide only for the first frames.
        bool slideShowsIcon() const { return animFrame() <= 4; }
    };
}
