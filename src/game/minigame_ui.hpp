// minigame_ui.hpp - Swift Pick, Three in a Row (wishing wells) and Memory Match (game tents):
// start, tick and draw. Moved out of walk.cpp's runWalk without changing behavior.
//
// Everything the minigames need from the rest of the game comes in through MinigameEnv (references to shared
// objects plus a few callbacks), so this file never touches walk.cpp's locals.
#pragma once
#include "../engine/display.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../loaders/import.hpp"
#include "../engine/render_content.hpp"
#include "../game/player.hpp"
#include "../game/dialogue.hpp"
#include "../game/item.hpp"
#include "../game/offerings.hpp"
#include "../game/menu_draw.hpp"
#include "../game/swift_pick.hpp"
#include "../engine/sprite_cache.hpp"
#include "../game/three_in_a_row.hpp"
#include "../game/memory_match.hpp"
#include "../game/trick_guess.hpp"
#include "../game/item_pools.hpp"
#include "../game/text_draw.hpp"


inline std::string readRomString(const gp::Rom &rom, int addr) // (was in walk.cpp)
{
    std::string s;
    for (int c; (c = rom.u16(addr++) & 0xff) != 0 && s.size() < 100;)
        s += char(c);
    return s;
}

// What the minigames get from the game. The first group are shared objects; the callbacks are set once in walk.cpp.
struct MinigameEnv
{
    SDL_Renderer *ren;
    const gp::Rom &rom;
    gp::ContentDB &db;
    gp::Player &player;
    const std::vector<gp::ItemDef> &itemDefs;
    TextDrawer &text;  // body text (font 0)
    TextDrawer &title; // minigame titles (font 3, palette 0x104)
    SDL_Texture *bg;   // InitScreenState(0, 0x5c3): the background all three share

    std::function<int(int, int)> randIn;                       // lo..hi INCLUSIVE
    std::function<void(int)> sfx;
    std::function<AnimRef(const gp::Id &)> animRefById;
    std::function<void()> drawSlotScene;                       // DrawActiveSlotSprites(): clear, queue, sort, draw
    std::function<void(std::function<void()>)> requestScreen;  // run something while the screen is black
    std::function<void(gp::DialogueBox)> showBox;              // dialogue.show(...)
    std::function<void(std::vector<int>)> startItemDrop;       // the pet drops prizes at (petX, petY)
};

// One frame of input for the minigame that is open. `live` is false during the fade-in, so presses are ignored.
struct MinigameInput
{
    bool live = false;
    bool z = false, x = false;
    bool left = false, right = false, up = false, down = false;
};

class Minigames
{
public:
    struct Well
    {
        int area, slot, cost, x, y;
        std::string needText, askText; // text ids: "not enough Bucks" / "throw N Bucks in?"
    };
    struct Tent
    {
        int area, slot, x, y;
        std::string needText, askText; // text ids: "not enough Bucks" / "buy a ticket?"
    };

    // Where the pet stands for the catch / where prizes land (StartPetActivity 0x24). Read by the item-drop code.
    int petX = 0, petY = 0;

    explicit Minigames(MinigameEnv &env) : e_(env) {}
    Minigames(const Minigames &) = delete; // lambdas below capture `this`
    Minigames &operator=(const Minigames &) = delete;

    // What the Trick Teacher's game needs to know about the lesson: level 1..3, the trick's name and the teacher's portrait.
    struct TrickLesson
    {
        int level = 1;
        std::string trickName;
        int portraitSprite = 0, portraitPal = gp::kMenuPalette;
    };

    bool active() const { return swiftShown_ || threeShown_ || memoryShown_ || trickShown_; }

    // ---- entry points (called from interactions) ----
    void requestSwiftPick()
    {
        e_.requestScreen([this]
                         { startSwiftPick(); swiftShown_ = true; });
    }

