// front_events.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of FrontEvents.
// What they need from the rest of the walk session comes in through FrontEventsEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct FrontEventsEnv
{
    App &app;
    int &bootPhase;
    unsigned int &bootT0;
    int &browseA;
    int &browsePal;
    std::function<void(int)> create;
    gp::DebugCode &debugCode;
    gp::Dialogue &dlg;
    int &fadeTick;
    FeKeys &feHeld;
    FeKeys &fePressed;
    bool &feSceneActive;
    gp::ScriptHost &feScripts;
    std::function<int()> firstEmptySlot;
    std::function<void()> goMenu;
    bool &hasPending;
    int &houseCur;
    const int &kFade;
    const unsigned int &kLogoMs;
    bool &leaving;
    int &loadCur;
    bool &loadPicked;
    unsigned int &loadPickedAt;
    std::function<void()> loadStart;
    MenuView &menu;
    gp::NameEntry &name;
    int &optCapture;
    bool &optPressed;
    int &optScroll;
    int &optSel;
    gp::Settings &opts;
    unsigned int &pressedUntil;
    std::function<int(int)> randBelow;
    SDL_Renderer* &ren;
    const gp::Rom&rom;
    bool &run;
    Screen &screen;
    std::function<void(Screen)> setScreen;
    std::function<void(Screen)> setScreenNoFade;
    bool &showOutlines;
    bool (&slotFull)[3];
    std::string (&slotName)[3];
    int &species;
    StarterScene &starter;
    gp::testmode::EntrySequence &testEntry;
    gp::testmode::ButtonTest &tmButton;
    unsigned int &tmChecksum;
    std::function<int(int)> tmEvent;
    std::function<void()> tmLeaveSound;
    gp::testmode::MenuList &tmMenu;
    std::function<void()> tmPlay;
    int &tmPress;
    TmScreen &tmScreen;
    gp::testmode::SoundTest &tmSound;
    std::function<void()> tmStart;
    std::unique_ptr<gp::ContentDB> &uiDb;
    SDL_Window* &win;
};

class FrontEvents : public FrontEventsEnv
{
public:
    explicit FrontEvents(FrontEventsEnv env) : FrontEventsEnv(std::move(env)) {}

