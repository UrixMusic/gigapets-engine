// tools/find_stock.cpp
#include "../src/loaders/rom.hpp"
#include <cstdio>
int main(int argc, char **argv)
{
    gp::Rom rom(argv[1]);
    const uint16_t pat[] = {0x022D, 0x043D, 0x0444, 0x0433, 0x0440, 0x0245};
    for (uint32_t a = 0; a + 12 <= rom.sizeWords(); ++a)
        for (int stride : {1, 2})
        { // packed, or interleaved with an index word
            int i = 0;
            while (i < 6 && rom.u16(a + i * stride) == pat[i])
                ++i;
            if (i >= 4)
                std::printf("match at 0x%x stride %d (%d/6)\n", a, stride, i);
        }
}