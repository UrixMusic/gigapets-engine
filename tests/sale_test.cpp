// tests/sale_test.cpp - what Jeremy pays (game/sale.hpp).
#include <cstdio>
#include "../src/game/sale.hpp"
using namespace gp;

int main()
{
    int fails = 0;
    auto check = [&](const char *w, bool ok)
    { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) fails++; };

    check("Jeremy is NPC 0x44", kSellerNpc == 0x44);
    check("price 10 sells for 8 each", saleTotal(10, 1) == 8 && saleTotal(10, 3) == 24);
    check("price 5: 4 each, 20 for five", saleTotal(5, 1) == 4 && saleTotal(5, 5) == 20);
    check("price 25: 20 each", saleTotal(25, 2) == 40);
    check("a price of 1 or 0 still pays 1 each", saleTotal(1, 4) == 4 && saleTotal(0, 7) == 7);
    check("price 2: 1.6 each, so 3 sell for 4 (the total is cut, not each one)", saleTotal(2, 3) == 4 && saleTotal(2, 1) == 1);
    check("price 100: 80 each", saleTotal(100, 10) == 800);

    check("types 14, 15, 16 and 17 are not on the sell list; 13 and 12 are", sellExcludedType(14) && sellExcludedType(15) && sellExcludedType(16) &&
                                                                              sellExcludedType(17) && !sellExcludedType(13) && !sellExcludedType(12) && !sellExcludedType(9));

    std::printf(fails ? "sale_test: %d failure(s)\n" : "sale_test: all passed\n", fails);
    return fails ? 1 : 0;
}