    // The events of one frame: keys and the window
    auto handleEvents()
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
            {
                run = false;
                break;
            }
            if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP)
                continue;
            const SDL_Keycode k = gp::keys().canonical(e.key.keysym.scancode, e.key.keysym.sym); // a rebound key acts as its action's default
            const bool down = e.type == SDL_KEYDOWN, rep = e.key.repeat != 0;
            const bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
            {
                // what the scripts see of the keys (Z, X and the arrows, by their action: a rebound key counts as its default)
                bool *h = k == SDLK_z ? &feHeld.z : k == SDLK_x ? &feHeld.x : k == SDLK_LEFT ? &feHeld.left : k == SDLK_RIGHT ? &feHeld.right : k == SDLK_UP ? &feHeld.up : k == SDLK_DOWN ? &feHeld.down : nullptr;
                bool *pr = k == SDLK_z ? &fePressed.z : k == SDLK_x ? &fePressed.x : k == SDLK_LEFT ? &fePressed.left : k == SDLK_RIGHT ? &fePressed.right : k == SDLK_UP ? &fePressed.up : k == SDLK_DOWN ? &fePressed.down : nullptr;
                if (h)
                    *h = down;
                if (pr && down && !rep)
                    *pr = true;
            }
            if (feSceneActive) // a script's scene has the keys
                continue;
            if (screen == Screen::Options && optCapture >= 0) // waiting for the new key of a binding: Escape gives up
            {
                if (down && !rep)
                {
                    if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                        optCapture = -1;
                    else if (gp::keys().bind(optCapture, e.key.keysym.scancode))
                    {
                        opts.keys = gp::keys().key;
                        optCapture = -1;
                        feSound(gp::fe::kSfxConfirm, 0x7f);
                    }
                }
                continue;
            }

            if (screen == Screen::Menu && down && !rep && !gp::debugMode() && debugCode.press(k))
            {
                gp::debugMode() = true;
                feSound(0x53, 200);
                std::printf("debug mode on\n");
            }
            if (down && !rep)
            {
                if (gp::debugMode() && k == SDLK_F1)
                    screen = Screen::Menu;
                else if (gp::debugMode() && k == SDLK_F2)
                    screen = Screen::Name;
                else if (gp::debugMode() && k == SDLK_F3)
                    screen = Screen::Browse;
                else if (k == SDLK_x)
                {
                    if (screen == Screen::Boot)
                    {
                        if (!testEntry.armed()) // (Cancel ends the test mode sequence)
                            screen = Screen::Menu; // debug skip, as before
                    }
                    else if (screen == Screen::Name || screen == Screen::Browse)
                        goMenu();
                    // the other screens handle Escape themselves below
                }
                else if (gp::debugMode() && k == SDLK_F4)
                {
                    smoothCam = !smoothCam;
                    std::printf("smooth mode %s\n", smoothCam ? "on" : "off");
                }
                else if (gp::debugMode() && k == SDLK_F5)
                {
                    app.placeMode = (app.placeMode + 1) % 3;
                    std::printf("placement mode %d\n", app.placeMode);
                }
                else if (gp::debugMode() && k == SDLK_F6)
                {
                    app.swapRB = !app.swapRB;
                    app.clearCaches();
                    std::printf("swapRB %d\n", int(app.swapRB));
                }
                else if (gp::debugMode() && k == SDLK_F7)
                {
                    species = (species + 1) % 4;
                    std::printf("species %d\n", species);
                }
                else if (gp::debugMode() && k == SDLK_F8)
                    showOutlines = !showOutlines;
                else if (gp::debugMode() && k == SDLK_F9)
                {
                    app.transMode = (app.transMode + 1) % 3;
                    app.clearCaches();
                    std::printf("layer transparency mode %d\n", app.transMode);
                }
            }
            if (screen == Screen::Boot && down && !rep && bootPhase <= 1)
            {
                const int ev = tmEvent(k);
                if (testEntry.armed()) // the chime has gone: Up, Down, Menu, Cancel
                {
                    if (testEntry.press(ev) == tmn::EntrySequence::Result::Entered)
                    {
                        tmStart();
                        setScreen(Screen::TestMode);
                    }
                    continue;
                }
                if (ev == tmn::Left || ev == tmn::Select) // the hold must not skip the logo
                    continue;
            }
            if (screen == Screen::Boot && down && !rep && k != SDLK_F1 && k != SDLK_F2 && k != SDLK_F3)
            {
                if (bootPhase == 0)
                    bootT0 = SDL_GetTicks() - kLogoMs; // skip the hold, fade out starts
                else if (bootPhase == 2)
                {
                    menu.a = menu.b = 0;
                    menu.c = 8;
                    menu.ready = true; // first press finishes the slide
                }
                continue; // phase 1: input ignored, like the game during fades
            }
            if (down && screen != Screen::Boot && (hasPending || leaving || fadeTick < kFade))
                continue; // input is ignored during fades, like the game
            if (screen == Screen::Menu)
            {
                if (!menu.ready)
                {
                    if (down && !rep) // first press finishes the slide
                    {
                        menu.a = menu.b = 0;
                        menu.c = 8;
                        menu.ready = true;
                    }
                    continue;
                }
                if (down)
                {
                    if (k == SDLK_UP && menu.cursor > 0)
                        --menu.cursor, feSound(gp::fe::kSfxMove, 0x7f);
                    if (k == SDLK_DOWN && menu.cursor < 3)
                        ++menu.cursor, feSound(gp::fe::kSfxMove, 0x7f);
                    if (k == SDLK_z && !rep)
                        menu.pressed = true, feSound(gp::fe::kSfxConfirm, 0x7f); // ShowMainMenuScreen: 0x2d / 0x59 at volume 0x7f
                    const int d = shift ? 8 : 1;
                    bool changed = gp::debugMode();
                    if (!changed)
                        ;
                    else if (k == SDLK_q)
                        menu.a -= d;
                    else if (k == SDLK_w)
                        menu.a += d;
                    else if (k == SDLK_e)
                        menu.b -= d;
                    else if (k == SDLK_r)
                        menu.b += d;
                    else if (k == SDLK_t)
                        menu.c -= d;
                    else if (k == SDLK_y)
                        menu.c += d;
                    else
                        changed = false;
                    if (changed)
                        std::printf("slide a=%d b=%d c=0x%x\n", menu.a, menu.b, menu.c);
                }
                else if (k == SDLK_z && menu.pressed)
                {
                    menu.pressed = false;
                    static const char *names[4] = {"New -> state 6 (intro), demo jumps to name entry", "Load -> state 8", "Options -> state 5", "Mods"};
                    std::printf("%s\n", names[menu.cursor]);
                    // the engine system "frontend.menu": the player chose on the title menu (choice "new", "load" or "options"); cancel or replace and nothing happens
                    gp::SystemCtx mc;
                    mc.set("choice", menu.cursor == 0 ? "new" : menu.cursor == 1 ? "load" : menu.cursor == 2 ? "options" : "mods");
                    const int cursor = menu.cursor;
                    feScripts.system("frontend.menu", mc, [&]
                                     {
                    if (cursor == 0)
                    {
                        name = NameEntry();
                        setScreen(Screen::Intro);
                    }
                    else if (cursor == 1)
                    {
                        loadStart();
                        setScreenNoFade(Screen::Load);
                    }
                    else if (cursor == 3)
                    {
                        gModsUi.open("mods", gp::modStatePath());
                        setScreenNoFade(Screen::Mods);
                    }
                    else if (cursor == 2)
                    {
                        opts = gp::loadSettings();
                        optSel = 0;
                        optScroll = 0;
                        optCapture = -1;
                        opts.keys = gp::keys().key;
                        optPressed = false;
                        setScreenNoFade(Screen::Options);
                    } });
                }
            }
            else if (screen == Screen::Intro && down && !rep && fadeTick >= 16)
            {
                if (k == SDLK_x)
                    goMenu(); // event 5 -> state 4
                else
                {
                    // the engine system "frontend.intro": the intro is over (Z): on to the starter room; cancel or replace and the front end stays on the intro (a mod may open a scene)
                    gp::SystemCtx ic;
                    feScripts.system("frontend.intro", ic, [&]
                                     {
                        starter.start(rom, randBelow, makeStarterPool(*uiDb, gp::loadProfileFile()));
                        setScreen(Screen::Starter); });
                }
            }
            else if (screen == Screen::Load && down && !rep && !loadPicked)
            {
                if (k == SDLK_UP)
                {
                    for (int i = loadCur - 1; i >= 0; --i)
                        if (slotFull[i])
                        {
                            loadCur = i;
                            feSound(gp::fe::kSfxMove, 0x7f);
                            break;
                        }
                }
                else if (k == SDLK_DOWN)
                {
                    for (int i = loadCur + 1; i < 3; ++i)
                        if (slotFull[i])
                        {
                            loadCur = i;
                            goto moved;
                        }
                    loadCur = 3;
                moved:
                    feSound(gp::fe::kSfxMove, 0x7f);
                }
                else if (k == SDLK_x)
                    setScreenNoFade(Screen::Menu); // event 5 -> state 4
                else if (k == SDLK_z)
                {
                    feSound(gp::fe::kSfxConfirm, 0x7f);
                    if (loadCur == 3)
                        setScreenNoFade(Screen::Menu);
                    else if (slotFull[loadCur])
                    {
                        // the engine system "frontend.load": a saved pet was chosen on the load screen (the game is started from it next: frontend.start); cancel or replace and it is not picked
                        gp::SystemCtx lc;
                        lc.set("slot", loadCur).set("name", slotName[loadCur]);
                        feScripts.system("frontend.load", lc, [&]
                                         {
                            loadPicked = true;
                            loadPickedAt = SDL_GetTicks(); });
                    }
                }
            }
            else if (screen == Screen::Mods && down && !rep)
            {
                // Up / Down move (or carry the picked up mod in order mode), Z (Select) switches the mod on / off (order mode: picks it up / puts it down), the Menu key is the order mode, X (Cancel) leaves
                if (k == SDLK_UP)
                    gModsUi.moveCursor(-1), feSound(gp::fe::kSfxMove, 0x7f);
                else if (k == SDLK_DOWN)
                    gModsUi.moveCursor(1), feSound(gp::fe::kSfxMove, 0x7f);
                else if (k == SDLK_z)
                    gModsUi.press(), feSound(gp::fe::kSfxConfirm, 0x7f);
                else if (k == SDLK_RETURN) // the Menu key (a rebound one reads as Enter)
                    gModsUi.toggleOrder(), feSound(gp::fe::kSfxMove, 0x7f);
                else if (k == SDLK_x)
                {
                    gModsUi.held = false;
                    if (gModsUi.changed) // the mods load again (runFrontEnd returns kFrontReload and is entered once more)
                    {
                        gFrontReload = true;
                        run = false;
                    }
                    else
                        setScreenNoFade(Screen::Menu);
                }
            }
            else if (screen == Screen::Options && down && !optPressed)
            {
                // rows a mod added (options.add) sit above Done, so Done and the debug row move down by their number
                const int nMod = int(feScripts.optionRowCount());
                const int rowsTotal = gp::optRowCount(nMod, gp::debugMode());
                // Up and down move (wrapping, and the list scrolls to follow); left and right step a volume, a choice or a
                // switch; Z flips a switch, cycles a choice and presses Done; X leaves. The settings are saved on the way out
                // (SaveSettingsToSlot). The sound and the window follow the choices at once.
                auto leave = [&](bool pressed)
                {
                    opts.keys = gp::keys().key;
                    gp::saveSettings(opts);
                    if (feScripts.profileDirty()) // what the mods' rows kept (profile.set)
                    {
                        gp::saveProfileFile(feScripts.profileKv());
                        feScripts.profileSaved();
                    }
                    optPressed = pressed;
                    goMenu();
                };
                auto applyAudio = [&]
                {
                    gp::audio().setVolume(opts.volume);
                    gp::audio().setMusicVolume(opts.musicVolume);
                    gp::audio().setSfxVolume(opts.sfxVolume);
                };
                auto applyWindow = [&]
                {
                    smoothCam = opts.interpolate;
                    gp::applyDisplay(win, ren, gp::DisplaySettings{opts.scale, opts.perfectScale, opts.windowMode, opts.aspect});
                };
                // true = Z/Right (forward), false = Left
                auto changeRow = [&](bool forward, bool byZ)
                {
                    if (gp::isModRow(optSel, nMod)) // a mod's row: its script gets the new value
                    {
                        feScripts.optionStep(size_t(gp::modRowIndex(optSel)), forward);
                        return;
                    }
                    switch (optSel)
                    {
                    case gp::kOptTutorials:
                        opts.tutorials = !opts.tutorials;
                        break;
                    case gp::kOptInterpolate:
                        opts.interpolate = !opts.interpolate;
                        applyWindow();
                        break;
                    case gp::kOptScaleMode:
                        opts.perfectScale = !opts.perfectScale;
                        applyWindow();
                        break;
                    case gp::kOptScale:
                        opts.scale = byZ && opts.scale == 8 ? 1 : gp::stepScale(opts.scale, forward);
                        applyWindow();
                        break;
                    case gp::kOptWindowMode:
                        opts.windowMode = gp::stepWindowMode(opts.windowMode, forward);
                        applyWindow();
                        break;
                    case gp::kOptAspect:
                        opts.aspect = 1 - opts.aspect;
                        applyWindow();
                        break;
                    case gp::kOptRetro:
                        opts.retroAudio = !opts.retroAudio;
                        gp::audio().setRetro(opts.retroAudio);
                        feSound(gp::fe::kSfxConfirm, 0x7f); // hear the difference
                        break;
                    case gp::kOptLive:
                        opts.liveMusic = !opts.liveMusic;
                        gp::audio().setLive(opts.liveMusic);
                        break;
                    default:
                        break;
                    }
                };
                // the engine system "frontend.options": the player changed a row (Left / Right / Z). row: "music", "sfx", "volume", "tutorials", "interpolate", "scale", "scale_mode",
                // "window_mode", "aspect", "retro", "live", a mod row's id, or "" (a key row); cancel or replace and the game's own change is not made
                auto change = [&](bool forward, bool byZ)
                {
                    static const char *const names[] = {"music", "sfx", "volume", "tutorials", "interpolate", "scale", "scale_mode", "window_mode", "aspect", "retro", "live"};
                    std::string rowName;
                    if (optSel >= 0 && optSel <= gp::kOptLive)
                        rowName = names[optSel];
                    else if (gp::isModRow(optSel, nMod))
                        if (const auto *mr = feScripts.optionRow(size_t(gp::modRowIndex(optSel))))
                            rowName = mr->id;
                    gp::SystemCtx oc;
                    oc.set("row", rowName).set("forward", forward ? 1 : 0).set("by_z", byZ ? 1 : 0);
                    feScripts.system("frontend.options", oc, [&]
                                     { changeRow(forward, byZ); });
                };
                int *slider = optSel == gp::kOptMusic ? &opts.musicVolume : optSel == gp::kOptVolume ? &opts.volume
                                                                        : optSel == gp::kOptSfx      ? &opts.sfxVolume
                                                                                                     : nullptr;
                if (k == SDLK_UP && !rep)
                    optSel = gp::optionsMove(optSel, false, rowsTotal), feSound(gp::fe::kSfxMove, 0x7f);
                else if (k == SDLK_DOWN && !rep)
                    optSel = gp::optionsMove(optSel, true, rowsTotal), feSound(gp::fe::kSfxMove, 0x7f);
                else if (k == SDLK_x && !rep)
                    leave(false);
                else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && slider)
                {
                    const int before = *slider;
                    *slider = gp::volumeStep(*slider, k == SDLK_RIGHT); // held keys repeat
                    applyAudio();
                    if (optSel != gp::kOptMusic && *slider != before) // hear the level (the music is playing already)
                        feSound(gp::fe::kSfxConfirm, 0x7f);
                }
                else if (k == SDLK_LEFT && gp::isKeyRow(optSel) && !rep) // Left on a binding: back to its default key
                {
                    gp::keys().reset(gp::keyRowAction(optSel));
                    opts.keys = gp::keys().key;
                }
                else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && !rep)
                    change(k == SDLK_RIGHT, false);
                else if (k == SDLK_z && !rep)
                {
                    feSound(gp::fe::kSfxConfirm, 0x7f); // ShowOptionsMenuScreen
                    if (optSel == gp::optDone(nMod))
                        leave(true);
                    else if (optSel == gp::optTestMode(nMod) && gp::debugMode()) // the ROM's test mode, from the debug unlock
                    {
                        opts.keys = gp::keys().key;
                        gp::saveSettings(opts);
                        tmStart();
                        setScreen(Screen::TestMode);
                    }
                    else if (gp::isKeyRow(optSel))
                        optCapture = gp::keyRowAction(optSel); // the next key pressed is the new binding
                    else
                        change(true, true);
                }
                optScroll = gp::optionsScroll(optSel, optScroll, rowsTotal);
                applyAudio();
            }
            else if (screen == Screen::TestMode && down && !rep)
            {
                const int ev = tmEvent(k);
                switch (tmScreen)
                {
                case TmScreen::Menu:
                    if (ev == tmn::Up)
                        tmMenu.move(-1);
                    else if (ev == tmn::Down)
                        tmMenu.move(1);
                    else if (ev == tmn::Select)
                        switch (tmMenu.sel)
                        {
                        case tmn::RowButton:
                            tmButton = {};
                            tmPress = -1;
                            tmScreen = TmScreen::Button;
                            break;
                        case tmn::RowSound:
                            tmSound = {};
                            tmScreen = TmScreen::Sound;
                            tmPlay(); // SoundTestPlaySelected(0)
                            break;
                        case tmn::RowMemory:
                        {
                            unsigned sum = 0; // TestEeprom: the 16-bit sum of the ROM words from 0x4000 up
                            for (uint32_t a = 0x4000; a < rom.sizeWords() && a < 0x400000u; ++a)
                                sum += rom.u16(a);
                            tmChecksum = sum & 0xffff;
                            tmScreen = TmScreen::Memory;
                            break;
                        }
                        case tmn::RowVideo:
                            tmScreen = TmScreen::Video1;
                            break;
                        case tmn::RowHandheld:
                            tmScreen = TmScreen::Handheld;
                            break;
                        case tmn::RowCarrier:
                            tmScreen = TmScreen::Carrier;
                            break;
                        default: // EXIT TEST MODE (the ROM powers down)
                            goMenu();
                            break;
                        }
                    break;
                case TmScreen::Button:
                    if (tmPress < 0)
                        tmPress = ev;
                    break;
                case TmScreen::Sound:
                    if (ev == tmn::Left)
                        tmSound.prev(), tmSound.playAll = false, tmPlay();
                    else if (ev == tmn::Right)
                        tmSound.next(), tmSound.playAll = false, tmPlay();
                    else if (ev == tmn::Up)
                        tmSound.playAll = false, tmPlay();
                    else if (ev == tmn::Down)
                    {
                        if (tmSound.togglePlayAll())
                            tmPlay();
                    }
                    else if (ev == tmn::Cancel)
                        tmLeaveSound();
                    break;
                case TmScreen::Memory:
                case TmScreen::Carrier:
                case TmScreen::Handheld:
                    if (ev == tmn::Cancel)
                        tmScreen = TmScreen::Menu;
                    break;
                case TmScreen::Video1:
                    tmScreen = TmScreen::Video2; // any button
                    break;
                case TmScreen::Video2:
                    tmScreen = TmScreen::Menu;
                    break;
                }
            }
            else if (screen == Screen::House && down && !rep && fadeTick >= 16)
            {
                // the engine system "frontend.house": Z on a house (an empty one makes the pet there, a full one asks to remove its pet); true = no mod took it over
                auto houseSystemDefault = [&]
                {
                    gp::SystemCtx hc;
                    hc.set("slot", houseCur).set("occupied", slotFull[houseCur] ? 1 : 0);
                    return feScripts.system("frontend.house", hc, [] {});
                };
                if (dlg.open())
                {
                    gp::DialogueBox &b = *dlg.box;
                    if (k == SDLK_LEFT)
                        b.yesSelected = true; // Yes is on the left
                    else if (k == SDLK_RIGHT)
                        b.yesSelected = false;
                    else if (k == SDLK_z)
                        dlg.choose(b.yesSelected ? 1 : 2); // closes first; don't use b after this
                    else if (k == SDLK_x)
                        dlg.choose(2); // Esc = No
                }
                else if (k == SDLK_UP && houseCur > 0)
                    --houseCur;
                else if (k == SDLK_DOWN && houseCur < 3)
                    ++houseCur;
                else if (k == SDLK_x)
                    goMenu(); // event 5 -> state 4
                else if (k == SDLK_z)
                {
                    if (houseCur == 3)
                        goMenu(); // Back
                    else if (!houseSystemDefault()) // the engine system "frontend.house" (below): a mod took the choice over
                    {
                    }
                    else if (!slotFull[houseCur])
                        create(houseCur);
                    else
                    {
                        gp::DialogueBox b = gp::messageBox("This will remove " + slotName[houseCur] +
                                                           " from the game.  Are you sure you want this?");
                        b.kind = gp::BoxKind::Confirm;
                        b.yesSelected = false; // the real default is a guess; No is the safe one for a delete
                        const int slot = houseCur;
                        b.onYes = [this, slot]
                        { create(slot); };
                        dlg.show(std::move(b));
                    }
                }
            }
            else if (screen == Screen::Name && down)
            {
                using In = NameEntry::In;
                NameEntry::Result r;
                bool acted = true;
                // HandleNameEntryInput: every direction clicks 0x2d (0xaf); Z clicks 0x59 (0x7f), except Back or Space with
                // nothing typed and OK on a blank name, which do nothing
                if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)
                    feSound(0x2d, 0xaf);
                else if (k == SDLK_z && !rep && (name.onGrid || (name.col < 2 ? !name.name.empty() : NameEntry::nonBlank(name.name))))
                    feSound(0x59, 0x7f);
                if (k == SDLK_LEFT)
                    r = name.step(In::Left);
                else if (k == SDLK_RIGHT)
                    r = name.step(In::Right);
                else if (k == SDLK_UP)
                    r = name.step(In::Up);
                else if (k == SDLK_DOWN)
                    r = name.step(In::Down);
                else if (k == SDLK_z && !rep)
                    r = name.step(In::Confirm);
                else
                    acted = false;
                if (acted && r.typed)
                    pressedUntil = SDL_GetTicks() + 200; // WaitForElapsedTime(0.2)
                if (r.done)
                {
                    // the engine system "frontend.name": the pet's name is chosen (OK); a mod may change `name` (up to 9 characters), or cancel / replace and the front end stays on the name screen
                    gp::SystemCtx nc;
                    nc.set("name", name.name);
                    feScripts.system("frontend.name", nc, [&]
                                     {
                        std::string chosenName = nc.str("name");
                        if (chosenName.size() > size_t(NameEntry::kMax))
                            chosenName.resize(size_t(NameEntry::kMax));
                        if (!chosenName.empty())
                            name.name = chosenName;
                        std::printf("Done: name \"%s\" -> InitPetName, state 7 (house select)\n", name.name.c_str());
                        houseCur = firstEmptySlot();
                        dlg.box.reset();
                        setScreen(Screen::House); });
                }
            }
            else if (screen == Screen::Browse && down)
            {
                const int step = shift ? 16 : 1;
                bool changed = true;
                if (k == SDLK_LEFT)
                    browseA = std::max(0, browseA - 1);
                else if (k == SDLK_RIGHT)
                    ++browseA;
                else if (k == SDLK_UP)
                    browsePal += step;
                else if (k == SDLK_DOWN)
                    browsePal -= step;
                else
                    changed = false;
                if (changed)
                    std::printf("browse a=0x%x pal=0x%x\n", browseA, browsePal);
            }
            else if (screen == Screen::Starter && down && !rep && fadeTick >= 16)
            {
                if (k == SDLK_x)
                    goMenu();
                else if (starter.confirmed < 0)
                {
                    int c = starter.cursor;
                    if (k == SDLK_LEFT && c >= 2)
                        c -= 2;
                    else if (k == SDLK_RIGHT && c < 10)
                        c += 2;
                    else if (k == SDLK_UP || k == SDLK_DOWN)
                        c ^= 1;
                    else if (k == SDLK_z)
                    {
                        // the engine system "frontend.starter": Z on a pet of the starter room (cursor: 0..11, two to a column); cancel or replace and it is not chosen
                        gp::SystemCtx sc;
                        sc.set("cursor", starter.cursor);
                        feScripts.system("frontend.starter", sc, [&]
                                         { starter.confirm(); });
                    }
                    if (c != starter.cursor)
                    {
                        starter.cursor = c;
                        starter.changed = true;
                    }
                }
            }
        }
    }
};
