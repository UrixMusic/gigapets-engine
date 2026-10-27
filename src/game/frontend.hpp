// game/frontend.hpp
// Logic for the screens before gameplay, ported from GameMainLoop's state machine:
//   4 MainMenu (ShowMainMenuScreen)        8 LoadSlot (ShowLoadSaveSlotScreen)
//   6 NewIntro (ShowNewPetIntroScreen)     7 HouseSelect (ShowSaveSlotSelectScreen)
//   CreateNewPet -> newGame()
// No SDL, no ROM, no drawing: input events in, next state / sfx id / save data out.
// Not ported yet: starter select (0x11), name entry (0x12, HandleNameEntryInput not read), options (5).
#pragma once
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include "save.hpp"

namespace gp::fe
{
    // Values are the ROM's g_gameState numbers.
    enum class State
    {
        MainMenu = 4,
        Options = 5,
        NewIntro = 6,
        HouseSelect = 7,
        LoadSlot = 8,
        Gameplay = 10,
        StarterSelect = 0x11,
        NameEntry = 0x12,
    };

    // PollSingleButtonEvent codes seen in the screens (2/3 = up/down, 4 = confirm, 5 = back).
    enum class Btn
    {
        None = -1,
        Up = 2,
        Down = 3,
        Confirm = 4,
        Back = 5
    };

    // UI strings, read from the ROM (wide chars at these word addresses).
    namespace str
    {
        constexpr const char *kEmpty = "Empty"; // 0xf4a4
        constexpr const char *kBack = "Back";   // 0xf4aa
        constexpr const char *kRemove =         // 0xf4af
            "This will remove %s from the game.  Are you sure you want this?";
        constexpr const char *kSelectHouse = "Select your house."; // 0xf4ef
        constexpr const char *kIntroTitle = "Next you will:";      // 0xe2b3
        constexpr const char *kIntro1 = "* Choose a pet";          // 0xe2c2
        constexpr const char *kIntro2 = "* Name it";               // 0xe2d1
        constexpr const char *kIntro3 = "* Pick a house";          // 0xe2db
        // 0xe2ea starts "Press Sel..." - the rest is cut off in my read; the label says "Press Select to continue"
        constexpr const char *kIntroPrompt = "Press Select to continue";
    }

    constexpr int kSfxMove = 0x2d, kSfxConfirm = 0x59;

    // ------------------------------------------------------------ slots
    struct SlotInfo
    {
        bool valid = false;
        std::string name; // empty -> show str::kEmpty
        int species = 0;
        int appearance = -1; // -1 = none saved (the ROM compares against 0xffff)
        int house = -1;
    };

    inline SlotInfo readSlot(int slot)
    {
        SlotInfo si;
        SaveData s;
        if (!loadFromFile(slotPath(slot), s) || !s.has("version"))
            return si;
        si.valid = true;
        si.name = s.getStr("name");
        si.species = s.getInt("species", 0);
        si.appearance = s.getInt("appearance", -1);
        si.house = s.getInt("house", -1);
        return si;
    }

    // ------------------------------------------------------------ main menu (state 4)
    // Entries: 0 = New (-> 6), 1 = Load (-> 8, the starting cursor), 2 = Options (-> 5).
    // Up/Down clamp at the ends. Confirm only marks the button pressed; it fires on RELEASE.
    // A press before the intro animation is done just makes the buttons appear.
    struct MainMenu
    {
        int cursor = 1;
        bool pressed = false; // iStack_6
        bool ready = false;   // iStack_5: buttons visible
        // Slide offsets, ticked at 30 Hz. Draw (palette 0x4d5 unless noted, layer 0x3000):
        //   0x9f4 at (b+0xb8, -1)   0x9f1 at (a, -1)   0x9f3 [pet col 2] at (a+0x1b, 0x28)
        //   0x9f0 [pet col 0] at (a, 0x7b)   0x9f2 [pet col 1] at (b+0xcf, 0x44)
        //   0x9e6 (pal 0x4c7) at (0x5f, c); once ready: button tiles (pal 0x4c7) at
        //   (0x68,0x85) (0x65,0xa0) (0x74,0xbb), tile = table[idle, hover, pressed]
        int a, b, c;
        bool redraw = true;

        explicit MainMenu(bool returningFromSlotScreen = false)
        {
            if (returningFromSlotScreen) // came back from state 7 or 8: no slide-in
                a = 0, b = 0, c = 8;
            else
                a = -0xac, b = 0x88, c = -0x8c;
        }

