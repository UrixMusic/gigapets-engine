// settings.hpp - the options screen's settings and the first-time tutorial events.
//
// ShowOptionsMenuScreen (0x03da32) has four rows: music on/off, volume, tutorials on/off, and Done. Here the music row is a
// music volume (0 = off) and a separate sound effects volume row is added: not in the original, but sound is easier to live
// with when the music and the effects can be set apart. LoadSettingsFromSave
// (0x03bbc7) reads them from the save header: tutorials (1 bit), music (1 bit) and the volume (14 bits; a value outside
// 0..100 becomes 0x50 = 80). InitOneTimeEvents (0x00fac7) starts with tutorials on. They are the same for every save, so
// here they live in one small file next to the saves, not in a slot.
//
// The tutorials ("one-time events") are 16 messages, one bit each in g_oneTimeEventsShown. That mask IS per save slot
// (LoadOneTimeEventFlags reads it from the slot), so it belongs to the player. IsFirstTimeEvent(n) is true when tutorials
// are on and bit n is clear. The message for an event is a text id read from a RAM table the ROM fills at run time; the
// ROM image has it empty, so the ids here (377 + event: "Press Select to talk to other pets.", ...) are INFERRED from the
// order of the messages and from the two the houses use (event 2 = the food bowl, 3 = the bed), which agree with it.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include "save.hpp"

namespace gp
{

    struct Settings
    {
        bool tutorials = true;
        int volume = 80;       // master
        int musicVolume = 100; // 0 = music off
        int sfxVolume = 100;
        bool interpolate = false;  // the smooth (sub-pixel, interpolated) camera and movement
        int scale = 2;             // window size: 320 x 240 times 1, 2, 4 or 8
        bool perfectScale = true;  // whole-number pixel scaling (a border if the window doesn't fit), else stretched
        int windowMode = 0;        // 0 windowed, 1 fullscreen, 2 borderless window
        int aspect = 0;            // 0 = 4:3, 1 = 16:9
        std::array<int, 7> keys{}; // the SDL scancodes of Up, Down, Left, Right, Select, Cancel and Menu; 0 = the default key
    };

    constexpr int kScaleChoices[4] = {1, 2, 4, 8};
    constexpr int kWindowModeCount = 3;
    // Left and right on the scale row: the next choice, stopping at the ends. A value that isn't a choice becomes 2.
    constexpr int stepScale(int scale, bool up)
    {
        int i = 1;
        for (int k = 0; k < 4; ++k)
            if (kScaleChoices[k] == scale)
                i = k;
        i = up ? (i < 3 ? i + 1 : 3) : (i > 0 ? i - 1 : 0);
        return kScaleChoices[i];
    }
    constexpr int sanitizeScale(int v) { return v == 1 || v == 2 || v == 4 || v == 8 ? v : 2; }
    // The window mode row cycles round the three modes either way.
    constexpr int stepWindowMode(int mode, bool up) { return up ? (mode + 1) % kWindowModeCount : (mode + kWindowModeCount - 1) % kWindowModeCount; }

    constexpr int kDefaultVolume = 80, kMaxVolume = 100;
    inline std::string settingsPath() { return "settings.txt"; }

    inline int sanitizeVolume(int v) { return v < 0 || v > kMaxVolume ? kDefaultVolume : v; }
    inline int sanitizePercent(int v, int fallback) { return v < 0 || v > kMaxVolume ? fallback : v; }

