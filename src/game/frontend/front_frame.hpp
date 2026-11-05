// front_frame.hpp - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool's spec are the methods of FrontFrame.
// What they need from the rest of the walk session comes in through FrontFrameEnv, under the names they had, so the bodies read as before.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>

struct FrontFrameEnv
{
    const char* &anyShot;
    App &app;
    int &bootPhase;
    unsigned int &bootT0;
    int &browseA;
    int &browsePal;
    Pet &chosen;
    gp::Dialogue &dlg;
    std::function<void(const gp::DialogueBox&)> drawConfirm;
    int &fadeOutTick;
    int &fadeSkip;
    int &fadeTick;
    ScriptDrawer &feDrawer;
    FeKeys &feHeld;
    FeKeys &fePressed;
    bool &feSceneActive;
    gp::ScriptHost &feScripts;
    long int &feTick;
    std::function<TextDrawer&(int, int)> fontFor;
    int &frame;
    FrontGfx &gfx;
    std::function<void()> goMenu;
    bool &hasPending;
    int &houseCur;
    const int &kFade;
    const unsigned int &kLogoMs;
    unsigned int &lastMs;
    std::string &lastTitle;
    bool &launchGame;
    bool &leaving;
    int &loadCur;
    bool &loadPicked;
    unsigned int &loadPickedAt;
    MenuView &menu;
    gp::NameEntry &name;
    int &optCapture;
    bool &optPressed;
    int &optScroll;
    int &optSel;
    gp::Settings &opts;
    int &outTick;
    Screen &pendingScreen;
    bool &pendingSlide;
    unsigned int &pressedUntil;
    std::function<int(int)> randBelow;
    SDL_Renderer* &ren;
    std::function<void()> resetSlide;
    bool &run;
    Screen &screen;
    std::function<void(Screen)> setScreen;
    int &shotFrame;
    bool &showOutlines;
    int (&slotColour)[3];
    bool (&slotFull)[3];
    std::string (&slotName)[3];
    int (&slotSpecies)[3];
    std::function<void()> smoothMenu;
    std::function<void()> startFade;
    std::function<void(int)> startGame;
    StarterScene &starter;
    std::function<void()> stepSlide;
    gp::testmode::EntrySequence &testEntry;
    double &tickAcc;
    gp::testmode::ButtonTest &tmButton;
    unsigned int &tmChecksum;
    gp::testmode::MenuList &tmMenu;
    std::function<void()> tmPlay;
    int &tmPress;
    TmScreen &tmScreen;
    int &tmSongTicks;
    gp::testmode::SoundTest &tmSound;
    std::unique_ptr<gp::ContentDB> &uiDb;
    SDL_Window* &win;
};

class FrontFrame : public FrontFrameEnv
{
public:
    explicit FrontFrame(FrontFrameEnv env) : FrontFrameEnv(std::move(env)) {}

