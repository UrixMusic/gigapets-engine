// game/test_mode.hpp
// The ROM's factory test mode: TestModeMenu (0x045d9d) and the tests it starts, plus the sequence that gets there
// (HandleTestModeEntrySequence 0x03bd60). Pure logic; the screens are drawn in frontend_demo.cpp.
//
// Entry, on the logo screen: hold Left + Select for 3 s (a chime, PlaySoundEffect(0x59, 0xbe)), then press Up, Down, Menu, Cancel. A
// wrong button, or taking too long, gives the attempt up. (The ROM only looks for this when its test-mode flag is set by the
// hardware; here it is always on.) How long the sequence may take after the chime is not read from the ROM: 5 s is a guess.
//
// Rows: BUTTON TEST, SOUND TEST, MEMORY TEST, VIDEO TEST, HH-TV TEST, PET CARRIER TEST, EXIT TEST MODE. The ROM's button events
// are 0 Left, 1 Right, 2 Up, 3 Down, 4 Select, 5 Cancel, 6 Menu.
#pragma once
#include <cstdint>

namespace gp::testmode
{
    enum Ev
    {
        Left = 0,
        Right = 1,
        Up = 2,
        Down = 3,
        Select = 4,
        Cancel = 5,
        Menu = 6
    };

    constexpr int kRows = 7;
    constexpr const char *kRowNames[kRows] = {"BUTTON TEST", "SOUND TEST", "MEMORY TEST", "VIDEO TEST", "HH-TV TEST", "PET CARRIER TEST", "EXIT TEST MODE"};
    enum Row
    {
        RowButton = 0,
        RowSound,
        RowMemory,
        RowVideo,
        RowHandheld,
        RowCarrier,
        RowExit
    };
    constexpr const char *kTitle = "GigaPets", *kSubtitle = "TEST MODE", *kVersion = "1.34.1", *kDate = "Mar 17 2006";
    constexpr int kTicksPerSecond = 30;

    // MoveListSelectionWithWrap: one step, wrapping both ways.
    struct MenuList
    {
        int sel = 0;
        void move(int delta)
        {
            sel += delta;
            if (sel >= kRows)
                sel = 0;
            else if (sel < 0)
                sel = kRows - 1;
        }
    };

    // ---- getting in ----
    class EntrySequence
    {
    public:
        enum class Result
        {
            None,
            Chime,  // the hold was long enough: play the chime
            Entered // the whole sequence went in
        };
        static constexpr int kHoldTicks = 3 * kTicksPerSecond;
        static constexpr int kSequenceTicks = 5 * kTicksPerSecond; // GUESS
        static constexpr int kSequence[4] = {Up, Down, Menu, Cancel};

        // Once per 30 Hz tick. `holding`: Left and Select are both down.
        Result tick(bool holding)
        {
            if (phase_ == Phase::Hold)
            {
                held_ = holding ? held_ + 1 : 0;
                if (held_ >= kHoldTicks)
                {
                    phase_ = Phase::Sequence;
                    index_ = 0;
                    since_ = 0;
                    return Result::Chime;
                }
            }
            else if (phase_ == Phase::Sequence && ++since_ > kSequenceTicks)
                phase_ = Phase::Failed;
            return Result::None;
        }
        // A button went down.
        Result press(int ev)
        {
            if (phase_ != Phase::Sequence)
                return Result::None;
            if (ev == kSequence[index_])
            {
                if (++index_ == 4)
                {
                    phase_ = Phase::Done;
                    return Result::Entered;
                }
            }
            else
                phase_ = Phase::Failed;
            return Result::None;
        }
        // The chime has gone and the sequence is being typed: the logo waits for it.
        bool armed() const { return phase_ == Phase::Sequence; }
        bool failed() const { return phase_ == Phase::Failed; }
        void reset() { *this = EntrySequence(); }

    private:
        enum class Phase
        {
            Hold,
            Sequence,
            Failed,
            Done
        };
        Phase phase_ = Phase::Hold;
        int held_ = 0, index_ = 0, since_ = 0;
    };

    // ---- BUTTON TEST (RunButtonTestLoop 0x0454db) ----
    // Asks for the seven buttons in order: LEFT, RIGHT, UP, DOWN, SELECT, CANCEL, MENU. A right press says SUCCESS and goes on, a
    // wrong one says FAIL (with a mark and a sound) and the test starts over. The verdict stays 0.5 s, or until the next press.
    // After the seventh: "DONE WITH TEST." Holding Cancel for 2 s leaves, any time.
    constexpr const char *kButtonNames[7] = {"LEFT", "RIGHT", "UP", "DOWN", "SELECT", "CANCEL", "MENU"};
    constexpr int kButtonSteps = 7;
    constexpr int kVerdictTicks = kTicksPerSecond / 2, kExitHoldTicks = 2 * kTicksPerSecond;
    constexpr int kControllerSprite = 0x9ac, kControllerPalette = 0x48d, kControllerX = 0x14, kControllerY = 0x14;
    constexpr int kFailSprite = 0x9af, kFailPalette = 0x490, kFailX = 0xf0, kFailY = 0x96;