    void openWell(const Well &w)
    {
        const bool big = w.cost >= 100;
        if (big && !gp::ThreeInARow::kPrizePoolsKnown)
        {
            e_.showBox(gp::messageBox("(100 Bucks well: prize tables not dumped yet)"));
            return;
        }
        if (e_.player.money < w.cost) // ROM: money > 4 (or > 99)
        {
            e_.showBox(gp::messageBox(e_.db.textOr(w.needText))); // "You need N Bucks to make a wish."
            return;
        }
        gp::DialogueBox b = gp::messageBox(e_.db.textOr(w.askText)); // "Would you like to throw N Bucks into the well?"
        b.kind = gp::BoxKind::Confirm;
        b.onYes = [this, w]
        { e_.requestScreen([this, w]
                           { startThreeInARow(w); threeShown_ = true; }); };
        b.onNo = [] {};
        e_.showBox(std::move(b));
    }

    // Game tents: area 3 slot 8, area 7 slot 50. Both share one dialogue; only the prize target point differs.
    void openTent(const Tent &t)
    {
        // ROM: money > 19 (32-bit compare) -> confirm box, else a plain message.
        if (e_.player.money < memoryCost())
        {
            e_.showBox(gp::messageBox(e_.db.textOr(t.needText))); // "You don't have enough Bucks to buy a ticket to the game tent."
            return;
        }
        gp::DialogueBox b = gp::messageBox(e_.db.textOr(t.askText)); // "Would you like to buy a ticket to the game tent for 20 Bucks?"
        b.kind = gp::BoxKind::Confirm;
        b.onYes = [this, t]
        {
            petX = t.x;
            petY = t.y;
            e_.requestScreen([this]
                             { startMemoryMatch(); memoryShown_ = true; });
        };
        b.onNo = [] {};
        e_.showBox(std::move(b));
    }

    // PlayRandomMinigame (0x017d60), the "Would you like to play a game?" answer from a friend: RandRange(1, 4) picks
    // Swift Pick, Three in a Row or Memory Match. Free of charge. Prizes drop at (x, y); the ROM passes the pet's
    // position and 10 below it.
    void openRandomGame(int x, int y)
    {
        const int pick = e_.randIn(1, 3);
        petX = x;
        petY = y;
        e_.requestScreen([this, pick, x, y]
                         {
                             if (pick == 1)
                             {
                                 startSwiftPick(true);
                                 swiftShown_ = true;
                             }
                             else if (pick == 2)
                             {
                                 startThreeInARow(Well{0, 0, 0, x, y, "", ""});
                                 threeShown_ = true;
                             }
                             else
                             {
                                 startMemoryMatch(true);
                                 memoryShown_ = true;
                             }
                         });
    }

    // Trick Teacher: fade to the code game. done(won) runs while the screen is black on the way back, so it may
    // open a dialogue box. CANCEL and running out of attempts both report false (the ROM returns 0 for both).
    void openTrick(TrickLesson lesson, std::function<void(bool)> done)
    {
        trickDone_ = std::move(done);
        e_.requestScreen([this, l = std::move(lesson)]
                         { startTrick(l); trickShown_ = true; });
    }

