// game/swift_pick.hpp - port of RunSwiftPickGame (reel logic only; drawing and the catch live in walk.cpp).
// One tick() is one pass of the original's loop (it ends with WaitForNextTick(1)).
#pragma once
#include <algorithm>
#include <array>
#include <vector>

namespace gp
{
    class SwiftPick
    {
    public:
        enum class Event
        {
            None,
            Exit,
            GotAway,
            Caught
        };

        static constexpr int kCells = 6;
        static constexpr int kSpacing = 70;  // cell k rests at x = k * 70 (0x46)
        static constexpr int kFrameX = 0x8c; // selector frame = cell 2
        static constexpr int kCellY = 0x82;
        static constexpr int kDecayEvery = 5;   // ticks between slow-downs after SELECT
        static constexpr int kResultDelay = 60; // 2.0 s at 59.94 ticks/s (INFERRED direction of the float compare)

        int lastMove() const { return lastMove_; } // px the reel scrolled in the latest tick (for smooth rendering)

        // items: pool of item ids, 0 = "Got Away!". speed: 12, or 10 when Lightbulbs > 199.
        void start(std::vector<int> items, int speed)
        {
            justPressed_ = false;
            lastMove_ = 0;
            pool_ = std::move(items);
            speed_ = speed;
            index_ = target_ = 0;
            pressed_ = stopping_ = justPressed_ = false;
            now_ = pressTick_ = stopTick_ = 0;
            for (int k = 0; k < kCells; ++k)
                cur_[k] = k * kSpacing;
            populate();
            open_ = !pool_.empty();
        }

        bool open() const { return open_; }
        bool justPressed() const { return justPressed_; } // SELECT accepted this tick (sound 0x59)
        int cellX(int k) const { return cur_[k]; }
        int cellItem(int k) const { return win_[k]; }
        int result() const { return pool_[index_]; } // the item under the frame

        Event tick(bool select, bool cancel)
        {

            justPressed_ = false;
            lastMove_ = 0;
            if (!open_)
                return Event::None;
            ++now_;
            const int n = int(pool_.size());

            if (stopping_ && now_ - stopTick_ >= kResultDelay)
            {
                open_ = false;
                return result() == 0 ? Event::GotAway : Event::Caught;
            }
            if (cancel && !pressed_)
            {
                open_ = false;
                return Event::Exit;
            }
            if (select && !pressed_)
            {
                const bool closer = (0xb4 - cur_[2]) < (cur_[3] - kFrameX);
                target_ = (index_ + (closer ? 3 : 4)) % n;
                pressed_ = justPressed_ = true;
                pressTick_ = now_;
            }

            if (!stopping_)
            {
                if (pressed_)
                {
                    if ((now_ - pressTick_) % kDecayEvery == 0)
                        speed_ = speed_ * 0x60 / 0x64;
                    speed_ = std::max(speed_, 1);
                }
                for (int &x : cur_)
                    x -= speed_;
                lastMove_ = speed_;
            }
            else if (cur_[2] > kFrameX)
            {
                speed_ = std::min(speed_, cur_[2] - kFrameX);
                for (int &x : cur_)
                    x -= speed_;
                lastMove_ = speed_;
                stopTick_ = now_;
            }

            if (cur_[1] <= 0) // the cell in slot 1 has reached slot 0's rest position: shift everything down
            {
                for (int k = 0; k < kCells - 1; ++k)
                    cur_[k] = cur_[k + 1];
                cur_[kCells - 1] = (kCells - 1) * kSpacing;
                if (!stopping_)
                {
                    index_ = (index_ + 1) % n;
                    if (pressed_ && index_ == target_)
                    {
                        stopping_ = true;
                        stopTick_ = now_;
                    }
                }
                populate();
            }
            return Event::None;
        }

    private:
        int itemAt(int i) const
        {
            const int n = int(pool_.size());
            return pool_[((i % n) + n) % n];
        }
        void populate()
        {
            for (int k = 0; k < kCells; ++k)
                win_[k] = itemAt(index_ - 2 + k);
        }

        std::vector<int> pool_;
        std::array<int, kCells> cur_{}, win_{};
        int lastMove_ = 0;
        int speed_ = 12, index_ = 0, target_ = 0, now_ = 0, pressTick_ = 0, stopTick_ = 0;
        bool open_ = false, pressed_ = false, stopping_ = false, justPressed_ = false;
    };
} // namespace gp