    inline Settings loadSettings(const std::string &path = settingsPath())
    {
        Settings s;
        SaveData d;
        if (!loadFromFile(path, d))
            return s;
        if (d.has("tutorials"))
            s.tutorials = d.getInt("tutorials") != 0;
        if (d.has("music") && d.getInt("music") == 0) // a file from before the music slider: off was off
            s.musicVolume = 0;
        if (d.has("musicVolume"))
            s.musicVolume = sanitizePercent(d.getInt("musicVolume", 100), 100);
        if (d.has("sfxVolume"))
            s.sfxVolume = sanitizePercent(d.getInt("sfxVolume", 100), 100);
        if (d.has("volume"))
            s.volume = sanitizeVolume(d.getInt("volume", kDefaultVolume));
        if (d.has("interpolate"))
            s.interpolate = d.getInt("interpolate") != 0;
        if (d.has("scale"))
            s.scale = sanitizeScale(d.getInt("scale", 2));
        if (d.has("perfectScale"))
            s.perfectScale = d.getInt("perfectScale") != 0;
        if (d.has("aspect"))
            s.aspect = std::clamp(d.getInt("aspect", 0), 0, 1);
        if (d.has("windowMode"))
            s.windowMode = std::clamp(d.getInt("windowMode", 0), 0, kWindowModeCount - 1);
        static const char *const kKeyNames[7] = {"key_up", "key_down", "key_left", "key_right", "key_select", "key_cancel", "key_menu"};
        for (int a = 0; a < 7; ++a)
            if (d.has(kKeyNames[a]))
                s.keys[size_t(a)] = d.getInt(kKeyNames[a], 0);
        return s;
    }

    inline bool saveSettings(const Settings &s, const std::string &path = settingsPath())
    {
        SaveData d;
        d.setInt("tutorials", s.tutorials ? 1 : 0);
        d.setInt("musicVolume", s.musicVolume);
        d.setInt("sfxVolume", s.sfxVolume);
        d.setInt("volume", s.volume);
        d.setInt("interpolate", s.interpolate ? 1 : 0);
        d.setInt("scale", s.scale);
        d.setInt("perfectScale", s.perfectScale ? 1 : 0);
        d.setInt("windowMode", s.windowMode);
        d.setInt("aspect", s.aspect);
        static const char *const kKeyNames[7] = {"key_up", "key_down", "key_left", "key_right", "key_select", "key_cancel", "key_menu"};
        for (int a = 0; a < 7; ++a)
            d.setInt(kKeyNames[a], s.keys[size_t(a)]);
        return saveToFile(path, d);
    }

    // ---- the options screen's rules ----
    // The list: music volume, sound effects volume, master volume, tutorials, interpolation, scale, scale mode, window mode,
    // aspect ratio, the seven key bindings, Done. Only kOptVisible rows fit under the logo; the list scrolls to keep the selected row in view.
    constexpr int kOptMusic = 0, kOptSfx = 1, kOptVolume = 2, kOptTutorials = 3, kOptInterpolate = 4, kOptScale = 5, kOptScaleMode = 6,
                  kOptWindowMode = 7, kOptAspect = 8, kOptKeyFirst = 9, kOptKeyLast = 15, kOptDone = 16, kOptRows = 17;
    constexpr int kOptVisible = 5;
    constexpr bool isKeyRow(int row) { return row >= kOptKeyFirst && row <= kOptKeyLast; }
    constexpr int keyRowAction(int row) { return row - kOptKeyFirst; }
    constexpr bool isSliderRow(int row) { return row == kOptMusic || row == kOptVolume || row == kOptSfx; }
    constexpr bool isToggleRow(int row) { return row == kOptTutorials || row == kOptInterpolate || row == kOptScaleMode; }
    constexpr bool isChoiceRow(int row) { return row == kOptScale || row == kOptWindowMode || row == kOptAspect; }
    // The first visible row that keeps `sel` on screen, moving the list as little as it can.
    constexpr int optionsScroll(int sel, int scroll)
    {
        const int top = sel < scroll ? sel : sel >= scroll + kOptVisible ? sel - kOptVisible + 1 : scroll;
        return top < 0 ? 0 : top > kOptRows - kOptVisible ? kOptRows - kOptVisible : top;
    }

    // Up (event 2) and down (event 3) wrap round the rows.
    constexpr int optionsMove(int sel, bool down) { return down ? (sel + 1) % kOptRows : (sel + kOptRows - 1) % kOptRows; }

    // Left and right on a volume row: to the next multiple of 10 below or above, 0..100.
    constexpr int volumeStep(int volume, bool up)
    {
        const int v = up ? (volume + 10) / 10 * 10 : (volume - 10) / 10 * 10;
        return v < 0 ? 0 : v > kMaxVolume ? kMaxVolume : v;
    }