    // The lit button's sprite and offset on the controller picture (GetSequenceStepTextId, GetButtonTestIconOffsetX / Y).
    inline int buttonIconSprite(int ev) { return ev < 0 || ev > 6 ? -1 : ev == Select ? 0x9ad : 0x9ae; }
    inline int buttonIconX(int ev)
    {
        static constexpr int x[7] = {7, 0x16, 0xf, 0xf, 0x4e, 0x5b, 0x48};
        return ev < 0 || ev > 6 ? 0 : x[ev];
    }
    inline int buttonIconY(int ev)
    {
        static constexpr int y[7] = {0x10, 0x10, 8, 0x17, 4, 0x17, 0x17};
        return ev < 0 || ev > 6 ? 0 : y[ev];
    }

    struct ButtonTest
    {
        int step = 0;       // buttons got right so far (7 = done)
        int verdict = 0;    // 0 nothing, 1 SUCCESS, 2 FAIL
        bool failed = false; // the mark is up (the test starts over once the verdict goes)
        bool failSound = false; // set by the press that failed; the caller plays the sound and clears it
        int lit = -1;       // the button drawn lit on the controller
        bool showing = false;
        int showTicks = 0, cancelHeld = 0;
        bool exit = false;

        // Once per 30 Hz tick. ev: the button that went down this tick, -1 for none.
        void tick(int ev, bool cancelDown)
        {
            if (showing)
            {
                if (ev == -1 && showTicks < kVerdictTicks)
                    ++showTicks;
                else
                {
                    showing = false; // over: a new press is taken as the next one
                    verdict = 0;
                    if (failed)
                    {
                        failed = false;
                        step = 0;
                    }
                }
            }
            if (!showing)
            {
                lit = ev;
                if (ev != -1)
                {
                    if (step < kButtonSteps)
                    {
                        if (ev == step)
                        {
                            verdict = 1;
                            ++step;
                            failed = false;
                        }
                        else
                        {
                            verdict = 2;
                            failed = true;
                            failSound = true;
                        }
                    }
                    showing = true;
                    showTicks = 0;
                }
            }
            cancelHeld = cancelDown ? cancelHeld + 1 : 0;
            if (cancelHeld >= kExitHoldTicks)
                exit = true;
        }
        bool done() const { return step >= kButtonSteps; }
    };

    // ---- SOUND TEST (RunSoundTestLoop 0x045821) ----
    // Ids 0..0x5f are sound effects (played at volume 0x7f), 0x60..0x68 the nine songs. Left / Right: previous / next (wrapping
    // over all 0x69); Up: play the current one again; Down: play all, one after another, once the last has finished and a
    // second has passed; Cancel: leave. Sound 0 loops: it starts over when it ends.
    constexpr int kSoundIds = 0x69, kFirstSong = 0x60, kSoundVolume = 0x7f, kPlayAllGapTicks = 60;

    struct SoundTest
    {
        int id = 0;
        bool playAll = false;
        int sinceStart = 0; // ticks since the last sound was started

        void next() { id = id + 1 == kSoundIds ? 0 : id + 1; }
        void prev() { id = id - 1 < 0 ? kSoundIds - 1 : id - 1; }
        bool isSong() const { return id >= kFirstSong; }
        int songIndex() const { return id - kFirstSong; }
        void started() { sinceStart = 0; }
        // Down: switches the run on or off (on: the current one plays again at once).
        bool togglePlayAll()
        {
            playAll = !playAll;
            return playAll;
        }
        // Once per tick, with whether the current sound is still sounding: true when it is time for the next one.
        bool tick(bool stillPlaying)
        {
            ++sinceStart;
            return playAll && sinceStart > kPlayAllGapTicks && !stillPlaying;
        }
    };

    // ---- MEMORY TEST (RunMemoryTestLoop 0x04598f) ----
    // The ROM runs an EEPROM write test (PASS or FAIL) and prints a ROM checksum: the 16-bit sum of the words from word 0x4000 up
    // (TestEeprom 0x045935, which reads the ROM through the DMA in blocks of 0x100 words up to the end of the 4 M-word space).
    inline uint16_t romChecksum(uint32_t (*word)(void *, uint32_t), void *ctx, uint32_t sizeWords)
    {
        uint32_t sum = 0;
        const uint32_t end = sizeWords < 0x400000u ? sizeWords : 0x400000u;
        for (uint32_t a = 0x4000; a < end; ++a)
            sum += word(ctx, a);
        return uint16_t(sum & 0xffff);
    }
}
