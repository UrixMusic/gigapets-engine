// trick_guess.hpp - the Trick Teacher's code game (RunTrickGuessingMinigame @ 0x02f1cd), header-only.
//
// Rules (from the ROM):
//   * The secret code is the pet's traits (g_petTraits): 5 values, each 0..3 = left, right, up, down. A level uses
//     the first 3 / 4 / 5 of them (teachers 0x30 / 0x31 / 0x32 via AttemptTrickLevel1 / 2 / 3). The code only
//     changes when a trick is learned (SetTricksKnownAndRerollTraits).
//   * An attempt is one arrow press per slot. Each press plays the "right" sound (0x15) if it matches the code at
//     that slot, else the "wrong" one (0x59), and the slot gets a check or a cross. Z does nothing.
//   * After the last slot the result text shows for 3.5 s. Then: cracked, or out of attempts, ends the game;
//     otherwise a new attempt starts with the SAME code. Attempts = 3, or 4 when Lightbulbs > 199.
//   * CANCEL leaves at any time while guessing. The ROM returns 0 for that, the same as running out of attempts.
//
// The sim ticks at 30 Hz. The ROM's 3.5 s delay is (elapsed gameTicks / 59.94) > 3.5 with gameTicks +2 per tick,
// i.e. 105 sim ticks here.
#pragma once
#include <array>

namespace gp
{

    class TrickGuess
    {
    public:
        static constexpr int kMaxCode = 5;
        static constexpr int kDelayTicks = 105; // 3.5 s at 30 Hz

        // Layout (screen px). Everything is drawn with palette 0x28c except the title (font 3, palette 0x104).
        static constexpr int kPortraitX = 0x19, kPortraitY = 0x41;
        static constexpr int kTextX = 0x14, kTextW = 0x118;
        static constexpr int kTitleY = 0x1e, kHintY = 0xc0;
        static constexpr int kBodyX = 0xf, kBodyW = 0x122, kBodyMidY = 0x59; // body text is centred on kBodyMidY
        // DrawFormattedText's last two arguments: the first kBodyIndentH px of the text are 0x3c narrower and shifted right,
        // which keeps it clear of the portrait at the left. This is what makes the body wrap to 4 lines.
        static constexpr int kBodyIndent = 0x3c, kBodyIndentH = 100;
        static constexpr int kSlotY = 0x83, kMarkY = 0x86;                   // empty slot frame / mark and cursor
        static constexpr int kSlotDX = -0x17, kMarkDX = -0x14;
        static constexpr int kSpriteSlot = 0x478, kSpriteCursor = 0x400, kSpriteRight = 0x476, kSpriteWrong = 0x479;
        static constexpr int kSpritePal = 0x28c;
        // Sound effects (ROM): right 0x15 vol 0xff, wrong 0x59 vol 0x7f.
        static constexpr int kSfxRight = 0x15, kSfxWrong = 0x59;
        // ROM strings (narrow): 0xf28f title, 0xf139 hint; the body text is one of these.
        static constexpr int kStrTitle = 0xf28f, kStrHint = 0xf139;
        static constexpr int kStrIntro = 0xf1ad;    // "To learn the %s trick, use the arrow keys to solve the code."
        static constexpr int kStrTriesLeft = 0xf1ea; // "You have %d more tries at solving the code."
        static constexpr int kStrLastTry = 0xf216;   // "You have 1 more try at solving the code."
        static constexpr int kStrCracked = 0xf23f;   // "You cracked the code and learned the trick!"
        static constexpr int kStrWrong = 0xf26b;     // "Sorry, that's not the correct code."

        enum class Event
        {
            None,
            Finished, // cracked, or out of attempts: ask solved()
            Cancelled // CANCEL: counts as a failure
        };
        enum class Text
        {
            Intro,
            TriesLeft,
            LastTry,
            Cracked,
            Wrong
        };

