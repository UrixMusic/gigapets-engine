// memory_match.hpp - Memory Match minigame (RunMemoryMatchGame @ 0x02ebd6), header-only.
//
// Rules (from the ROM):
//   * 12 cards in a 4 x 3 grid (cols x rows), 6 item pairs. Cursor wraps in both directions.
//   * Z picks a card. Already-matched cards and the first-picked card can't be picked.
//   * After the 2nd pick: a matching pair resolves right away; a mismatch is shown for 2.0 s first.
//   * A mismatch costs a wrong guess. Game ends when all 6 pairs are found or the guesses run out
//     (maxWrong = 4, or 5 when the Lightbulbs stat > 199), then waits 2.0 s and finishes.
//   * Prizes = the items of every matched pair (up to 6), dropped via the same path as Swift Pick
//     (StartPetActivity 0x24). X cancels immediately and gives nothing.
//   * Cost: 20 Bucks, taken before play (LaunchMemoryMatchGame: AdjustPlayerMoney(-0x14)).
//
// The sim ticks at 30 Hz. The ROM's 2.0 s delay is (elapsed gameTicks / 59.94) > 2.0 with gameTicks +2 per
// tick, i.e. 60 sim ticks here.
#pragma once
#include <array>
#include <functional>
#include <vector>

namespace gp
{

    class MemoryMatch
    {
    public:
        static constexpr int kCols = 4, kRows = 3, kCards = 12, kPairs = 6;
        static constexpr int kCost = 20;
        static constexpr int kDelayTicks = 60; // 2.0 s at 30 Hz

        // Layout (screen px). Pitch 0x37 between cards.
        static constexpr int kPitch = 0x37;
        static constexpr int kTileX = 0x32, kTileY = 0x41; // card back / face tile origin
        static constexpr int kIconDX = 5, kIconDY = 5;     // item icon inside the face (0x37-0x32, 0x46-0x41)
        static constexpr int kCursorX = 0x2f, kCursorY = 0x3e;
        static constexpr int kSpriteBack = 0x471, kSpriteFace = 0x475, kSpriteCursor = 0x477;
        static constexpr int kFlipAnim = 0x7c, kFlipAnimPal = 0x28c;
        // Text (ROM strings: title 0xf1a0, counter format 0xf189 "Wrong Guesses Left: %d")
        static constexpr int kTitleY = 0x13, kCounterY = 0x2c, kTextX = 0x14, kTextW = 0x118;
        // Sound effects (ROM): move 0x2d vol 0xaf, pick 0x59 vol 0x7f, match 0x15 vol 0xff.
        static constexpr int kSfxMove = 0x2d, kSfxPick = 0x59, kSfxMatch = 0x15;

        enum class Event
        {
            None,
            Finished,
            Cancelled
        };
        enum class CardView
        {
            Back,
            Face,
            Anim
        }; // Anim = DrawMemoryCardBack: the 0x7c animation frame at the card

        // items: 6 distinct item ids (BuildUniqueRandomSet(out, 6, 2)).
        // rr(lo, hi): random int in [lo, hi), like the ROM's RandRange.
        // flipFrames: number of frames of anim 0x7c (played on a wrong guess).
        void start(const std::vector<int> &items, int maxWrong, int flipFrames,
                   const std::function<int(int, int)> &rr)
        {
            cards_.fill(-1);
            for (int i = 0; i < kPairs && i < int(items.size()); ++i)
                for (int copy = 0; copy < 2; ++copy)
                {
                    int remaining = kCards - (i * 2 + copy);
                    int k = rr(0, remaining); // k-th still-empty cell
                    for (int c = 0; c < kCards; ++c)
                        if (cards_[c] == -1 && k-- == 0)
                        {
                            cards_[c] = items[size_t(i)];
                            break;
                        }
                }
            matched_.fill(-1);
            matchedCount_ = wrong_ = 0;
            maxWrong_ = maxWrong;
            flipFrames_ = flipFrames;
            curR_ = curC_ = 0;
            clearPicks();
            state_ = State::Pick;
            open_ = true;
            bool reverse_ = false;
            stateTick_ = 0;
            animTick_ = 0;
            animPlaying_ = false;
            justMoved_ = justPicked_ = justMatched_ = false;
        }

        bool open() const { return open_; }

