// script_world_test.cpp - the walking-world script API: player.pos / setPos / lock / unlock / react, area.go / current, camera.shake, and the keys an Area onTick is told.
// Only script_host.hpp and Lua: the game side is an in-memory fake, so what the HOST checks and returns is tested on its own.
#include "../src/mods/script_host.hpp"
#include <cstdio>

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
    int x = 100, y = 200, lock = 0, shakes = 0, lastReact = -1, trips = 0;
    bool inRoom = false, busy = false;
    std::string area = "base:area/0", tripArea;
    int tripX = 0, tripY = 0;
    void wire(gp::ScriptApi &a)
    {
        a.getPetPos = [this](int &px, int &py) { if (inRoom) return false; px = x; py = y; return true; };
        a.setPetPos = [this](int px, int py) { if (inRoom) return false; x = px; y = py; return true; };
        a.lockPet = [this](int w) { lock |= w; };
        a.unlockPet = [this] { lock = 0; };
        a.petReact = [this](int k) { if (busy) return false; lastReact = k; return true; };
        a.travel = [this](const std::string &id, int px, int py) { if (id.rfind("nope", 0) == 0) return false; ++trips; tripArea = id; tripX = px; tripY = py; return true; };
        a.currentArea = [this] { return area; };
        a.cameraShake = [this] { ++shakes; };
    }
};

int main()
{
    gp::ScriptHost h;
    Fake f;
    f.wire(h.api());
    CHECK(h.runString("m", "local x, y = player.pos(); assert(x == 100 and y == 200)"));
    CHECK(h.runString("m", "assert(player.setPos(150, 260) == true)"));
    CHECK(f.x == 150 && f.y == 260);
    CHECK(!h.runString("m", "player.setPos(1.5, 2)"));      // whole numbers only
    CHECK(!h.runString("m", "player.setPos(10000000, 0)")); // and in range
    f.inRoom = true;
    CHECK(h.runString("m", "assert(player.pos() == nil and player.setPos(1, 1) == false)")); // no pet in the world (a house room)
    f.inRoom = false;
    // lock: "move", "actions", "all" (the default) add up; unlock frees; a typo is an error that names the choices
    CHECK(h.runString("m", "player.lock('move')") && f.lock == 1);
    CHECK(h.runString("m", "player.lock('actions')") && f.lock == 3);
    CHECK(h.runString("m", "player.unlock()") && f.lock == 0);
    CHECK(h.runString("m", "player.lock()") && f.lock == 3);
    CHECK(!h.runString("m", "player.lock('everything')"));
    // react
    CHECK(h.runString("m", "assert(player.react('shocked') == true)") && f.lastReact == 2);
    CHECK(h.runString("m", "assert(player.react('happy') == true)") && f.lastReact == 0);
    CHECK(!h.runString("m", "player.react('dance')"));
    f.busy = true;
    CHECK(h.runString("m", "assert(player.react('sad') == false)"));
    // area.go / area.current / camera.shake
    CHECK(h.runString("m", "assert(area.current() == 'base:area/0'); assert(area.go('m:area/pit', 224, 215) == true)"));
    CHECK(f.trips == 1 && f.tripArea == "m:area/pit" && f.tripX == 224 && f.tripY == 215);
    CHECK(h.runString("m", "assert(area.go('nope:area/x', 1, 1) == false)")); // the game says no
    CHECK(h.runString("m", "assert(area.go('bad id,', 1, 1) == false)"));     // forbidden characters never get to the game
    CHECK(!h.runString("m", "area.go('m:area/pit', -1, 5)"));
    CHECK(f.trips == 1);
    CHECK(h.runString("m", "camera.shake(); camera.shake()") && f.shakes == 2);
    // an Area onTick is told the keys: ctx.input (pressed this tick) and ctx.held
    CHECK(h.runString("m", R"(
        _G.seen = {}
        local A = Area.hooks('m:area/t')
        function A:onTick(ctx)
          seen.z = ctx.input.z; seen.left = ctx.input.left; seen.hz = ctx.held.z; seen.hleft = ctx.held.left; seen.hup = ctx.held.up; seen.tick = ctx.tick
        end)"));
    gp::SceneInput in;
    in.z = true;
    in.left = true;
    in.heldLeft = true;
    in.heldUp = false;
    CHECK(h.fireAreaTick("m:area/t", 7, in));
    CHECK(h.runString("m", "assert(seen.z == true and seen.left == true and seen.hz == false and seen.hleft == true and seen.hup == false and seen.tick == 7)"));
    CHECK(h.fireAreaTick("m:area/t", 8)); // with no keys given they are all false
    CHECK(h.runString("m", "assert(seen.z == false and seen.hleft == false)"));
    { // with nothing wired the calls are harmless
        gp::ScriptHost h2;
        CHECK(h2.runString("m", "assert(player.pos() == nil and player.setPos(1, 2) == false and player.react('happy') == false and area.go('a:b', 1, 2) == false and area.current() == '')"));
        CHECK(h2.runString("m", "player.lock(); player.unlock(); camera.shake()"));
    }
    std::printf(fails ? "script_world_test: %d failure(s)\n" : "script_world_test: all passed\n", fails);
    return fails ? 1 : 0;
}