    // ---- once per game tick while active() ----
    void tick(const MinigameInput &in)
    {
        if (swiftShown_)
        {
            const gp::SwiftPick::Event ev = swift_.tick(in.live && in.z, in.live && in.x);
            if (swift_.justPressed())
                e_.sfx(0x59);
            if (ev == gp::SwiftPick::Event::GotAway)
                e_.sfx(1);
            else if (ev == gp::SwiftPick::Event::Caught)
                e_.startItemDrop({swift_.result()});
            if (!swift_.open()) // caught, got away or cancelled: fade back to the world
                e_.requestScreen([this]
                                 { swiftShown_ = false; });
            return;
        }
        if (threeShown_)
        {
            const gp::ThreeInARow::Event ev = three_.tick(in.live && in.z, in.live && in.x);
            if (three_.justStopped())
                e_.sfx(three_.improved() ? 0x15 : 0x59);
            if (ev == gp::ThreeInARow::Event::Finished)
                e_.startItemDrop(three_.rewards());
            if (!three_.open()) // finished or cancelled: fade back to the world
                e_.requestScreen([this]
                                 { threeShown_ = false; });
            return;
        }
        if (memoryShown_)
        {
            const gp::MemoryMatch::Event ev = memory_.tick(in.live && in.left, in.live && in.right,
                                                           in.live && in.up, in.live && in.down,
                                                           in.live && in.z, in.live && in.x);
            if (memory_.justMoved())
                e_.sfx(gp::MemoryMatch::kSfxMove);
            if (memory_.justPicked())
                e_.sfx(gp::MemoryMatch::kSfxPick);
            if (memory_.justMatched())
                e_.sfx(gp::MemoryMatch::kSfxMatch);
            if (ev == gp::MemoryMatch::Event::Finished && !memory_.rewards().empty())
                e_.startItemDrop(memory_.rewards()); // ROM: StartPetActivity(0x24, x, y, 6, 0) only if >= 1 pair matched
            if (!memory_.open())
                e_.requestScreen([this]
                                 { memoryShown_ = false; });
            return;
        }
        if (trickShown_)
        {
            const gp::TrickGuess::Event ev = trick_.tick(in.live && in.left, in.live && in.right,
                                                         in.live && in.up, in.live && in.down, in.live && in.x);
            if (trick_.justRight())
                e_.sfx(gp::TrickGuess::kSfxRight);
            if (trick_.justWrong())
                e_.sfx(gp::TrickGuess::kSfxWrong);
            if (!trick_.open()) // cracked, out of attempts or cancelled: fade back, then report
            {
                const bool won = ev == gp::TrickGuess::Event::Finished && trick_.solved();
                e_.requestScreen([this, won]
                                 {
                                     trickShown_ = false;
                                     std::function<void(bool)> done = std::move(trickDone_);
                                     trickDone_ = nullptr;
                                     if (done)
                                         done(won);
                                 });
            }
        }
    }

