// tests/settings_test.cpp - the options screen's rules, the settings file, and the tutorial flags (game/settings.hpp).
#include <cstdio>
#include "../src/game/settings.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    // ---- the rows ----
    check("seventeen rows: the settings, the aspect ratio, seven key bindings, Done last", kOptMusic == 0 && kOptSfx == 1 && kOptVolume == 2 && kOptTutorials == 3 && kOptInterpolate == 4 &&
                                                                    kOptScale == 5 && kOptScaleMode == 6 && kOptWindowMode == 7 && kOptAspect == 8 && kOptKeyFirst == 9 &&
                                                                    kOptKeyLast == 15 && kOptDone == 16 && kOptRows == 17);
    check("the key rows map to the seven actions", isKeyRow(9) && isKeyRow(15) && !isKeyRow(8) && !isKeyRow(16) && keyRowAction(9) == 0 && keyRowAction(15) == 6);
    check("down wraps from done to music", optionsMove(0, true) == 1 && optionsMove(16, true) == 0);
    check("up wraps from music to done", optionsMove(0, false) == 16 && optionsMove(16, false) == 15);
    check("rows by kind: three sliders, three toggles, two choices, then Done", isSliderRow(0) && isSliderRow(1) && isSliderRow(2) && !isSliderRow(3) &&
                                                                           isToggleRow(3) && isToggleRow(4) && isToggleRow(6) && isChoiceRow(5) && isChoiceRow(7) && isChoiceRow(8) && !isChoiceRow(16) && !isToggleRow(16));
    check("the list stays put while the selection is on screen", optionsScroll(0, 0) == 0 && optionsScroll(4, 0) == 0 && optionsScroll(2, 3) == 2);
    check("it scrolls down just enough", optionsScroll(5, 0) == 1 && optionsScroll(8, 1) == 4 && optionsScroll(16, 0) == 12);
    check("and back up", optionsScroll(0, 4) == 0 && optionsScroll(3, 4) == 3);
    check("the scale steps 1, 2, 4, 8 and stops at the ends", stepScale(2, true) == 4 && stepScale(4, true) == 8 && stepScale(8, true) == 8 &&
                                                           stepScale(2, false) == 1 && stepScale(1, false) == 1 && stepScale(3, true) == 4 && sanitizeScale(3) == 2);
    check("the window mode cycles round both ways", stepWindowMode(0, true) == 1 && stepWindowMode(2, true) == 0 && stepWindowMode(0, false) == 2);

    // ---- the volume ----
    check("right goes to the next ten, capped at 100", volumeStep(80, true) == 90 && volumeStep(90, true) == 100 && volumeStep(100, true) == 100);
    check("left goes to the previous ten, floored at 0", volumeStep(80, false) == 70 && volumeStep(10, false) == 0 && volumeStep(0, false) == 0);
    check("off a multiple of ten it snaps: 85 -> 90 up, 70 down", volumeStep(85, true) == 90 && volumeStep(85, false) == 70);
    check("a volume outside 0..100 becomes 80", sanitizeVolume(-1) == 80 && sanitizeVolume(101) == 80 && sanitizeVolume(50) == 50 && sanitizeVolume(0) == 0 && sanitizeVolume(100) == 100);

    // ---- the file ----
    const std::string path = "settings_test.tmp";
    std::remove(path.c_str());
    {
        const Settings d = loadSettings(path);
        check("no file: tutorials on, music 100, sfx 100, volume 80", d.tutorials && d.musicVolume == 100 && d.sfxVolume == 100 && d.volume == 80);
        check("and the display: 2x, perfect, windowed, no interpolation", !d.interpolate && d.scale == 2 && d.perfectScale && d.windowMode == 0 && d.aspect == 0);
        check("and the default keys", d.keys[0] == 0 && d.keys[6] == 0);
    }
    {
        Settings s;
        s.tutorials = false;
        s.musicVolume = 40;
        s.sfxVolume = 70;
        s.volume = 30;
        s.interpolate = true;
        s.scale = 4;
        s.perfectScale = false;
        s.windowMode = 2;
        s.aspect = 1;
        s.keys[4] = 6;
        check("saves", saveSettings(s, path));
        const Settings r = loadSettings(path);
        check("and reads back the same", !r.tutorials && r.musicVolume == 40 && r.sfxVolume == 70 && r.volume == 30 && r.interpolate && r.scale == 4 && !r.perfectScale && r.windowMode == 2 && r.aspect == 1 && r.keys[4] == 6 && r.keys[0] == 0);
    }
    {
        SaveData d;
        d.setInt("volume", 500);
        saveToFile(path, d);
        const Settings r = loadSettings(path);
        check("a bad volume in the file becomes 80, the other settings keep their defaults", r.volume == 80 && r.tutorials && r.musicVolume == 100 && r.sfxVolume == 100);
    }
    {
        SaveData d;
        d.setInt("music", 0);
        saveToFile(path, d);
        check("an old file with music off keeps the music off", loadSettings(path).musicVolume == 0);
    }
    std::remove(path.c_str());

    // ---- the tutorials ----
    check("16 events; the bowl is 2 and the bed 3", kTutorialEvents == 16 && kTutorialBowl == 2 && kTutorialBed == 3);
    check("texts run on from 377: bowl 379, bed 380", tutorialText(kTutorialBowl) == 379 && tutorialText(kTutorialBed) == 380 && tutorialText(0) == 377);
    check("first time: on and not shown", isFirstTime(true, 0, 3) && !isFirstTime(false, 0, 3));
    check("shown once, never again", !isFirstTime(true, markShown(0, 3), 3) && isFirstTime(true, markShown(0, 3), 2));
    check("marking is one bit and keeps the others", markShown(0b101, 1) == 0b111 && markShown(markShown(0, 3), 3) == 8);
    check("events outside 0..15 are ignored", !isFirstTime(true, 0, 16) && !isFirstTime(true, 0, -1) && markShown(5, 16) == 5);
    check("the wait is 0.25 s: 8 steps of 1/30", kTutorialWaitTicks == 8);

    // ---- by a ground item ----
    check("the first item of all gets 'collect things' (4), whatever it is", itemTutorial(true, 0, 3) == 4 && itemTutorial(true, 0, 0xa3) == 4);
    const unsigned s4 = markShown(0, 4);
    check("after that, by kind: halo 2..5 -> 5, pitchfork 6..9 -> 6", itemTutorial(true, s4, 2) == 5 && itemTutorial(true, s4, 5) == 5 &&
                                                                        itemTutorial(true, s4, 6) == 6 && itemTutorial(true, s4, 9) == 6);
    check("bulb 0xe..0x11 -> 7, star 0xa..0xd -> 8, the token 0xa3 -> 12", itemTutorial(true, s4, 0xe) == 7 && itemTutorial(true, s4, 0x11) == 7 &&
                                                                              itemTutorial(true, s4, 0xa) == 8 && itemTutorial(true, s4, 0xd) == 8 &&
                                                                              itemTutorial(true, s4, kFriendshipTokenItem) == 12);
    check("an ordinary item after the first gets nothing", itemTutorial(true, s4, 0x20) == -1 && itemTutorial(true, s4, 1) == -1);
    check("each kind once", itemTutorial(true, markShown(s4, 5), 3) == -1 && itemTutorial(true, markShown(s4, 5), 6) == 6);
    check("tutorials off: never", itemTutorial(false, 0, 3) == -1);

    // ---- objects and pets ----
    check("a real object near: event 1, once", worldTutorial(true, 0, false, -1, 0x20, false) == 1 && worldTutorial(true, markShown(0, 1), false, -1, 0x20, false) == -1);
    check("an ignored object (or none) lets the pet one through: event 0", worldTutorial(true, 0, false, -1, -1, true) == 0 && worldTutorial(true, 0, false, -1, 0x32, true) == 0 &&
                                                                              worldTutorial(true, 0, false, -1, 6, true) == 0);
    check("no pet near and no real object: nothing", worldTutorial(true, 0, false, -1, -1, false) == -1);
    check("a real object hides the pet message", worldTutorial(true, markShown(0, 1), false, -1, 0x20, true) == -1);
    check("an item beats an object and a pet", worldTutorial(true, 0, true, 3, 0x20, true) == 4);
    check("the ignored objects are none, 0x76, 0x32, 0x17, 6", objectIgnoredByTutorial(-1) && objectIgnoredByTutorial(0x76) && objectIgnoredByTutorial(0x32) &&
                                                                  objectIgnoredByTutorial(0x17) && objectIgnoredByTutorial(6) && !objectIgnoredByTutorial(0x3f));

    // ---- coming to rest ----
    check("sick -> 9, then unhappy -> 11, then dirty -> 10", idleTutorial(true, 0, true, true, true) == 9 && idleTutorial(true, 0, false, true, true) == 11 &&
                                                                idleTutorial(true, 0, false, false, true) == 10 && idleTutorial(true, 0, false, false, false) == -1);
    check("a sick pet that has had its message gets nothing else", idleTutorial(true, markShown(0, 9), true, true, true) == -1);
    check("each once", idleTutorial(true, markShown(0, 11), false, true, true) == -1 && idleTutorial(true, markShown(0, 10), false, false, true) == -1);
    check("happiness below 0x1a is low, cleanliness 0x19 or less is dirty", kUnhappyBelow == 0x1a && kDirtyAtMost == 0x19);

    std::printf(fails ? "settings_test: %d failure(s)\n" : "settings_test: all passed\n", fails);
    return fails ? 1 : 0;
}