    // The rest of a frame: the 30 Hz ticks (fades, slides, scripts), the screen's own step and its drawing, then the picture is shown
    auto stepAndDraw()
    {
        // ---- draw
        gp::useUiScreen(ren); // 16:9: the 320 x 240 screens sit in the middle
        gp::clearFrame(ren);
        gfx.q.clear();
        gfx.texts.clear();
        gfx.lastText.clear();

        // 30 Hz game ticks (drives the fade and the boot slide)
        const Uint32 now = SDL_GetTicks();
        tickAcc += (now - lastMs) * 30.0 / 1000.0;
        lastMs = now;
        int ticks = 0;
        while (tickAcc >= 1.0)
        {
            ++ticks;
            tickAcc -= 1.0;
        }
        const bool hitch = fadeSkip > 0; // right after a screen change: ignore the texture-upload hitch
        if (hitch)
            --fadeSkip;
        const int gt = hitch ? 0 : std::min(ticks, 3); // ticks for fades and the menu slide
        for (int ti = 0; ti < std::min(ticks, 4); ++ti) // the scripts' ticks (30 a second)
        {
            const auto keysOf = [&]
            {
                gp::SceneInput in;
                in.z = ti == 0 && fePressed.z;
                in.x = ti == 0 && fePressed.x;
                in.left = ti == 0 && fePressed.left;
                in.right = ti == 0 && fePressed.right;
                in.up = ti == 0 && fePressed.up;
                in.down = ti == 0 && fePressed.down;
                in.heldZ = feHeld.z;
                in.heldX = feHeld.x;
                in.heldLeft = feHeld.left;
                in.heldRight = feHeld.right;
                in.heldUp = feHeld.up;
                in.heldDown = feHeld.down;
                return in;
            };
            gp::ScriptHost::TickInfo ti_;
            ti_.tick = feTick;
            ti_.menu = true; // (what is open: the front end's own screens)
            ti_.scene = feSceneActive;
            ti_.keys = keysOf();
            feScripts.fireGameTick(false, ti_);
            if (!feSceneActive)
                if (std::string sceneId; feScripts.takeSceneOpen(sceneId))
                {
                    feScripts.beginScene(sceneId);
                    feSceneActive = true;
                    std::printf("front end: scene %s opened\n", sceneId.c_str());
                }
            if (feSceneActive)
            {
                feScripts.tickScene(keysOf());
                if (feScripts.sceneCloseRequested())
                {
                    feScripts.endScene();
                    feSceneActive = false;
                }
            }
            feScripts.tick(); // wait(n) in the scripts
            feScripts.fireGameTick(true, ti_);
            ++feTick;
            fePressed = FeKeys();
        }
        if (hasPending)
        {
            fadeOutTick = std::min(kFade, fadeOutTick + gt);
            if (fadeOutTick >= kFade) // black: swap and fade the new screen in
            {
                screen = pendingScreen;
                hasPending = false;
                if (screen == Screen::Menu && pendingSlide)
                    resetSlide();
                pendingSlide = false;
                startFade();
            }
        }
        else if (leaving)
        {
            fadeOutTick = std::min(kFade, fadeOutTick + gt);
            if (fadeOutTick >= kFade)
                run = false; // black: chosenSlot is already set, walk takes over
        }
        else if (fadeTick < kFade)
            fadeTick = std::min(kFade, fadeTick + gt);

        switch (screen)
        {
        case Screen::Boot:
        {
            if (bootPhase == 0) // fade in (fadeTick), then hold the logos
            {
                { // HandleTestModeEntrySequence: Left + Select held for 3 s, a chime, then Up, Down, Menu, Cancel; the logo waits for it
                    const Uint8 *ks = SDL_GetKeyboardState(nullptr);
                    const bool both = gp::keys().held(ks, gp::Action::Left) && gp::keys().held(ks, gp::Action::Select);
                    for (int i = 0; i < ticks; ++i)
                        if (testEntry.tick(both) == tmn::EntrySequence::Result::Chime)
                            feSound(0x59, 0xbe);
                    if (testEntry.armed() && now - bootT0 >= kLogoMs - 100)
                        bootT0 = now - (kLogoMs - 100);
                }
                app.drawBackground(1, 0x5c7);
                if (now - bootT0 >= kLogoMs)
                {
                    bootPhase = 1;
                    outTick = 0;
                }
                break;
            }
            if (bootPhase == 1) // logos fade out to black
            {
                app.drawBackground(1, 0x5c7);
                outTick += ticks;
                if (outTick >= 16)
                {
                    bootPhase = 2;
                    startFade(); // plain background fades in
                    menu.a = -0xac;
                    menu.b = 0x88;
                    menu.c = -0x8c;
                    menu.ready = false;
                }
                break;
            }
            for (int i = 0; i < gt; ++i) // phase 2: slide in
                stepSlide();
            app.drawBackground(2, 0x5c5);
            smoothMenu();
            drawMainMenu(gfx, menu);
            if (menu.ready)
                screen = Screen::Menu; // plain assignment: no new fade
            break;
        }
        case Screen::Menu:
            for (int i = 0; i < gt && !menu.ready; ++i) // replayed slide-in after the pet flow
                stepSlide();
            app.drawBackground(2, 0x5c5);
            smoothMenu();
            drawMainMenu(gfx, menu);
            break;
        case Screen::Intro:
            app.drawBackground(0, 0x5c3);
            drawIntro(gfx, menu);
            break;
        case Screen::Starter:
        {
            for (int i = 0; i < ticks; ++i)
                starter.tick(app, randBelow);
            starter.alpha = smoothCam ? float(tickAcc) : 1.f; // how far through the current tick we are
            starter.draw(app, gfx, now);
            if (starter.finished)
            {
                const auto &sp = starter.pets[starter.confirmed];
                chosen.species = sp.species;
                chosen.colour = sp.colour;
                for (int j = 0; j < 4; ++j)
                    chosen.stat[j] = sp.stat[j];
                chosen.sick = sp.react; // the flag the selector pet carries (the one that keeps reacting)
                std::printf("picked species %d colour 0x%x -> state 0x12\n", chosen.species, chosen.colour);
                starter.finished = false;
                setScreen(Screen::Name); // fades out with the starter scene still on screen
            }
            gfx.drawTile(0x2fb, 0x28c, 0, 0xb0, 0x1000, 999);
            const auto &cp = starter.pets[starter.cursor];
            char buf[16];
            static const int sx[4] = {0x78, 0x78, 0xaf, 0xaf}, sy[4] = {0xbe, 0xd4, 0xbe, 0xd4};
            for (int j = 0; j < 4; ++j)
            {
                std::snprintf(buf, sizeof buf, "%d", cp.stat[j]);
                gfx.drawTextBox(2, 0x28c, sx[j], sy[j], buf, 0x1e, 2, 0x1000);
            }
            break;
        }
        case Screen::Load:
            app.drawBackground(3, 0x5c6);
            if (loadCur < 3 && slotFull[loadCur] && slotSpecies[loadCur] >= 0 && gp::knownSpecies(slotSpecies[loadCur]))
                app.drawLoadPicture(gp::artOf(slotSpecies[loadCur]), slotColour[loadCur]);
            drawLoad(gfx, loadCur, loadPicked, slotName, slotFull, slotSpecies, slotColour);
            if (loadPicked && !leaving && !hasPending && now - loadPickedAt >= 200) // WaitForElapsedTime; the real length is unread
            {
                std::printf("LoadSaveSlot(%d) \"%s\" -> state 0xa (gameplay)\n", loadCur, slotName[loadCur].c_str());
                if (launchGame)
                    startGame(loadCur); // loadPicked stays set so the picked look holds through the fade
                else
                {
                    loadPicked = false;
                    goMenu(); // the demo stops here
                }
            }
            break;
        case Screen::Options:
            app.drawBackground(2, 0x5c5);
            {
                // the rows of the mods (options.add): their name and the value as text
                std::vector<std::pair<std::string, std::string>> modRows;
                for (size_t mi = 0; mi < feScripts.optionRowCount(); ++mi)
                {
                    const auto *mr = feScripts.optionRow(mi);
                    if (!mr)
                        break;
                    const double v = feScripts.optionGet(mi);
                    std::string value;
                    if (mr->kind == "toggle")
                        value = v != 0 ? "On" : "Off";
                    else if (mr->kind == "choice")
                        value = uiDb->textOr(mr->choices[size_t(std::clamp(int(v), 0, int(mr->choices.size()) - 1))], "?");
                    else
                    {
                        char b[32];
                        std::snprintf(b, sizeof b, "%g", v);
                        value = b;
                    }
                    modRows.emplace_back(uiDb->textOr(mr->label, mr->id), value);
                }
                drawOptions(gfx, optSel, optPressed, opts, optScroll, optCapture, gp::optRowCount(int(modRows.size()), gp::debugMode()), modRows);
            }
            break;
        case Screen::TestMode:
        {
            if (tmScreen == TmScreen::Video1 || tmScreen == TmScreen::Video2)
            {
                app.drawBackground(tmScreen == TmScreen::Video1 ? 0x21 : 0x22, 0x5ea); // RunVideoTestLoop
                break;
            }
            app.drawBackground(0, 0x5c3);
            switch (tmScreen)
            {
            case TmScreen::Menu:
                drawTestMenu(gfx, tmMenu.sel);
                break;
            case TmScreen::Button:
            {
                const Uint8 *ks = SDL_GetKeyboardState(nullptr);
                const bool cancelDown = gp::keys().held(ks, gp::Action::Cancel);
                for (int i = 0; i < ticks; ++i)
                {
                    tmButton.tick(i == 0 ? tmPress : -1, cancelDown);
                    if (i == 0)
                        tmPress = -1;
                }
                if (tmButton.failSound)
                {
                    tmButton.failSound = false;
                    feSound(0x59, 0x7f);
                }
                if (tmButton.exit)
                    tmScreen = TmScreen::Menu;
                drawButtonTest(gfx, tmButton);
                break;
            }
            case TmScreen::Sound:
            {
                for (int i = 0; i < ticks; ++i)
                {
                    // The songs loop here, so one counts as playing for 30 s; sound 0 starts over when it ends.
                    tmSongTicks += tmSound.isSong();
                    const bool playing = tmSound.isSong() ? tmSongTicks < 30 * tmn::kTicksPerSecond : gp::audio().sfxActive(tmSound.id);
                    if (tmSound.id == 0 && !playing)
                        tmPlay();
                    else if (tmSound.tick(playing))
                    {
                        tmSound.next();
                        tmPlay();
                    }
                }
                drawSoundTest(gfx, tmSound.id);
                break;
            }
            case TmScreen::Memory:
                drawTestText(gfx, 0x10, 0x10, "EEPROM TEST:");
                drawTestText(gfx, 0xb4, 0x10, "PASS"); // there is no EEPROM here: the saves are files
                drawTestText(gfx, 0x10, 0x30, "ROM CHECKSUM:");
                drawTestText(gfx, 0xb4, 0x30, std::to_string(tmChecksum));
                drawTestText(gfx, 0x10, 0x60, "PRESS SELECT TO RESTART THE TEST.");
                drawTestText(gfx, 0x10, 0x90, "PRESS CANCEL TO EXIT THIS TEST.");
                break;
            case TmScreen::Carrier:
                drawTestText(gfx, 0x10, 0x10, "NO PET CARRIER INSERTED");
                drawTestText(gfx, 0x10, 0x90, "PRESS CANCEL TO EXIT THIS TEST.");
                break;
            case TmScreen::Handheld:
                drawTestText(gfx, 0x10, 0x30, "INSERT HANDHELD");
                drawTestText(gfx, 0x10, 0x90, "PRESS CANCEL TO EXIT THIS TEST.");
                break;
            default:
                break;
            }
            break;
        }
        case Screen::House:
            app.drawBackground(2, 0x5c5);
            drawHouse(gfx, houseCur, slotName, slotFull);
            if (dlg.open())
                drawConfirm(*dlg.box);
            break;
        case Screen::Name:
            app.drawBackground(0x1f, 0x5dd);
            drawNameEntry(gfx, name, chosen.species, SDL_GetTicks() < pressedUntil);
            break;
        case Screen::Browse:
            app.drawBackground(browseA, browsePal);
            break;
        }

        app.flush(gfx);
        for (const auto &t : gfx.texts)
        {
            const gp::Align al = t.mode == 1   ? gp::Align::Center
                                 : t.mode == 2 ? gp::Align::Right
                                               : gp::Align::Left;
            fontFor(t.style, t.pal).draw(t.s, t.x, t.y, t.w, al);
        }
        if (feSceneActive) // a script's scene: the whole screen is its own (as in the world), the front end's screen is behind it unseen
        {
            gp::useUiScreen(ren);
            gp::clearFrame(ren);
            feDrawer.run(feScripts.drawScene());
        }
        // Game.hooks().onHud(g) and onPostDraw(g) draw over the front end's screens too, with everything a script can draw (script_draw.hpp: shapes, canvases, blits, meshes, "@frame",
        // sprites of mods and the game's pictures, text)
        feDrawer.run(feScripts.drawHud(""));
        gp::useWholeScreen(ren);
        feDrawer.frameDrawW = gp::logicalW();
        feDrawer.frameDrawH = 240;
        feDrawer.run(feScripts.drawPost(gp::logicalW()));
        if (showOutlines)
        {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            for (const auto &t : gfx.texts)
            {
                SDL_Rect r{t.x, t.y, std::min(t.w, int(t.s.size()) * 6), 10};
                SDL_RenderDrawRect(ren, &r);
            }
        }
        // Fades are counted in 30 Hz ticks. In smooth mode the part-way-through-a-tick fraction is added, so the
        // black overlay ramps every frame instead of in 16 steps. (Off: frac is 0 and this is the old integer maths.)
        const float frac = (smoothCam && fadeSkip == 0) ? float(tickAcc) : 0.f;
        float alphaF = 0.f;
        if (hasPending || leaving)
            alphaF = 255.f * std::min(float(kFade), float(fadeOutTick) + frac) / float(kFade);
        else if (fadeTick < kFade)
            alphaF = 255.f * std::max(0.f, float(kFade) - (float(fadeTick) + frac)) / float(kFade);
        if (screen == Screen::Boot && bootPhase == 1)
            alphaF = 255.f * std::min(16.f, float(outTick) + frac) / 16.f;
        const int alpha = int(alphaF);
        gp::useWholeScreen(ren); // the fade covers the whole window
        if (alpha > 0)
        {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, uint8_t(alpha));
            SDL_RenderFillRect(ren, nullptr);
        }
        if (anyShot && frame == shotFrame)
        {
            int w = 0, h = 0;
            SDL_GetRendererOutputSize(ren, &w, &h);
            if (SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888))
            {
                SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
                SDL_SaveBMP(surf, anyShot);
                SDL_FreeSurface(surf);
            }
            run = false;
        }
        SDL_RenderPresent(ren);

        std::string title = "front-end demo";
        if (screen == Screen::Name)
            title += "  name: [" + gfx.lastText + "]";
        if (title != lastTitle)
        {
            SDL_SetWindowTitle(win, title.c_str());
            lastTitle = title;
        }
    }
};
