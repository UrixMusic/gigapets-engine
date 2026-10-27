// item_pools.hpp - the minigame prize pools and BuildUniqueRandomSet (0x02f805).
//
// Tables are read from the ROM at 0x5a9d7 (and match a MAME RAM dump of 0x1ff9..0x2011):
//   RAM 0x1fcb  46 entries  general pool (ids 2,6,a,e are the personality-boost items, listed 3x each for weight)
//   RAM 0x1ff9  19 entries  "first slots" pool (ids 3,7,b,f boosts listed 2x, plus 0x60.. items)
//   RAM 0x200c  19 entries  second prize pool (the 100 Bucks well / BuildRandomPrizePool), ends with 0x10b
// Ids 2..0x11 and 0x109..0x10b are the "special" ids (stat boosts / money / mystery bag), same as isSpecialItem().
#pragma once
#include <array>
#include <functional>
#include <vector>

namespace gp
{

inline constexpr std::array<int, 46> kPoolGeneral = {
    0x02, 0x06, 0x0a, 0x0e, 0x02, 0x06, 0x0a, 0x0e, 0x02, 0x06, 0x0a, 0x0e,
    0x17, 0x19, 0x1a, 0x1b, 0x1d, 0x1e, 0x1f, 0x20, 0x22, 0x24, 0x25, 0x26,
    0x27, 0x28, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
    0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81};

inline constexpr std::array<int, 19> kPoolFirst = {
    0x03, 0x07, 0x0b, 0x0f, 0x03, 0x07, 0x0b, 0x0f,
    0x60, 0x65, 0x66, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c};

inline constexpr std::array<int, 19> kPoolPrize = { // 100 Bucks well; BuildRandomPrizePool's source
    0x72, 0x71, 0x9f, 0xa0, 0xa1, 0xa2, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d,
    0x04, 0x08, 0x0c, 0x10, 0x10b};

// BuildUniqueRandomSet(out, count, poolAThreshold): the first `firstSlots` entries are drawn from kPoolFirst, the
// rest from kPoolGeneral; any draw already in the set is re-rolled; then (count+1)/2 random swaps shuffle it.
// rr(lo, hi) is the ROM's RandRange: [lo, hi).
// Known callers: Memory Match (6, 2); Swift Pick via PlayRandomMinigame (10, 2); Three in a Row (RandRange(6,9), 1).
inline std::vector<int> buildUniqueRandomSet(int count, int firstSlots, const std::function<int(int, int)> &rr)
{
    std::vector<int> out;
    while (int(out.size()) < count)
    {
        const int i = int(out.size());
        const int item = i < firstSlots ? kPoolFirst[size_t(rr(0, int(kPoolFirst.size())))]
                                        : kPoolGeneral[size_t(rr(0, int(kPoolGeneral.size())))];
        bool dup = false;
        for (int v : out)
            dup = dup || v == item;
        if (!dup)
            out.push_back(item);
    }
    for (int k = 0; k < (count + 1) / 2; ++k)
        std::swap(out[size_t(rr(0, count))], out[size_t(rr(0, count))]);
    return out;
}

} // namespace gp