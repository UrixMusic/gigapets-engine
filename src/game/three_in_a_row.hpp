// game/three_in_a_row.hpp - port of RunThreeInARowGame (reel logic only; drawing, the wells and the drop live in walk.cpp).
// One tick() is one pass of the original's loop.
#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <utility>
#include <vector>

namespace gp
{
    class ThreeInARow
    {
    public:
        enum class Event { None, Exit, Finished };
        using RandFn = std::function<int(int, int)>; // like the ROM's RandRange: lo .. hi-1 (exclusive top!)

        static constexpr int kReels = 3;
        static constexpr int kIconX = 0x46, kSpacing = 0x46, kY = 0x82; // reel i icon at kIconX + i * kSpacing
        static constexpr int kResultDelay = 60;                         // 2.0 s
        static constexpr int kMoneyItem = 0x10a;                        // both launchers pass the flag that selects it (0x109 otherwise)

        // ---- item pools (word addresses; dumped from MAME) ----
        // BuildUniqueRandomSet: pool A at 0x1ff9 (19 entries), pool B at 0x1fcb (46). Repeats are deliberate weighting.
        static constexpr int kPoolA[19] = {
            0x03, 0x07, 0x0B, 0x0F, 0x03, 0x07, 0x0B, 0x0F,
            0x60, 0x65, 0x66,
            0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C};
        static constexpr int kPoolB[46] = {
            0x02, 0x06, 0x0A, 0x0E, 0x02, 0x06, 0x0A, 0x0E, 0x02, 0x06, 0x0A, 0x0E,
            0x17, 0x19, 0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x22, 0x24, 0x25, 0x26,
            0x27, 0x28, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33,
            0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81};

        // BuildRandomPrizePool (the 100 Bucks well): tables at 0x200c (2), 0x200e (4), 0x2012 (8), 0x201a (5).
        static constexpr bool kPrizePoolsKnown = true;
        static constexpr int kPrizeA[2] = {0x72, 0x71};
        static constexpr int kPrizeB[4] = {0x9F, 0xA0, 0xA1, 0xA2};
        static constexpr int kPrizeC[8] = {0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D};
        static constexpr int kPrizeD[5] = {0x04, 0x08, 0x0C, 0x10, 0x10B};
        static constexpr int kPrizeSpecial = 0xb1; // goes first unless held or already offered (HasItemBeenOffered)

        // BuildUniqueRandomSet(out, n, 1): the first item from pool A, the rest from pool B, no repeats,
        // then (n+1)/2 random swaps (not a full shuffle).
        static std::vector<int> pickItems(int n, const RandFn &rnd)
        {
            std::vector<int> v;
            while (int(v.size()) < n)
            {
                const int c = v.empty() ? kPoolA[rnd(0, 19)] : kPoolB[rnd(0, 46)];
                if (std::find(v.begin(), v.end(), c) == v.end())
                    v.push_back(c);
            }
            swapShuffle(v, rnd);
            return v;
        }

        // BuildRandomPrizePool(out, n): [0xb1 if not held/offered], one each from the 2/4/8-entry tables, then
        // picks from the 5-entry table. Quirk kept: the repeat check covers only the table-D picks, not the first three.
        static std::vector<int> pickPrizeItems(int n, bool includeSpecial, const RandFn &rnd)
        {
            std::vector<int> v;
            if (includeSpecial)
                v.push_back(kPrizeSpecial);
            v.push_back(kPrizeA[rnd(0, 2)]);
            v.push_back(kPrizeB[rnd(0, 4)]);
            v.push_back(kPrizeC[rnd(0, 8)]);
            const size_t fixed = v.size();
            while (int(v.size()) < n)
            {
                const int c = kPrizeD[rnd(0, 5)];
                if (std::find(v.begin() + fixed, v.end(), c) == v.end())
                    v.push_back(c);
            }
            swapShuffle(v, rnd);
            return v;
        }

        // items: the unique set. periods: {14,12,10}, or {16,14,12} when Lightbulbs > 199.
        void start(std::vector<int> items, std::array<int, 3> periods, int moneyItem, const RandFn &rnd)
        {
            a_ = std::move(items);
            const int n = int(a_.size());
            b_ = a_; // ShuffleAssignSlots: a random permutation of the same items (exact algorithm not checked)
            for (int i = n - 1; i > 0; --i)
                std::swap(b_[i], b_[rnd(0, i + 1)]);
            period_ = periods;
            money_ = moneyItem;
            pos_ = {0, n - 1, 0};
            timer_ = {0, 0, 0};
            stopped_ = matches_ = now_ = stopTick_ = 0;
            matched_ = -1;
            justStopped_ = improved_ = false;
            open_ = n > 0;
        }

        bool open() const { return open_; }
        int stopped() const { return stopped_; }    // reels stopped; also the index of the reel waiting to stop
        bool justStopped() const { return justStopped_; }
        bool improved() const { return improved_; } // sound 0x15 if true, else 0x59
        int item(int reel) const { return (reel == 2 ? b_ : a_)[pos_[reel]]; }

        std::vector<int> rewards() const // empty = no match; a triple also drops the money item
        {
            std::vector<int> r;
            if (matches_ > 0)
                r.push_back(matched_);
            if (matches_ == 3)
                r.push_back(money_);
            return r;
        }

        Event tick(bool select, bool cancel)
        {
            justStopped_ = false;
            if (!open_)
                return Event::None;
            ++now_;
            if (stopped_ >= kReels && now_ - stopTick_ >= kResultDelay)
            {
                open_ = false;
                return Event::Finished;
            }
            if (stopped_ < kReels)
            {
                if (cancel)
                {
                    open_ = false;
                    return Event::Exit;
                }
                if (select)
                {
                    ++stopped_;
                    if (stopped_ == kReels)
                        stopTick_ = now_;
                    const int m = countPairs();
                    improved_ = m > matches_;
                    matches_ = m;
                    justStopped_ = true;
                }
            }
            const int n = int(a_.size());
            for (int i = stopped_; i < kReels; ++i) // the reel that just stopped doesn't move this tick
                if (++timer_[i] >= period_[i])
                {
                    timer_[i] = 0;
                    pos_[i] = i == 1 ? (pos_[i] + n - 1) % n : (pos_[i] + 1) % n; // reel 1 runs backwards (confirmed in game)
                }
            return Event::None;
        }

    private:
        static void swapShuffle(std::vector<int> &v, const RandFn &rnd)
        {
            const int n = int(v.size());
            for (int i = 0; i < (n + 1) / 2; ++i)
                std::swap(v[rnd(0, n)], v[rnd(0, n)]);
        }

        int countPairs()
        {
            int c = 0;
            for (int i = 0; i < stopped_; ++i)
                for (int j = i + 1; j < stopped_; ++j)
                    if (item(i) == item(j))
                    {
                        ++c;
                        matched_ = item(i);
                    }
            return c;
        }

        std::vector<int> a_, b_;
        std::array<int, 3> pos_{}, timer_{}, period_{14, 12, 10};
        int stopped_ = 0, matches_ = 0, matched_ = -1, money_ = kMoneyItem, now_ = 0, stopTick_ = 0;
        bool open_ = false, justStopped_ = false, improved_ = false;
    };
} // namespace gp