        // 0 = none, otherwise the sound effect to play
        int onPress(Btn k)
        {
            redraw = true;
            if (!ready)
            {
                ready = true;
                return 0;
            }
            if (k == Btn::Up)
            {
                cursor = std::max(0, cursor - 1);
                return kSfxMove;
            }
            if (k == Btn::Down)
            {
                cursor = std::min(2, cursor + 1);
                return kSfxMove;
            }
            if (k == Btn::Confirm)
            {
                pressed = true;
                return kSfxConfirm;
            }
            return 0;
        }

        std::optional<State> onRelease(Btn k)
        {
            if (k != Btn::Confirm || !pressed)
                return std::nullopt;
            return cursor == 0 ? State::NewIntro : cursor == 1 ? State::LoadSlot
                                                               : State::Options;
        }

        void tick()
        {
            if (a < 0)
                a += 2, redraw = true;
            else if (a > 0)
                a = 0, redraw = true;

            if (b > 0 && a > -0x87)
                b -= 2, redraw = true;
            else if (b < 0)
                b = 0, redraw = true;

            if (c < 8 && b < 10)
                c += 2, redraw = true;
            else if (c > 8)
                c = 8, redraw = true;
            else if (c == 8)
                ready = true, redraw = true;
        }
    };

    // ------------------------------------------------------------ load screen (state 8)
    // Slots 0..2 plus entry 3 = Back. Up/Down jump over slots with no valid save.
    // Confirm on a valid slot: g_currentSaveSlot = slot, LoadSaveSlot, then switch state
    // (the target was not visible in the decompile; Gameplay is the obvious one).
    struct LoadScreen
    {
        SlotInfo slots[3];
        int cursor = 3;
        struct Result
        {
            State next;
            int slot = -1; // >= 0: load this slot
        };

        explicit LoadScreen(const SlotInfo (&s)[3])
        {
            std::copy(std::begin(s), std::end(s), slots);
            // The ROM keeps g_loadSaveSelectedSlot across visits; starting on the first valid slot is the
            // sensible reading.
            for (int i = 0; i < 3; ++i)
                if (slots[i].valid)
                {
                    cursor = i;
                    break;
                }
        }

        std::optional<Result> onButton(Btn k)
        {
            if (k == Btn::Up)
            {
                for (int i = cursor - 1; i >= 0; --i)
                    if (slots[i].valid)
                    {
                        cursor = i;
                        break;
                    }
            }
            else if (k == Btn::Down && cursor != 3)
            {
                int n = 3;
                for (int i = cursor + 1; i < 3; ++i)
                    if (slots[i].valid)
                    {
                        n = i;
                        break;
                    }
                cursor = n;
            }
            else if (k == Btn::Back)
                return Result{State::MainMenu, -1};
            else if (k == Btn::Confirm)
            {
                if (cursor < 3 && slots[cursor].valid)
                    return Result{State::Gameplay, cursor};
                if (cursor == 3)
                    return Result{State::MainMenu, -1};
            }
            return std::nullopt;
        }

        std::string label(int i) const
        {
            return i == 3 ? str::kBack : slots[i].name.empty() ? str::kEmpty
                                                               : slots[i].name;
        }
    };

    // ------------------------------------------------------------ house select (state 7)
    // The last step of a new game. A slot IS a house. Cursor starts on the first empty slot (3 = Back
    // when all are full). Confirming an empty slot creates the pet; a full slot asks first.
    struct HouseSelect
    {
        SlotInfo slots[3];
        int cursor = 3;
        bool confirming = false; // the "This will remove %s" box is open
        struct Result
        {
            State next;
            int slot = -1; // >= 0: run newGame() into this slot
        };

        explicit HouseSelect(const SlotInfo (&s)[3])
        {
            std::copy(std::begin(s), std::end(s), slots);
            for (int i = 0; i < 3; ++i)
                if (!slots[i].valid)
                {
                    cursor = i;
                    break;
                }
        }

        std::string confirmText() const
        {
            return fmtName(str::kRemove, slots[cursor].name);
        }

        // Yes/No on the box
        std::optional<Result> onAnswer(bool yes)
        {
            if (!confirming)
                return std::nullopt;
            confirming = false;
            if (yes)
                return Result{State::Gameplay, cursor};
            return std::nullopt;
        }

