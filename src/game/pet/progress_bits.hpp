#pragma once
#include <array>
#include <cstdint>

namespace gp
{
    // The bit array at word 0x540. Bit n = object row n of the CURRENT area, 1 = present (default all ones).
    struct ProgressBits
    {
        std::array<uint16_t, 64> w;
        ProgressBits() { w.fill(0xffff); }
        void set(int bit, bool v)
        {
            uint16_t &x = w[size_t(bit >> 4) & 63];
            const uint16_t m = uint16_t(1u << (bit & 15));
            x = v ? uint16_t(x | m) : uint16_t(x & ~m);
        }
        bool present(int bit) const { return bit < 0 || ((w[size_t(bit >> 4) & 63] >> (bit & 15)) & 1); }
    };

    // Migrate*FlagsToProgressBits. flags bit i = item (base + i), set = handed over.
    inline void migrateOfferBits(int area, uint16_t flags, ProgressBits &b)
    {
        static constexpr int kGardenia[12] = {0x18, 0x16, 0xa, 0xe, 0x17, 0xf, 0x12, 0xd, 0x19, 0x1a, 0x13, 9};
        static constexpr int kKing[12] = {8, 0xb, 0xa, 0xc, 9, 5, 3, 4, 1, 2, 7, 6};
        const int *t = area == 0x22 ? kGardenia : area == 0x1c ? kKing : nullptr;
        if (!t) return;
        for (int i = 0; i < 12; ++i) b.set(t[i], (flags >> i) & 1);
    }

    // Fresh array for a room: everything present, then the offer flags. Quest-count gates are no longer
    // applied here: they are ObjectDef::questMin/questMax (see importQuestGates). Call on room entry,
    // after a quest completes, and after a hand-in.
    inline ProgressBits rebuildBits(int area, uint16_t kingFlags, uint16_t gardenFlags)
    {
        ProgressBits b;
        if (area == 0x1c) migrateOfferBits(area, kingFlags, b);
        if (area == 0x22) migrateOfferBits(area, gardenFlags, b);
        return b;
    }
}