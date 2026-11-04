// What a script can do to the objects of a loaded area: add one, move one (the area's own too), take one out. The db's own area is
// never changed, and a new AreaRuntime (the area entered again) starts from the db.
#include "../src/engine/area_runtime.hpp"
#include <iostream>

static int g_fail = 0;
#define CHECK(c)                                                  \
    do                                                            \
    {                                                             \
        if (!(c))                                                 \
        {                                                         \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                             \
        }                                                         \
    } while (0)

static gp::ObjectDef obj(const char *id, int x, int y, int reach = 0)
{
    gp::ObjectDef o;
    o.id = id;
    o.type = "t";
    o.x = x;
    o.y = y;
    o.boxLeft = x - 8;
    o.boxRight = x + 8;
    o.boxTop = y - 16;
    o.boxBottom = y;
    if (reach)
    {
        o.reachX = o.reachY = reach;
        o.interactId = 0;
    }
    return o;
}

static std::vector<std::string> order(const gp::AreaRuntime &r)
{
    std::vector<std::string> v;
    for (const gp::ObjectDef *o : r.objects())
        v.push_back(o->id);
    return v;
}

int main()
{
    gp::ContentDB db;
    gp::AreaDef a;
    a.id = "base:area/9";
    a.objects = {obj("a", 100, 100), obj("b", 200, 50), obj("c", 300, 150, 30)};
    {
        gp::AreaRuntime r(db, a);
        CHECK((order(r) == std::vector<std::string>{"b", "a", "c"})); // sorted by feet

        // add: sorted in by y, usable when it has a reach
        const gp::ObjectDef *n = r.addObject(obj("s1", 150, 120, 25));
        CHECK(n && n->id == "s1" && (order(r) == std::vector<std::string>{"b", "a", "s1", "c"}));
        CHECK(!r.addObject(obj("s1", 1, 1)) && !r.addObject(obj("a", 1, 1))); // an id that is taken
        r.updateVisible(0, 0, 400, 300);
        CHECK(r.nearestInteractable(150, 120) && r.nearestInteractable(150, 120)->id == "s1");

        // move one of the area's own objects: the copy moves, the db does not
        CHECK(r.moveObject("a", 100, 400));
        CHECK((order(r) == std::vector<std::string>{"b", "s1", "c", "a"}));
        CHECK(a.objects[0].y == 100 && a.objects[0].boxBottom == 100); // the db's record is untouched
        const gp::ObjectDef *moved = nullptr;
        for (const gp::ObjectDef *o : r.objects())
            if (o->id == "a")
                moved = o;
        CHECK(moved && moved->x == 100 && moved->y == 400 && moved->boxBottom == 400 && moved->boxTop == 384); // its box came along
        CHECK(r.moveObject("a", 120, 410) && r.moveObject("s1", 10, 10)); // again, and a spawned one
        CHECK(!r.moveObject("nope", 1, 1));

        // remove: gone from the order, from the interactables, and it can be added again
        CHECK(r.removeObject("c") && !r.has("c") && !r.removeObject("c"));
        r.updateVisible(0, 0, 400, 300);
        CHECK(!r.nearestInteractable(300, 150));
        CHECK(r.addObject(obj("c", 5, 5)) && r.has("c"));
        CHECK(r.moveObject("c", 6, 7)); // the new "c", not the removed one
        for (const gp::ObjectDef *o : r.objects())
            if (o->id == "c")
                CHECK(o->x == 6 && o->y == 7 && !o->interactive());
        CHECK(a.objects.size() == 3 && a.objects[2].x == 300 && a.objects[2].interactive()); // still as the db has it
    }
    { // another picture for an object: its type (and the art box the cull uses) change; place, reach and id stay; the db is untouched
        gp::ContentDB db2;
        gp::ObjectType big;
        big.id = "big";
        big.hasArt = true;
        big.art.anchorX = 20;
        big.art.anchorY = 40;
        big.art.dx = 2;
        big.art.dy = -1;
        big.art.w = 40;
        big.art.h = 48;
        db2.put(big);
        gp::AreaRuntime r(db2, a);
        CHECK(r.setObjectType("c", "big")); // an object of the area (with a reach)
        const gp::ObjectDef *c = nullptr;
        for (const gp::ObjectDef *o : r.objects())
            if (o->id == "c")
                c = o;
        CHECK(c && c->type == "big" && c->x == 300 && c->y == 150 && c->reachX == 30 && c->interactive());
        CHECK(c && c->boxLeft == 300 - 20 + 2 && c->boxTop == 150 - 40 - 1 && c->boxRight == c->boxLeft + 40 && c->boxBottom == c->boxTop + 48);
        CHECK(a.objects[2].type == "t" && a.objects[2].boxLeft == 292); // the db's record is untouched
        CHECK(!r.setObjectType("c", "nobody") && !r.setObjectType("nope", "big")); // an unknown type or object
        const gp::ObjectDef *s2 = r.addObject(obj("s2", 50, 60));
        CHECK(s2 && r.setObjectType("s2", "big") && r.moveObject("s2", 70, 80)); // a spawned one; the box moves with it afterwards
    }
    { // a runtime built again starts from the db: nothing a script did is kept
        gp::AreaRuntime r(db, a);
        CHECK((order(r) == std::vector<std::string>{"b", "a", "c"}) && !r.has("s1") && r.objects()[1]->y == 100);
    }
    if (g_fail)
        std::cerr << g_fail << " check(s) failed\n";
    else
        std::cout << "area_runtime_edit_test: all passed\n";
    return g_fail ? 1 : 0;
}
