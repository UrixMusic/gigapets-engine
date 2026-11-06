// item_withdraw_test.cpp - item records: "patch" changes the fields given (as replace does), "remove" withdraws an item (no finds, no shop stock, no stock rules) but keeps it.
#define SDL_MAIN_HANDLED
#include "../src/mods/mod_loader.hpp"
#include <iostream>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                   \
    do                                                             \
    {                                                              \
        if (!(c))                                                  \
        {                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                              \
        }                                                          \
    } while (0)

static void writeFile(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "gp_item_withdraw_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    ContentDB db;
    for (int i = 0; i < 2; ++i)
    {
        ItemDef d;
        d.id = "base:item/" + std::to_string(i);
        d.price = 10 + i;
        d.maxQty = 9;
        db.put(std::move(d));
    }
    {
        FindWeightDef f;
        f.id = "base:find/0";
        f.item = "base:item/0";
        f.weight = 5;
        db.put(std::move(f));
        FindWeightDef g;
        g.id = "base:find/1";
        g.item = "base:item/1";
        g.weight = 7;
        db.put(std::move(g));
        ShopDef s;
        s.id = "base:shop/1";
        s.items = {{"base:item/0", 1, 5}, {"base:item/1", 1, 5}};
        db.put(std::move(s));
        StockRuleDef r;
        r.id = "base:stock/0";
        r.shop = "base:shop/1";
        r.from = "item";
        r.item = "base:item/0";
        db.put(std::move(r));
        StockRuleDef q;
        q.id = "base:stock/1";
        q.shop = "base:shop/1";
        q.from = "item";
        q.item = "base:item/1";
        db.put(std::move(q));
    }
    writeFile(root / "m/manifest.json", R"({"id":"m","version":"1.0","game_api":"1","name":"M","description":"d"})");
    writeFile(root / "m/data/items.json", R"([
      {"type":"item","mode":"patch","id":"base:item/1","price":99},
      {"type":"item","mode":"remove","id":"base:item/0"},
      {"type":"item","mode":"remove","id":"base:item/9"},
      {"type":"item","mode":"remove","id":"base:item/1","price":5}
    ])");
    ModReport rep = importMods(db, root.string());
    CHECK(db.item("base:item/1")->price == 99); // patch: the price changed, the rest stayed
    CHECK(db.item("base:item/1")->maxQty == 9);
    CHECK(db.item("base:item/0") != nullptr);   // withdrawn, not gone
    CHECK(db.findWeight("base:find/0") == nullptr && db.findWeight("base:find/1") != nullptr);
    CHECK(db.shop("base:shop/1")->items.size() == 1 && db.shop("base:shop/1")->items[0].item == "base:item/1");
    CHECK(db.stockRules().size() == 1 && db.stockRules()[0].item == "base:item/1");
    int errs = 0;
    for (const std::string &e : rep.errors)
    {
        errs += e.find("no such item") != std::string::npos || e.find("unknown field 'price' in an item remove") != std::string::npos;
        std::cerr << "(error) " << e << "\n";
    }
    CHECK(errs == 2); // removing something that is not there, and a remove with fields
    fs::remove_all(root, ec);
    if (g_fail)
        return 1;
    std::cout << "item_withdraw_test: ok\n";
    return 0;
}