    // ---- the tutorials ----
    constexpr int kTutorialEvents = 16;
    constexpr int kTutorialTalk = 0, kTutorialObject = 1, kTutorialBowl = 2, kTutorialBed = 3;
    constexpr int kTutorialTextBase = 377; // INFERRED: text id = 377 + event
    constexpr int tutorialText(int event) { return kTutorialTextBase + event; }
    // TickFirstNPCEncounterSequence: after the pet has settled the message waits 0.25 s (elapsed ticks / 59.94 >= 0.25).
    constexpr int kTutorialWaitTicks = 8; // 0.25 s in 30 Hz steps, rounded up

    // ---- which message the overworld wants (HandlePetStageEvents 0x01308a, TickPlayerActorIdleBehavior 0x0147df) ----
    constexpr int kTutorialItemFirst = 4, kTutorialHalo = 5, kTutorialPitchfork = 6, kTutorialBulb = 7, kTutorialStar = 8;
    constexpr int kTutorialSick = 9, kTutorialDirty = 10, kTutorialUnhappy = 11, kTutorialToken = 12, kTutorialStuff = 15;
    constexpr int kFriendshipTokenItem = 0xa3;
    constexpr int kUnhappyBelow = 0x1a, kDirtyAtMost = 0x19; // IsHappinessLow: Happiness < 0x1a; Cleanliness <= 0x19

    constexpr bool isFirstTime(bool tutorialsOn, unsigned shown, int event)
    {
        return tutorialsOn && event >= 0 && event < kTutorialEvents && ((shown >> event) & 1u) == 0;
    }
    constexpr unsigned markShown(unsigned shown, int event)
    {
        return event >= 0 && event < kTutorialEvents ? (shown | (1u << event)) & 0xffffu : shown;
    }

    // Standing by a ground item: the very first one gets "collect things" (4); after that each kind of item has its own
    // message, once: halo items 2..5, pitchfork items 6..9, star items 10..13, light bulb items 14..17, and the friendship token.
    constexpr int itemTutorial(bool on, unsigned shown, int item)
    {
        if (isFirstTime(on, shown, kTutorialItemFirst))
            return kTutorialItemFirst;
        if (isFirstTime(on, shown, kTutorialHalo) && item >= 2 && item <= 5)
            return kTutorialHalo;
        if (isFirstTime(on, shown, kTutorialPitchfork) && item >= 6 && item <= 9)
            return kTutorialPitchfork;
        if (isFirstTime(on, shown, kTutorialBulb) && item >= 0xe && item <= 0x11)
            return kTutorialBulb;
        if (isFirstTime(on, shown, kTutorialStar) && item >= 0xa && item <= 0xd)
            return kTutorialStar;
        if (isFirstTime(on, shown, kTutorialToken) && item == kFriendshipTokenItem)
            return kTutorialToken;
        return -1;
    }
    // Objects that don't count as "an object" for the message (and so let the NPC one through): none, 0x76, 0x32, 0x17, 6.
    constexpr bool objectIgnoredByTutorial(int slot) { return slot == -1 || slot == 0x76 || slot == 0x32 || slot == 0x17 || slot == 6; }
    // Standing by something: an item beats everything; otherwise an object (event 1) or, with no real object, a pet to talk
    // to (event 0). -1 = nothing to show.
    constexpr int worldTutorial(bool on, unsigned shown, bool itemNear, int item, int objectSlot, bool npcNear)
    {
        if (itemNear)
            return itemTutorial(on, shown, item);
        if (objectIgnoredByTutorial(objectSlot))
            return npcNear && isFirstTime(on, shown, kTutorialTalk) ? kTutorialTalk : -1;
        return isFirstTime(on, shown, kTutorialObject) ? kTutorialObject : -1;
    }
    // When the pet comes to rest: sick (9) wins, then unhappy (11), then dirty (10). A sick pet that has had its message
    // gets nothing, whatever else is wrong.
    constexpr int idleTutorial(bool on, unsigned shown, bool sick, bool unhappy, bool dirty)
    {
        if (sick)
            return isFirstTime(on, shown, kTutorialSick) ? kTutorialSick : -1;
        if (unhappy)
            return isFirstTime(on, shown, kTutorialUnhappy) ? kTutorialUnhappy : -1;
        return dirty && isFirstTime(on, shown, kTutorialDirty) ? kTutorialDirty : -1;
    }

} // namespace gp
