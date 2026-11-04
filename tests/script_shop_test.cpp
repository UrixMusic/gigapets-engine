// script_shop_test.cpp - the shop.* script API (stock, add, remove, restock): what the host checks and returns, with an in-memory fake behind it.
#include "../src/mods/script_host.hpp"
#include <cstdio>
#include <map>
#include <vector>

static int fails = 0;
#define CHECK(c)                                                       \
    do                                                                 \
    {                                                                  \
        if (!(c))                                                      \
        {                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);           \
            ++fails;                                                   \
        }                                                              \
    } while (0)

struct Fake
{
    std::vector<gp::ScriptApi::ShopLine> lines = {{"base:item/41", 2}, {"base:item/42", 5}};
    int restocks = 0;
    std::string lastRestock = "?";
    void wire(gp::ScriptApi &a)
    {
        a.shopStock = [this](const std::string &shop, std::vector<gp::ScriptApi::ShopLine> &out)
        {
            if (shop != "base:shop/10" && shop != "base:area/10")
                return false;
            out = lines;
            return true;
        };
        a.shopAdd = [this](const std::string &shop, const std::string &item, int n)
        {
            if (shop != "base:shop/10" || item.rfind("base:item/", 0) != 0)
                return 0;
            lines.push_back({item, n});
            return n;
        };
        a.shopRemove = [this](const std::string &shop, const std::string &item)
        {
            if (shop != "base:shop/10")
                return false;
            for (size_t i = 0; i < lines.size(); ++i)
                if (lines[i].item == item)
                {
                    lines.erase(lines.begin() + std::ptrdiff_t(i));
                    return true;
                }
            return false;
        };
        a.shopRestock = [this](const std::string &shop)
        {
            ++restocks;
            lastRestock = shop;
            return shop.empty() || shop == "base:shop/10";
        };
    }
};

int main()
{
    gp::ScriptHost h;
    Fake f;
    f.wire(h.api());
    CHECK(h.runString("m", R"(
        local s = shop.stock('base:shop/10')
        assert(#s == 2 and s[1].item == 'base:item/41' and s[1].count == 2 and s[2].count == 5)
        assert(shop.stock('base:area/10') ~= nil)       -- a shop by its area's id too
        assert(shop.stock('base:shop/99') == nil)       -- no such shop
        assert(shop.add('base:shop/10', 'base:item/43') == 1)
        assert(shop.add('base:shop/10', 'base:item/44', 3) == 3)
        assert(shop.add('base:shop/99', 'base:item/44') == 0)
        assert(shop.add('base:shop/10', 'nope') == 0)
        assert(#shop.stock('base:shop/10') == 4)
        assert(shop.remove('base:shop/10', 'base:item/41') == true)
        assert(shop.remove('base:shop/10', 'base:item/41') == false)
        assert(shop.remove('base:shop/99', 'base:item/41') == false)
        assert(shop.restock('base:shop/10') == true and shop.restock('base:shop/9') == false and shop.restock() == true)
    )"));
    for (const auto &e : h.errors()) std::printf("script error: %s\n", e.c_str());
    CHECK(f.restocks == 3 && f.lastRestock.empty());
    CHECK(!h.runString("m", "shop.add('base:shop/10', 'base:item/43', 0)"));   // the count is 1 to 99
    CHECK(!h.runString("m", "shop.add('base:shop/10', 'base:item/43', 100)"));
    CHECK(h.runString("m", "assert(shop.stock('bad id,') == nil and shop.add('bad=x', 'base:item/1') == 0 and shop.remove('base:shop/10', 'a|b') == false)")); // forbidden characters are refused before the host is asked
    CHECK(!h.runString("m", "shop.stock()")); // an argument is needed
    { // with nothing wired the calls are harmless
        gp::ScriptHost h2;
        CHECK(h2.runString("m", "assert(shop.stock('base:shop/10') == nil and shop.add('a', 'b') == 0 and shop.remove('a', 'b') == false and shop.restock() == false)"));
    }
    std::printf(fails ? "script_shop_test: %d failure(s)\n" : "script_shop_test: all passed\n", fails);
    return fails ? 1 : 0;
}