        // One 30 Hz tick. dirs are edge presses (newly pressed this tick).
        Event tick(bool left, bool right, bool up, bool down, bool zPressed, bool xPressed)
        {
            justMoved_ = justPicked_ = justMatched_ = false;
            if (!open_)
                return Event::None;
            ++stateTick_;
            if (animPlaying_)
                ++animTick_;

            switch (state_)
            {
            case State::Pick:
                if (xPressed)
                {
                    open_ = false;
                    return Event::Cancelled; // ROM: event 5 returns straight away, no prizes
                }
                if (zPressed)
                {
                    const int cell = curR_ * kCols + curC_;
                    if (!isMatched(cards_[size_t(cell)]))
                    {
                        if (firstR_ < 0)
                        {
                            firstR_ = curR_;
                            firstC_ = curC_;
                        }
                        else if (firstR_ == curR_ && firstC_ == curC_)
                            break; // same card twice: ignored
                        else
                        {
                            secondR_ = curR_;
                            secondC_ = curC_;
                            stateTick_ = 0; // the 2 s mismatch timer starts at the 2nd pick
                        }
                        state_ = State::Flip;
                        animTick_ = 0;
                        animPlaying_ = true;
                        reverse_ = false;
                        justPicked_ = true;
                    }
                }
                else if (left || right || up || down)
                {
                    if (left)
                        curC_ = (curC_ + kCols - 1) % kCols;
                    if (right)
                        curC_ = (curC_ + 1) % kCols;
                    if (up)
                        curR_ = (curR_ + kRows - 1) % kRows;
                    if (down)
                        curR_ = (curR_ + 1) % kRows;
                    justMoved_ = true;
                }
                break;

            case State::Flip: // ROM state 1: waits for the flip animation (idle here: one tick)
                if (!animBusy())
                {
                    state_ = secondR_ >= 0 ? State::Judge : State::Pick;
                    stateTick_ = secondR_ >= 0 ? stateTick_ : 0;
                    animPlaying_ = false;
                }
                break;

            case State::Judge: // ROM state 2
            {
                const bool same = cardAt(firstR_, firstC_) == cardAt(secondR_, secondC_);
                if (stateTick_ <= kDelayTicks && !same)
                    break; // mismatch stays face up for 2 s; a match resolves immediately
                if (same)
                {
                    matched_[size_t(matchedCount_++)] = cardAt(firstR_, firstC_);
                    justMatched_ = true;
                }
                else
                {
                    ++wrong_;
                    animTick_ = 0;
                    animPlaying_ = true; // PlaySpriteAnimation(0x7c, ...) on a wrong guess
                    reverse_ = true;     // add this line (flags = 4: plays last frame to first)
                }
                state_ = State::Resolve;
                break;
            }

            case State::Resolve: // ROM state 3
                if (!animBusy())
                {
                    clearPicks();
                    animPlaying_ = false;
                    if (matchedCount_ < kPairs && wrong_ < maxWrong_)
                        state_ = State::Pick;
                    else
                    {
                        state_ = State::End;
                        stateTick_ = 0;
                    }
                }
                break;

            case State::End: // ROM state 4: 2.0 s, then copy the prize list and leave
                if (stateTick_ > kDelayTicks)
                {
                    open_ = false;
                    return Event::Finished;
                }
                break;
            }
            return Event::None;
        }

        // ---- drawing / host info ----
        int cursorRow() const { return curR_; }
        int cursorCol() const { return curC_; }
        int wrongLeft() const { return maxWrong_ - wrong_; }
        int cardAt(int r, int c) const { return cards_[size_t(r * kCols + c)]; }
        std::vector<int> rewards() const { return std::vector<int>(matched_.begin(), matched_.begin() + matchedCount_); }
        bool justMoved() const { return justMoved_; }     // play kSfxMove
        bool justPicked() const { return justPicked_; }   // play kSfxPick
        bool justMatched() const { return justMatched_; } // play kSfxMatch

        // What to draw for a card (ROM draw loop in RunMemoryMatchGame).
        CardView view(int r, int c) const
        {
            if (isMatched(cardAt(r, c)))
                return CardView::Face;
            const bool isFirst = firstR_ == r && firstC_ == c;
            const bool isSecond = secondR_ == r && secondC_ == c;
            if (!isFirst && !isSecond)
                return CardView::Back;
            if (state_ == State::Flip)
            {
                if (isFirst)
                    return secondR_ >= 0 ? CardView::Face : CardView::Anim;
                return CardView::Anim;
            }
            return state_ == State::Resolve ? CardView::Anim : CardView::Face;
        }

        int animFrame() const
        {
            if (!animPlaying_ || flipFrames_ <= 0)
                return 0;
            int f = animTick_ * 14 / 30;
            if (f >= flipFrames_)
                f = flipFrames_ - 1;
            return reverse_ ? flipFrames_ - 1 - f : f;
        }
        int animYOffset() const
        {
            const int f = animFrame();
            return f == 0 ? -3 : f == 1 ? -5
                             : f == 2   ? -3
                                        : 0; // DAT_001fca == 1 (verified in emulator)
        }

    private:
        enum class State
        {
            Pick,
            Flip,
            Judge,
            Resolve,
            End
        };
        bool animBusy() const { return animPlaying_ && animTick_ * 14 / 30 < flipFrames_; }
        bool isMatched(int item) const
        {
            for (int m : matched_)
                if (m == item)
                    return true;
            return false;
        }
        void clearPicks() { firstR_ = firstC_ = secondR_ = secondC_ = -1; }

        std::array<int, kCards> cards_{};
        std::array<int, kPairs> matched_{};
        int matchedCount_ = 0, wrong_ = 0, maxWrong_ = 4, flipFrames_ = 0;
        int curR_ = 0, curC_ = 0, firstR_ = -1, firstC_ = -1, secondR_ = -1, secondC_ = -1;
        State state_ = State::Pick;
        bool reverse_ = false;
        bool open_ = false, animPlaying_ = false;
        bool justMoved_ = false, justPicked_ = false, justMatched_ = false;
        int stateTick_ = 0, animTick_ = 0;
    };

} // namespace gp