        std::optional<Result> onButton(Btn k)
        {
            if (confirming)
                return std::nullopt;
            if (k == Btn::Up && cursor != 0)
                --cursor;
            else if (k == Btn::Down && cursor != 3)
                ++cursor;
            else if (k == Btn::Back)
                return Result{State::MainMenu, -1}; // target is a guess
            else if (k == Btn::Confirm)
            {
                if (cursor == 3)
                    return Result{State::MainMenu, -1};
                if (!slots[cursor].valid)
                    return Result{State::Gameplay, cursor};
                confirming = true;
            }
            return std::nullopt;
        }

    private:
        static std::string fmtName(std::string f, const std::string &n)
        {
            const size_t p = f.find("%s");
            if (p != std::string::npos)
                f.replace(p, 2, n);
            return f;
        }
    };

    // ------------------------------------------------------------ CreateNewPet
    // What the starter select screen has to hand over. Personality and illness come from the picked
    // candidate (GetDefaultStatValue 0..4 read the candidate's stats); appearance is believed to be
    // candidate offset +5 (g_starterExtraField).
    struct StarterPick
    {
        int species = 0; // 0..2
        int appearance = 0;
        int personality[4] = {}; // halos, pitchforks, stars, lightbulbs
        bool sick = false;       // GetDefaultStatValue(4) != 0
    };

    struct KitItem
    {
        int id, qty;
    };
    struct StarterKit
    {
        KitItem grants[8];
        int equip[6][2]; // {slot, item}: SetInventorySlot(slot, item)
    };

    // From CreateNewPet's listing (arguments pushed right to left).
    inline const StarterKit &starterKit(int species)
    {
        static const StarterKit k[3] = {
            {{{1, 1}, {0x6b, 1}, {0x6f, 1}, {0x73, 1}, {0x83, 1}, {0x1c, 2}, {0x9e, 1}, {0x93, 1}},
             {{0x1b, 0x6b}, {0x1c, 0x6f}, {3, 0x73}, {9, 0x83}, {1, 0x9e}, {0, 0x93}}},
            {{{1, 1}, {0x6e, 1}, {0x6f, 1}, {0x75, 1}, {0x89, 1}, {0x21, 2}, {0xa0, 1}, {0x96, 1}},
             {{0x1b, 0x6e}, {0x1c, 0x6f}, {3, 0x75}, {0xc, 0x89}, {1, 0xa0}, {0, 0x96}}},
            {{{1, 1}, {0x6d, 1}, {0x6f, 1}, {0x74, 1}, {0x86, 1}, {0x23, 2}, {0x9f, 1}, {0x9c, 1}},
             {{0x1b, 0x6d}, {0x1c, 0x6f}, {3, 0x74}, {6, 0x86}, {1, 0x9f}, {0, 0x9c}}},
        };
        return k[std::clamp(species, 0, 2)];
    }

    constexpr int kNoIllness = 10; // the save's "illness" value for healthy

    struct NewGameEnv
    {
        std::function<int(int lo, int hi)> randRange; // RandRange; inclusive assumed (check)
        std::function<int(int species)> rollIllness;  // RollRandomIllnessId, 0..9
    };

    // Everything CreateNewPet does that lands in a save. Starting values for care/personality are
    // assumed to be 0 (ResetPetStatsAndOdds); the care deltas are applied on top.
    inline SaveData newGame(const StarterPick &p, const std::string &name, int house, const NewGameEnv &env)
    {
        SaveData s;
        s.setInt("version", 3);
        s.setInt("species", p.species);
        s.setInt("appearance", p.appearance);
        s.setInt("house", house);
        s.setStr("name", name);

        std::map<int, int> inv, equip;
        const StarterKit &kit = starterKit(p.species);
        for (const KitItem &g : kit.grants)
            inv[g.id] += g.qty;
        for (const auto &e : kit.equip)
            equip[e[0]] = e[1];
        s.setMap("inventory", inv);
        s.setMap("equip", equip); // NEW key: SetInventorySlot targets

        s.setInt("money", env.randRange(0x28, 0x3d));
        // AdjustCareStat(1,0x41) (0,0x34) (3,0x50) (5,0x4b) (4,0x4b); index 2 untouched
        s.setList("care", {0x34, 0x41, 0, 0x50, 0x4b, 0x4b});
        s.setList("personality", {p.personality[0], p.personality[1], p.personality[2], p.personality[3]});
        s.setInt("illness", p.sick ? env.rollIllness(p.species) : kNoIllness);
        s.setInt("tricksKnown", 0); // SetTricksKnownAndRerollTraits(0) also rerolls traits: not modelled
        s.setInt("questsCompleted", 1);
        return s;
    }
}