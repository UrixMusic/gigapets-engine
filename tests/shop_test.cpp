// shop_test.cpp [rom] - shop stock: the ROM's rules for rolling, inserting, clamping and buying (no dumped table), the ROM's weight tables as records,
// and the `shop` record.
#include "../src/loaders/import.hpp"
#include "../src/mods/mod_loader.hpp"
#include <iostream>
#include <set>

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

    { // ClaimLRUCacheSlot: a full row gives up its oldest entry; RemoveItem
        ShopStock st;
        for (int i = 1; i <= 9; ++i)
            st.insert("a", "I" + std::to_string(i));
        st.insert("a", "I1"); // I1 is the oldest by order number, whatever its stock
        CHECK(st.claim("a", "I5") && st.row("a")[4].stock == 2);   // already there: one more
        CHECK(st.claim("a", "NEW"));
        auto &r = st.row("a");
        CHECK(r[0].item == "NEW" && r[0].stock == 1 && r[0].idx == 10 && r[1].item == "I2"); // the oldest (I1, order 1) went; the new one is one past the highest
        CHECK(st.claim("a", "NEW2") && r[1].item == "NEW2" && r[1].idx == 11);
        CHECK(!st.claim("a", ""));
        CHECK(st.removeItem("a", "NEW") && r[0].item.empty() && !st.removeItem("a", "NEW") && !st.removeItem("zz", "NEW"));
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

    { // the ROM's stock injections, as records
        const std::vector<StockRuleDef> &rules = base.stockRules();
        CHECK(rules.size() == 18);
        bool shopsKnown = true;
        for (const StockRuleDef &r : rules)
            shopsKnown = shopsKnown && base.shop(r.shop) != nullptr;
        CHECK(shopsKnown);
        auto area = [&](const Id &shop) -> Id { const ShopDef *d = base.shop(shop); return d ? d->area : Id(); };
        std::set<Id> held, offered;
        auto go = [&](ShopStock &st, const StockEvent &ev) {
            applyStockRules(rules, st, ev, area, [&](const Id &i) { return held.count(i) > 0; }, [&](const Id &i) { return offered.count(i) > 0; });
        };
        auto has = [&](ShopStock &st, int shopArea, int item) { return st.find("base:area/" + std::to_string(shopArea), "base:item/" + std::to_string(item)) >= 0; };
        { // on entering an area
            ShopStock st;
            StockEvent ev;
            ev.area = "base:area/1";
            go(st, ev);
            CHECK(has(st, 12, 193));
            ShopStock st2;
            held.insert("base:item/193");
            go(st2, ev);
            CHECK(!has(st2, 12, 193)); // the pet has it already
            held.clear();
            ShopStock st3;
            ev.area = "base:area/7";
            go(st3, ev);
            CHECK(has(st3, 23, 176));
            offered.insert("base:item/176");
            ShopStock st4;
            go(st4, ev);
            CHECK(!has(st4, 23, 176)); // handed in to the King already
            offered.clear();
            ShopStock st5;
            ev.area = "base:area/3";
            go(st5, ev);
            CHECK(st5.rowOf("base:area/12") == nullptr);
        }
        { // a quest of mode 4 / 6: every objective item to three shops, item 90 to two others
            ShopStock st;
            StockEvent ev;
            ev.kind = StockEvent::TurnIn;
            ev.quest = 7;
            ev.mode = 4;
            ev.objectives = {"base:item/41", "base:item/90", "base:item/42"};
            go(st, ev);
            CHECK(has(st, 10, 41) && has(st, 17, 41) && has(st, 18, 41) && has(st, 10, 42) && has(st, 18, 42));
            CHECK(!has(st, 10, 90) && !has(st, 17, 90) && !has(st, 18, 90) && has(st, 12, 90) && has(st, 19, 90));
            CHECK(!has(st, 12, 41) && !has(st, 19, 41));
            ShopStock st2;
            ev.mode = 3;
            go(st2, ev);
            CHECK(st2.rowOf("base:area/10") == nullptr); // other modes stock nothing here
            ShopStock st3;
            ev.mode = 2;
            ev.spawns = {"base:item/77", "base:item/78"};
            go(st3, ev);
            CHECK(has(st3, 19, 77) && !has(st3, 19, 78) && !has(st3, 10, 41));
        }
        { // the story quests by number
            ShopStock st;
            StockEvent ev;
            ev.kind = StockEvent::TurnIn;
            ev.mode = 5;
            ev.quest = 89;
            go(st, ev);
            CHECK(has(st, 12, 35) && has(st, 17, 31) && has(st, 12, 25));
            ShopStock st2;
            ev.quest = 20;
            ev.objectives = {"base:item/60", "base:item/61"};
            go(st2, ev);
            CHECK(has(st2, 19, 60) && !has(st2, 19, 61));
            ShopStock st3;
            ev.quest = 55;
            go(st3, ev);
            CHECK(has(st3, 26, 247));
            ShopStock st4;
            ev.mode = 3; // the numbers only count for the story quests
            go(st4, ev);
            CHECK(st4.rowOf("base:area/26") == nullptr);
        }
        { // a sick pet's cure goes to the pharmacy
            ShopStock st;
            StockEvent ev;
            ev.kind = StockEvent::Illness;
            ev.cure = "base:item/80";
            go(st, ev);
            CHECK(has(st, 19, 80));
        }
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
    { // the shop_stock record
        ModReport rep;
        ContentDB db = load(R"([
 {"type":"shop_stock","mode":"add","id":"sh:stock/gem","shop":"base:shop/10","on":"area","area":"base:area/0","item":"base:item/100","unless_held":true},
 {"type":"shop_stock","mode":"add","id":"sh:stock/q","shop":"base:shop/11","on":"quest","quest":30,"from":"first_objective"},
 {"type":"shop_stock","mode":"add","id":"sh:stock/t","shop":"base:shop/12","on":"type","modes":[1,2],"from":"each_objective","except_item":"base:item/90"},
 {"type":"shop_stock","mode":"patch","id":"base:stock/193","item":"base:item/194","unless_held":false},
 {"type":"shop_stock","mode":"remove","id":"base:stock/cure"}
])", rep);
        for (const auto &e : rep.errors)
            std::cerr << e << "\n";
        CHECK(rep.errors.empty());
        auto find = [&](const Id &id) -> const StockRuleDef * { for (const StockRuleDef &r : db.stockRules()) if (r.id == id) return &r; return nullptr; };
        const StockRuleDef *g = find("sh:stock/gem"), *q = find("sh:stock/q"), *t = find("sh:stock/t"), *p = find("base:stock/193");
        CHECK(g && g->on == "area" && g->area == "base:area/0" && g->item == "base:item/100" && g->unlessHeld && !g->unlessOffered);
        CHECK(q && q->quest == 30 && q->from == "first_objective");
        CHECK(t && t->modes == std::vector<int>({1, 2}) && t->exceptItem == "base:item/90");
        CHECK(p && p->item == "base:item/194" && !p->unlessHeld && p->shop == "base:shop/12");
        CHECK(find("base:stock/cure") == nullptr && db.stockRules().size() == 18 + 3 - 1);
    }
    {
        auto badS = [&](const std::string &rec, const std::string &want) {
            ModReport rep;
            load("[" + rec + "]", rep);
            const bool ok = mentions(rep.errors, want);
            if (!ok)
                std::cerr << "  wanted: " << want << " for " << rec << "\n";
            return ok;
        };
        const std::string h = R"({"type":"shop_stock","mode":"add","id":"sh:stock/x","shop":"base:shop/10",)";
        CHECK(badS(R"({"type":"shop_stock","mode":"add","id":"sh:stock/x","on":"area"})", "'shop' and 'on' are required"));
        CHECK(badS(h + R"("on":"area","area":"base:area/0"})", "unknown item"));
        CHECK(badS(R"({"type":"shop_stock","mode":"add","id":"sh:stock/x","shop":"nope:shop/1","on":"illness","from":"cure"})", "unknown shop"));
        CHECK(badS(h + R"("on":"sometimes","item":"base:item/1"})", "'on' must be"));
        CHECK(badS(h + R"("on":"area","area":"base:area/0","from":"nothing"})", "'from' must be"));
        CHECK(badS(h + R"("on":"area","area":"base:area/999","item":"base:item/1"})", "existing 'area'"));
        CHECK(badS(h + R"("on":"quest","from":"first_objective"})", "needs 'quest'"));
        CHECK(badS(h + R"("on":"type","from":"each_objective"})", "needs 'modes'"));
        CHECK(badS(h + R"("on":"area","area":"base:area/0","from":"first_objective"})", "fixed 'item'"));
        CHECK(badS(h + R"("on":"quest","quest":3,"from":"cure"})", "only goes with"));
        CHECK(badS(h + R"("on":"quest","quest":3,"item":"base:item/1","only_item":"base:item/2"})", "each_objective"));
        CHECK(badS(h + R"("on":"quest","quest":3,"item":"base:item/1","unless_held":true})", "unless_held"));
        CHECK(badS(h + R"("on":"area","area":"base:area/0","item":"base:item/1","unless_held":1})", "true or false"));
        CHECK(badS(h + R"("on":"type","modes":["a"],"from":"each_objective"})", "list of quest type modes"));
        CHECK(badS(R"({"type":"shop_stock","mode":"patch","id":"nope:stock/1","item":"base:item/1"})", "no such shop_stock rule"));
        CHECK(badS(R"({"type":"shop_stock","mode":"remove","id":"base:stock/cure","item":"base:item/1"})", "unknown field"));
        CHECK(badS(R"({"type":"shop_stock","mode":"patch","id":"base:stock/193","item":"base:item/9999"})", "unknown item"));
    }
    fs::remove_all(root);

    std::cerr << (g_fail ? std::to_string(g_fail) + " failed\n" : "shop_test: all passed\n");
    return g_fail ? 1 : 0;
}