        // DrawGuessResultSlots: x of slot 0 and the pitch between slots.
        static void slotLayout(int length, int &base, int &pitch)
        {
            base = 0;
            pitch = 0x46;
            if (length == 3)
                base = 0x5a;
            else if (length == 4)
                base = 0x37;
            else if (length == 5)
            {
                base = 0x32;
                pitch = 0x37;
            }
        }

        // code: g_petTraits. length: 3..5. maxTries: attempts allowed.
        void start(const std::array<int, kMaxCode> &code, int length, int maxTries)
        {
            code_ = code;
            length_ = length < 1 ? 1 : length > kMaxCode ? kMaxCode
                                                         : length;
            maxTries_ = maxTries < 1 ? 1 : maxTries;
            attempts_ = guessed_ = right_ = waitTicks_ = 0;
            solved_ = false;
            text_ = Text::Intro;
            state_ = State::Begin;
            open_ = true;
            justRight_ = justWrong_ = false;
        }

        bool open() const { return open_; }

        // One 30 Hz tick. Arrows and x are edge presses (newly pressed this tick).
        // The ROM polls ONE button event per loop, so only one of them counts per tick.
        Event tick(bool left, bool right, bool up, bool down, bool xPressed)
        {
            justRight_ = justWrong_ = false;
            if (!open_)
                return Event::None;
            const int ev = xPressed ? 5 : left ? 0
                                      : right  ? 1
                                      : up     ? 2
                                      : down   ? 3
                                               : -1;

            if (state_ == State::Begin) // ROM state 0: new attempt (the same loop pass still handles this tick's press)
            {
                guessed_ = right_ = 0;
                text_ = attempts_ == 0 ? Text::Intro : maxTries_ - attempts_ > 1 ? Text::TriesLeft
                                                                                 : Text::LastTry;
                state_ = State::Guess;
            }

            if (state_ == State::Guess) // ROM state 1
            {
                if (ev == 5)
                {
                    open_ = false;
                    return Event::Cancelled;
                }
                if (ev >= 0 && ev <= 3)
                {
                    if (ev == code_[size_t(guessed_)])
                    {
                        ++right_;
                        justRight_ = true;
                    }
                    else
                        justWrong_ = true;
                    guess_[size_t(guessed_++)] = ev;
                    if (guessed_ >= length_)
                        state_ = State::Judge;
                }
            }

            if (state_ == State::Judge) // ROM state 2: runs in the same pass as the last press
            {
                solved_ = right_ >= length_;
                text_ = solved_ ? Text::Cracked : Text::Wrong;
                waitTicks_ = 0;
                state_ = State::Wait;
            }

            if (state_ == State::Wait) // ROM state 3
            {
                if (waitTicks_++ >= kDelayTicks)
                {
                    ++attempts_;
                    if (attempts_ >= maxTries_ || solved_)
                    {
                        open_ = false;
                        return Event::Finished;
                    }
                    state_ = State::Begin;
                }
            }
            return Event::None;
        }

        // ---- drawing / host info ----
        bool solved() const { return solved_; }
        Text text() const { return text_; }
        int length() const { return length_; }
        int guessCount() const { return guessed_; } // slots filled in this attempt
        int triesLeft() const { return maxTries_ - attempts_; }
        bool markRight(int slot) const { return guess_[size_t(slot)] == code_[size_t(slot)]; } // slot < guessCount()
        bool justRight() const { return justRight_; }                                          // play kSfxRight
        bool justWrong() const { return justWrong_; }                                          // play kSfxWrong

    private:
        enum class State
        {
            Begin,
            Guess,
            Judge,
            Wait
        };
        std::array<int, kMaxCode> code_{}, guess_{};
        int length_ = 3, maxTries_ = 3, attempts_ = 0, guessed_ = 0, right_ = 0, waitTicks_ = 0;
        bool solved_ = false, open_ = false, justRight_ = false, justWrong_ = false;
        Text text_ = Text::Intro;
        State state_ = State::Begin;
    };

} // namespace gp
