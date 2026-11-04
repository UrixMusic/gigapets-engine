// shop_test.cpp [rom] - shop stock: the ROM's rules for rolling, inserting, clamping and buying (no dumped table), the ROM's weight tables as records,
// and the `shop` record.
#include "../src/loaders/import.hpp"
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

static bool mentions(const std::vector<std::string> &v, const std::string &s)
{
    for (const auto &e : v)
        if (e.find(s) != std::string::npos)
            return true;
    return false;
}
static ShopDef shopOf(const Id &area, std::vector<ShopItem> items, int rolls = 10)
{
    ShopDef s;
    s.id = "t:shop/" + area;
    s.area = area;
    s.rolls = rolls;
    s.items = std::move(items);
    return s;
}

int main(int argc, char **argv)
{
    { // PickWeightedRandomItem: summed weights, walked from the last entry back
        const ShopDef s = shopOf("a", {{"A", 1, 5}, {"B", 2, 5}, {"C", 3, 5}});
        auto at = [&](int r) { return pickWeightedItem(s, [&](int n) { CHECK(n == 6); return r; }); };
        CHECK(at(0) == "C" && at(2) == "C" && at(3) == "B" && at(4) == "B" && at(5) == "A");
        CHECK(pickWeightedItem(shopOf("a", {}), [](int) { return 0; }).empty());
        CHECK(pickWeightedItem(shopOf("a", {{"A", 0, 5}}), [](int) { return 0; }).empty());
        CHECK(pickWeightedItem(shopOf("a", {{"A", 0, 5}, {"B", 4, 5}}), [](int) { return 3; }) == "B"); // a weight of 0 is never picked
    }
    { // InsertOrIncrementPoolSlot
        ShopStock st;
        CHECK(st.insert("a", "A") && st.insert("a", "B") && st.insert("a", "A"));
        auto &r = st.row("a");
        CHECK(r[0].item == "A" && r[0].stock == 2 && r[0].idx == 1 && r[1].item == "B" && r[1].stock == 1 && r[1].idx == 2 && r[2].item.empty());
        CHECK(st.buyOne("a", 0) == false && r[0].stock == 1);
        CHECK(st.buyOne("a", 0) == true && r[0].item.empty()); // sold out: the slot is free again
        CHECK(st.insert("a", "C") && r[0].item == "C" && r[0].idx == 3 && r[0].stock == 1); // the first empty slot, one past the highest order number
        CHECK(st.insert("a", "B") && r[1].stock == 2); // a match after the gap still counts
        for (int i = 0; i < 7; ++i)
            CHECK(st.insert("a", "X" + std::to_string(i)));
        CHECK(!st.insert("a", "Y"));  // nine different items: no room
        CHECK(st.insert("a", "X3"));  // but more of one that is there is fine
        CHECK(!st.insert("a", ""));
        CHECK(st.find("a", "X3") >= 0 && st.find("a", "nope") < 0 && st.find("zz", "A") < 0);
    }
    { // the start and the restock
        const std::vector<ShopDef> shops = {shopOf("a", {{"A", 1, 5}, {"B", 1, 5}, {"C", 1, 5}}), shopOf("b", {{"D", 1, 5}}, 4), shopOf("c", {{"E", 5, 5}}, 0)};
        ShopStock st;
        int calls = 0;
        auto rnd = [&](int n) { return (calls++ * 7) % n; };
        st.init(shops, rnd);
        auto total = [&](const Id &a) { int n = 0; for (const StockEntry &e : *st.rowOf(a)) n += e.stock; return n; };
        CHECK(total("a") == 10 && total("b") == 4 && total("c") == 0 && st.rowOf("c") != nullptr);
        st.restock(shops, rnd);
        CHECK(total("a") == 11 && total("b") == 5 && total("c") == 1);
        st.init(shops, rnd);
        CHECK(total("a") == 10); // starting again forgets the old stock
    }
    { // ClampInventorySlotQuantities
        const ShopDef s = shopOf("a", {{"A", 1, 3}, {"B", 1, 15}});
        ShopStock st;
        for (int i = 0; i < 9; ++i)
            st.insert("a", "A");
        for (int i = 0; i < 9; ++i)
            st.insert("a", "B");
        st.insert("a", "U"); // not in the table: no cap of its own
        st.insert("a", "U");
        std::map<Id, int> held = {{"B", 6}};
        st.clamp(s, [&](const Id &i) { return held[i]; }, [](const Id &) { return 9; });
        auto &r = st.row("a");
        CHECK(r[0].item == "A" && r[0].stock == 3);          // the shop's cap
        CHECK(r[1].item == "B" && r[1].stock == 3);          // what the pet can still carry: 9 - 6
        CHECK(r[2].item == "U" && r[2].stock == 2);          // unlisted: only the carry limit
        held["B"] = 9;
        st.clamp(s, [&](const Id &i) { return held[i]; }, [](const Id &) { return 9; });
        CHECK(r[1].item.empty());                            // can carry no more: the entry goes
    }

    // ---- the ROM's tables
    Rom rom(argc > 1 ? argv[1] : "rom.u7");
    ContentDB base;
    importRom(rom, base);
    CHECK(base.shops().size() == 22);
    {
        const ShopDef *s = base.shop("base:shop/10"); // the first shop: area 10, the ROM's row 0
        CHECK(s && s->area == "base:area/10" && s->rolls == 10 && s->items.size() == 10);
        CHECK(s && s->items[0].item == "base:item/41" && s->items[0].weight == 60 && s->items[0].cap == 15);
        CHECK(s && s->items[9].item == "base:item/26" && s->items[9].weight == 5 && s->items[9].cap == 5);
        const ShopDef *l = base.shop("base:shop/11");
        CHECK(l && l->items.size() == 8 && l->items[2].item == "base:item/56" && l->items[2].weight == 2 && l->items[2].cap == 1);
        bool allKnown = true, areas = true;
        for (const ShopDef &d : base.shops())
        {
            areas = areas && base.area(d.area) != nullptr;
            for (const ShopItem &i : d.items)
                allKnown = allKnown && base.item(i.item) != nullptr;
        }
        CHECK(allKnown && areas);
        CHECK(base.shopForArea("base:area/10") == s && base.shopForArea("base:area/0") == nullptr);
        ShopStock st; // rolled from the real tables
        int k = 0;
        st.init(base.shops(), [&](int n) { return (k++ * 13) % n; });
        int stocked = 0;
        for (const ShopDef &d : base.shops())
            for (const StockEntry &e : *st.rowOf(d.area))
                stocked += e.present() ? 1 : 0;
        CHECK(stocked > 22 * 3);
    }

    // ---- the record
    const fs::path root = fs::temp_directory_path() / "gp_shoprec";
    auto writeMod = [&](const std::string &data) {
        fs::remove_all(root);
        fs::create_directories(root / "sh" / "data");
        std::ofstream(root / "sh" / "manifest.json") << R"({"id":"sh","version":"1.0","game_api":"1"})";
        std::ofstream(root / "sh" / "data" / "s.json") << data;
    };
    auto load = [&](const std::string &data, ModReport &rep) {
        writeMod(data);
        ContentDB db;
        importRom(rom, db);
        rep = importMods(db, root);
        return db;
    };
    {
        ModReport rep;
        ContentDB db = load(R"([
 {"type":"shop","mode":"patch","id":"base:shop/10","rolls":14,"add_items":[{"item":"base:item/100","weight":7,"cap":2},{"item":"base:item/41","weight":1,"cap":9}],"remove_items":["base:item/42"]},
 {"type":"shop","mode":"patch","id":"base:shop/11","items":[{"item":"base:item/61"}]},
 {"type":"shop","mode":"remove","id":"base:shop/12"},
 {"type":"shop","mode":"add","id":"sh:shop/store","area":"base:area/0","items":[{"item":"base:item/30","weight":3}],"rolls":2}
])", rep);
        for (const auto &e : rep.errors)
            std::cerr << e << "\n";
        CHECK(rep.errors.empty());
        const ShopDef *a = db.shop("base:shop/10");
        CHECK(a && a->rolls == 14 && a->items.size() == 10 && a->items[0].item == "base:item/41" && a->items[0].weight == 1 && a->items[0].cap == 9);
        bool has100 = false, has42 = false;
        for (const ShopItem &i : a->items)
        {
            has100 = has100 || (i.item == "base:item/100" && i.weight == 7 && i.cap == 2);
            has42 = has42 || i.item == "base:item/42";
        }
        CHECK(has100 && !has42);
        const ShopDef *b = db.shop("base:shop/11");
        CHECK(b && b->items.size() == 1 && b->items[0].weight == 10 && b->items[0].cap == 5); // the defaults
        CHECK(db.shop("base:shop/12") == nullptr && db.shops().size() == 22);
        const ShopDef *c = db.shop("sh:shop/store");
        CHECK(c && c->area == "base:area/0" && c->rolls == 2 && c->items.size() == 1 && c->items[0].weight == 3 && db.shopForArea("base:area/0") == c);
        CHECK(rep.lastWriter["base:shop/10"] == "sh");
    }
    auto bad = [&](const std::string &rec, const std::string &want) {
        ModReport rep;
        load("[" + rec + "]", rep);
        const bool ok = mentions(rep.errors, want);
        if (!ok)
            std::cerr << "  wanted: " << want << " for " << rec << "\n";
        return ok;
    };
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","items":[{"item":"base:item/30"}]})", "'area'"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/999","items":[{"item":"base:item/30"}]})", "unknown area"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/0"})", "'items' is required"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/10","items":[{"item":"base:item/30"}]})", "already has a shop"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"other:shop/x","area":"base:area/0","items":[{"item":"base:item/30"}]})", "own prefix"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/0","items":[{"item":"base:item/9999"}]})", "unknown item"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/0","items":[{"item":"base:item/30"},{"item":"base:item/30"}]})", "listed twice"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/0","items":[{"item":"base:item/30","weight":-1}]})", "weight"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/0","items":[{"item":"base:item/30","cap":0}]})", "cap"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/0","items":[{"item":"base:item/30","price":1}]})", "unknown field"));
    CHECK(bad(R"({"type":"shop","mode":"add","id":"sh:shop/x","area":"base:area/0","items":[{"item":"base:item/30"}],"remove_items":[]})", "only go with a patch"));
    CHECK(bad(R"({"type":"shop","mode":"patch","id":"base:shop/10","area":"base:area/0","rolls":1})", "cannot be changed"));
    CHECK(bad(R"({"type":"shop","mode":"patch","id":"base:shop/10"})", "nothing to change"));
    CHECK(bad(R"({"type":"shop","mode":"patch","id":"base:shop/10","remove_items":["base:item/1"]})", "not in the shop"));
    CHECK(bad(R"({"type":"shop","mode":"patch","id":"base:shop/10","rolls":500})", "rolls"));
    CHECK(bad(R"({"type":"shop","mode":"patch","id":"nope:shop/1","rolls":1})", "no such shop"));
    CHECK(bad(R"({"type":"shop","mode":"remove","id":"base:shop/10","rolls":1})", "unknown field"));
    fs::remove_all(root);

    std::cerr << (g_fail ? std::to_string(g_fail) + " failed\n" : "shop_test: all passed\n");
    return g_fail ? 1 : 0;
}