    // ---- once per frame, after the world is drawn ----
    void draw(float renderAlpha)
    {
        if (swiftShown_)
            drawSwiftPick(renderAlpha);
        if (threeShown_)
            drawThreeInARow();
        if (memoryShown_)
            drawMemoryMatch();
        if (trickShown_)
            drawTrickGuess();
    }

private:
    // ---- shared drawing ----
    void backdrop()
    {
        gp::clearFrame(e_.ren);
        SDL_SetTextureBlendMode(e_.bg, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(e_.ren, e_.bg, nullptr, nullptr);
        e_.drawSlotScene(); // DrawActiveSlotSprites() is part of every one of these screens
    }

    // UI sprite tile shared by the minigames (kMenuPalette). A sprite that was never imported warns once.
    void tileF(int sprite, float x, float y)
    {
        const AnimRef A = e_.animRefById(gp::uiSpriteId(sprite, gp::kMenuPalette));
        if (!A.size())
        {
            if (warnedUiSprites_.insert(sprite).second)
                std::printf("missing UI sprite 0x%x: add it to the import list (the {0x400, 0x40a, ...} loop)\n", sprite);
            return;
        }
        const gp::SpriteFrame &s = A.anim->frames[0];
        SDL_FRect r{x - s.anchorX + s.dx, y - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
        SDL_RenderCopyF(e_.ren, (*A.tex)[0], nullptr, &r);
    }
    // Same, with a palette: the Memory Match card tiles need 0x28c.
    void tilePalF(int sprite, int pal, float x, float y)
    {
        const AnimRef A = e_.animRefById(gp::uiSpriteId(sprite, pal));
        if (!A.size())
        {
            if (warnedUiSprites_.insert(sprite ^ (pal << 16)).second)
                std::printf("missing UI sprite 0x%x pal 0x%x\n", sprite, pal);
            return;
        }
        const gp::SpriteFrame &s = A.anim->frames[0];
        SDL_FRect r{x - s.anchorX + s.dx, y - s.anchorY + s.dy + 1, float(s.w), float(s.h)};
        SDL_RenderCopyF(e_.ren, (*A.tex)[0], nullptr, &r);
    }

    // ---- Swift Pick (area 0, slot 98): setup from ProcessStoryModeObjectInteraction, then RunSwiftPickGame ----
    void startSwiftPick(bool random = false)
    {
        const gp::MinigameDef &mg = e_.db.minigameOr("base:minigame/swift_pick");              // pool and numbers: minigame_defs.hpp
        const bool fast = e_.player.personality[gp::Lightbulbs] > mg.get("fastAbove", 199);    // GetPersonalityStat(3) > 0xc7
        const int speed = fast ? mg.get("speedFast", 10) : mg.get("speed", 12);
        if (random) // PlayRandomMinigame: 10 unique items (BuildUniqueRandomSet(out, 10, 2)), prizes at the caller's point
        {
            auto rr = [this](int lo, int hi)
            { return e_.randIn(lo, hi - 1); };
            swift_.start(gp::buildUniqueRandomSet(10, 2, rr), speed);
            return;
        }
        petX = mg.get("petX", 0x628) + e_.randIn(-mg.get("petXJitter", 10), mg.get("petXJitter", 10));
        petY = mg.get("petY", 0x4c2) + e_.randIn(-mg.get("petYJitter", 15), mg.get("petYJitter", 15));
        swift_.start(mg.list("pool"), speed);
    }

    void drawSwiftPick(float renderAlpha)
    {
        backdrop();
        e_.title.draw(readRomString(e_.rom, 0xf111), 0x14, 0x23, 0x118, gp::Align::Center); // "Swift Pick"
        e_.text.draw(readRomString(e_.rom, 0xf11c), 0x14, 0x50, 0x118, gp::Align::Center);  // "Press SELECT to get an item"
        e_.text.draw(readRomString(e_.rom, 0xf139), 0x14, 0xb9, 0x118, gp::Align::Center);  // "Press CANCEL to exit"
        const float glide = (1.f - renderAlpha) * swift_.lastMove();                        // 0 when smoothing is off
        tileF(0x400, gp::SwiftPick::kFrameX, gp::SwiftPick::kCellY);
        for (int k = 0; k < gp::SwiftPick::kCells; ++k)
            tileF(e_.itemDefs[swift_.cellItem(k)].iconSprite, swift_.cellX(k) + glide, gp::SwiftPick::kCellY);
    }

    // ---- Wishing wells = Three in a Row (RunThreeInARowGame) ----
    // The well placements are InteractionDefs (action "well"); the 100-Bucks one is ThreeInARowGigalympia.
    void startThreeInARow(const Well &w)
    {
        e_.player.adjustMoney(-w.cost); // taken before play, even if you cancel
        const gp::MinigameDef &mg = e_.db.minigameOr("base:minigame/three_in_a_row");
        const bool fast = e_.player.personality[gp::Lightbulbs] > mg.get("fastAbove", 199);
        const std::vector<int> &pl = mg.list(fast ? "periodsFast" : "periods");
        const std::array<int, 3> periods = pl.size() == 3 ? std::array<int, 3>{pl[0], pl[1], pl[2]}
                                           : fast         ? std::array<int, 3>{16, 14, 12}
                                                          : std::array<int, 3>{14, 12, 10};
        auto rr = [this](int lo, int hi)
        { return e_.randIn(lo, hi - 1); }; // the ROM's RandRange is exclusive at the top
        const int n = rr(mg.get("itemsLo", 6), mg.get("itemsHi", 9));
        petX = w.x;
        petY = w.y;
        std::vector<int> items;
        if (w.cost >= 100)
        {
            const int special = gp::ThreeInARow::kPrizeSpecial;
            const bool includeSpecial = e_.player.itemCount(gp::legacyItemId(special)) == 0 &&
                                        !e_.player.hasFlag(gp::offeredKey(special));
            items = gp::ThreeInARow::pickPrizeItems(n, includeSpecial, rr);
        }
        else
            items = gp::ThreeInARow::pickItems(n, rr);
        three_.start(std::move(items), periods, gp::ThreeInARow::kMoneyItem, rr);
    }

    void drawThreeInARow()
    {
        backdrop(); // same background and slot scene as Swift Pick
        e_.title.draw(readRomString(e_.rom, 0xf14f), 0x14, 0x1e, 0x118, gp::Align::Center); // "Three in a Row"
        e_.text.draw(readRomString(e_.rom, 0xf15e), 0x14, 0x41, 0x118, gp::Align::Center);  // "Match 2 items to win. Match 3 for a bonus"
        e_.text.draw(readRomString(e_.rom, 0xf139), 0x14, 0xbe, 0x118, gp::Align::Center);  // "Press CANCEL to exit"
        for (int i = 0; i < gp::ThreeInARow::kReels; ++i)
        {
            const int x = gp::ThreeInARow::kIconX + i * gp::ThreeInARow::kSpacing;
            if (i == three_.stopped()) // the reel waiting to stop
                tileF(0x400, float(x), float(gp::ThreeInARow::kY));
            else
                tileF(0x478, float(x - 3), float(0x7f));
            tileF(e_.itemDefs[three_.item(i)].iconSprite, float(x), float(gp::ThreeInARow::kY));
        }
    }

    // ---- Memory Match ----
    const gp::MinigameDef &memoryDef() const { return e_.db.minigameOr("base:minigame/memory_match"); }
    int memoryCost() const { return memoryDef().get("cost", gp::MemoryMatch::kCost); }

    void startMemoryMatch(bool free = false) // free: a friend's random game, no ticket
    {
        const gp::MinigameDef &mg = memoryDef();
        if (!free)
            e_.player.adjustMoney(-memoryCost()); // taken before play, even if you cancel
        const bool fast = e_.player.personality[gp::Lightbulbs] > mg.get("fastAbove", 199);
        const int maxWrong = fast ? mg.get("maxWrongFast", 5) : mg.get("maxWrong", 4);
        auto rr = [this](int lo, int hi)
        { return e_.randIn(lo, hi - 1); }; // ROM RandRange is exclusive at the top

        // BuildUniqueRandomSet(out, 6, 2): 6 DISTINCT items, the first 2 from kPoolFirst, the rest from kPoolGeneral,
        // then shuffled (item_pools.hpp, tables read from the ROM and matching the MAME dump).
        std::vector<int> items = gp::buildUniqueRandomSet(gp::MemoryMatch::kPairs, 2, rr);

        const int flipFrames = int(e_.animRefById("base:anim/" + std::to_string(gp::MemoryMatch::kFlipAnim) + "@28c").size());
        memory_.start(items, maxWrong, flipFrames, rr);
    }

    void drawMemoryMatch()
    {
        using MM = gp::MemoryMatch;
        backdrop();
        e_.title.draw(readRomString(e_.rom, 0xf1a0), MM::kTextX, MM::kTitleY, MM::kTextW, gp::Align::Center); // "Memory Match"
        char buf[48];
        std::snprintf(buf, sizeof buf, "Wrong Guesses Left: %d", memory_.wrongLeft()); // fmt string at 0xf189
        e_.text.draw(buf, MM::kTextX, MM::kCounterY, MM::kTextW, gp::Align::Center);

        tilePalF(MM::kSpriteCursor, 0x28c, float(memory_.cursorCol() * MM::kPitch + MM::kCursorX),
                 float(memory_.cursorRow() * MM::kPitch + MM::kCursorY));
        for (int r = 0; r < MM::kRows; ++r)
            for (int c = 0; c < MM::kCols; ++c)
            {
                const float x = float(c * MM::kPitch + MM::kTileX), y = float(r * MM::kPitch + MM::kTileY);
                switch (memory_.view(r, c))
                {
                case MM::CardView::Back:
                    tilePalF(MM::kSpriteBack, 0x28c, x, y);
                    break;
                case MM::CardView::Face:
                    tilePalF(MM::kSpriteFace, 0x28c, x, y);
                    tileF(e_.itemDefs[memory_.cardAt(r, c)].iconSprite, x + MM::kIconDX, y + MM::kIconDY);
                    break;
                case MM::CardView::Anim: // DrawMemoryCardBack: anim 0x7c's current frame, hopping -3/-5/-3
                {
                    const AnimRef A = e_.animRefById("base:anim/" + std::to_string(MM::kFlipAnim) + "@28c");
                    if (!A.size())
                    {
                        tilePalF(MM::kSpriteBack, 0x28c, x, y);
                        break;
                    }
                    const int f = std::min(memory_.animFrame(), int(A.size()) - 1);
                    const gp::SpriteFrame &s = A.anim->frames[f];
                    SDL_FRect rc{x - s.anchorX + s.dx, y + memory_.animYOffset() - s.anchorY + s.dy, float(s.w), float(s.h)};
                    SDL_RenderCopyF(e_.ren, (*A.tex)[f], nullptr, &rc);
                    break;
                }
                }
            }
    }

    // ---- Trick Teacher (RunTrickGuessingMinigame, called from AttemptTrickLevel1..3) ----
    void startTrick(const TrickLesson &l)
    {
        const gp::MinigameDef &mg = e_.db.minigameOr("base:minigame/trick_guess");
        const bool fast = e_.player.personality[gp::Lightbulbs] > mg.get("fastAbove", 199);
        const int tries = fast ? mg.get("triesFast", 4) : mg.get("tries", 3);
        const std::vector<int> &lens = mg.list("codeLengths");
        const size_t k = size_t(std::max(l.level, 1) - 1);
        const int length = k < lens.size() ? lens[k] : int(k) + 3; // 3, 4, 5 characters of the pet's traits
        lesson_ = l;
        trick_.start(e_.player.trickCode, length, tries);
    }

    std::string trickBody() const
    {
        using TG = gp::TrickGuess;
        switch (trick_.text())
        {
        case TG::Text::Intro:
            return gp::fmtS(readRomString(e_.rom, TG::kStrIntro), {lesson_.trickName});
        case TG::Text::TriesLeft:
        {
            std::string s = readRomString(e_.rom, TG::kStrTriesLeft); // "%d more tries"
            const size_t p = s.find("%d");
            if (p != std::string::npos)
                s.replace(p, 2, std::to_string(trick_.triesLeft()));
            return s;
        }
        case TG::Text::LastTry:
            return readRomString(e_.rom, TG::kStrLastTry);
        case TG::Text::Cracked:
            return readRomString(e_.rom, TG::kStrCracked);
        case TG::Text::Wrong:
            break;
        }
        return readRomString(e_.rom, TG::kStrWrong);
    }

    void drawTrickGuess()
    {
        using TG = gp::TrickGuess;
        backdrop();
        tilePalF(lesson_.portraitSprite, lesson_.portraitPal, float(TG::kPortraitX), float(TG::kPortraitY));
        e_.title.draw(readRomString(e_.rom, TG::kStrTitle), TG::kTextX, TG::kTitleY, TG::kTextW, gp::Align::Center); // "Trick Teacher"
        const std::string body = trickBody();
        const int h = e_.text.layout(body, TG::kBodyX, 0, TG::kBodyW, gp::Align::Center, TG::kBodyIndent, TG::kBodyIndentH).height;
        e_.text.draw(body, TG::kBodyX, TG::kBodyMidY - h / 2, TG::kBodyW, gp::Align::Center, // centred on kBodyMidY
                     TG::kBodyIndent, TG::kBodyIndentH);

        // DrawGuessResultSlots: the slot being filled gets the cursor, filled slots a check or a cross.
        int base = 0, pitch = 0;
        TG::slotLayout(trick_.length(), base, pitch);
        for (int i = 0; i < trick_.length(); ++i)
        {
            const float x = float(base + i * pitch);
            if (i == trick_.guessCount())
                tilePalF(TG::kSpriteCursor, TG::kSpritePal, x + TG::kMarkDX, float(TG::kMarkY));
            else
                tilePalF(TG::kSpriteSlot, TG::kSpritePal, x + TG::kSlotDX, float(TG::kSlotY));
            if (i < trick_.guessCount())
                tilePalF(trick_.markRight(i) ? TG::kSpriteRight : TG::kSpriteWrong, TG::kSpritePal,
                         x + TG::kMarkDX, float(TG::kMarkY));
        }
        e_.text.draw(readRomString(e_.rom, TG::kStrHint), TG::kTextX, TG::kHintY, TG::kTextW, gp::Align::Center); // "Press CANCEL to exit"
    }

    MinigameEnv &e_;
    gp::SwiftPick swift_;
    gp::ThreeInARow three_;
    gp::MemoryMatch memory_;
    gp::TrickGuess trick_;
    TrickLesson lesson_;
    std::function<void(bool)> trickDone_;
    bool swiftShown_ = false, threeShown_ = false, memoryShown_ = false, trickShown_ = false;
    std::set<int> warnedUiSprites_;
};