// stat_notify.hpp - the little panel that drops down from the top right when money or a personality stat changes.
//
// UpdateStatChangeNotification (0x04eea6) / BeginStatChangeNotification (0x04edbc) / DrawStatChangeNotification (0x04ee25),
// fed by AddToLifetimeEarnings (every AdjustPlayerMoney) and AddToStatCounter (stat items, mystery bags, personality foods).
// Five pending changes: slot 0 money, slots 1..4 halos, pitchforks, stars, lightbulbs. One panel at a time, each slot in
// turn (the scan takes one slot per step, so a change waits at most 5 steps):
//   state 1/2  the panel slides down 5 px a step to 0x26 below its resting top;
//   state 3    after 10 steps (money 5) the number starts counting from the old value to the new one, 1 per 10 steps for a
//              stat and 5 per 5 steps for money, with the up or down sound each step; once nothing is left it waits one
//              more interval;
//   state 4    it slides back up 5 px a step.
// The panel is the tile 0x993 at (0xba, offset - 0x2c) with the stat's icon at (0xd2 + shift, offset) and the number, right
// aligned to 0x11d - shift, at y = offset - 0x14; the shift is 0 from 1000, 4 from 100, else 8.
#pragma once
#include <algorithm>
#include <array>
#include <functional>

namespace gp
{

    struct StatNoticeSlot
    {
        int key, upSound, downSound, tile, palette;
    };
    // The table at 0xf9f9 (stride 5): key = the id AddToStatCounter is called with.
    constexpr StatNoticeSlot kStatNoticeSlots[5] = {
        {0x0, 0x14, 0x14, 0xacb, 0x28c}, // money
        {0x2, 0x15, 0x15, 0xac2, 0x28c}, // halos
        {0x6, 0x15, 0x15, 0xace, 0x28c}, // pitchforks
        {0xa, 0x15, 0x15, 0xad6, 0x28c}, // stars
        {0xe, 0x15, 0x15, 0xac6, 0x28c}, // lightbulbs
    };
    constexpr int kStatNoticeBg = 0x993, kStatNoticeBgX = 0xba, kStatNoticeBgDY = -0x2c;
    constexpr int kStatNoticeIconX = 0xd2, kStatNoticeTextRight = 0x11d, kStatNoticeTextDY = -0x14;
    constexpr int kStatNoticeDrop = 0x26, kStatNoticeSlide = 5;
    // Tuned by eye against the real game: where the icon sits relative to "its lower right corner at the ROM's coordinates".
    // (+18, +10 puts a 32x32 halo at its anchor point, give or take a pixel.)
    constexpr int kStatNoticeIconNudgeX = 18, kStatNoticeIconNudgeY = 10;

    constexpr int statNoticeShift(long shown) { return shown >= 1000 ? 0 : shown >= 100 ? 4 : 8; }

    struct StatNotice
    {
        int state = 0, offset = 0, wait = 0, idx = 0;
        int prevOffset = 0; // the offset before the latest tick, for the smooth renderer
        std::array<int, 5> pending{};
        long shown = 0;

        bool visible() const { return state != 0; }
        // The offset between the last two ticks, `alpha` of the way (0..1); the plain offset when not smoothing.
        float offsetAt(bool smooth, float alpha) const { return smooth ? float(prevOffset) + float(offset - prevOffset) * alpha : float(offset); }
        void reset()
        {
            state = 0;
            pending.fill(0);
        }

        // AddToStatCounter(key, delta): ignores keys that are not in the table.
        void add(int key, int delta)
        {
            for (int i = 0; i < 5; ++i)
                if (kStatNoticeSlots[i].key == key)
                {
                    pending[size_t(i)] += delta;
                    return;
                }
        }

        // One game step. current(i) = the player's value now for slot i. Returns the sound to play, or -1.
        int tick(const std::function<long(int)> &current)
        {
            prevOffset = offset;
            switch (state)
            {
            case 0:
                if (++idx > 4)
                    idx = 0;
                if (pending[size_t(idx)] != 0)
                {
                    state = 1; // BeginStatChangeNotification: start from the value before the change
                    shown = current(idx) - pending[size_t(idx)];
                }
                break;
            case 1:
                offset = 0;
                state = 2;
                break;
            case 2:
                offset += kStatNoticeSlide;
                if (offset > kStatNoticeDrop - 1)
                {
                    offset = kStatNoticeDrop;
                    state = 3;
                    wait = 10;
                }
                break;
            case 3:
                if (--wait < 1)
                {
                    int &p = pending[size_t(idx)];
                    if (p == 0)
                    {
                        state = 4;
                        break;
                    }
                    const int step = idx == 0 ? 5 : 1;
                    wait = idx == 0 ? 5 : 10;
                    int d = std::min(p < 0 ? -p : p, step);
                    int sound = kStatNoticeSlots[idx].upSound;
                    if (p < 1)
                    {
                        sound = kStatNoticeSlots[idx].downSound;
                        d = -d;
                    }
                    shown += d;
                    p -= d;
                    return sound;
                }
                break;
            default: // 4: back up
                if (offset - kStatNoticeSlide > 0)
                {
                    offset -= kStatNoticeSlide;
                    return -1;
                }
                offset = 0;
                state = 0;
                break;
            }
            return -1;
        }
    };

} // namespace gp
