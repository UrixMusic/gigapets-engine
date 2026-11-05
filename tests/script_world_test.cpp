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
    int w = 448, h = 288;
    std::string lastObj, lastType, npcCalls;
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
        a.walkable = [this](int px, int py) { return !inRoom && px >= 32 && py >= 32 && px < w - 32 && py < h - 32; }; // a floor with a wall of one tile round it
        a.setObjectType = [this](const std::string &id, const std::string &type) { if (id != "m:obj/s1" || type.rfind("nope", 0) == 0) return false; lastObj = id; lastType = type; return true; };
        auto here = [](const std::string &id) { return id == "m:npc/n"; };
        a.npcWalkTo = [this, here](const std::string &id, int px, int py) { if (!here(id)) return false; npcCalls += "walk" + std::to_string(px) + "," + std::to_string(py) + ";"; return true; };
        a.npcFace = [this, here](const std::string &id, int fc) { if (!here(id)) return false; npcCalls += "face" + std::to_string(fc) + ";"; return true; };
        a.npcPlay = [this, here](const std::string &id, const std::string &an) { if (!here(id) || an == "nope:anim/x") return false; npcCalls += "play" + an + ";"; return true; };
        a.npcStop = [this, here](const std::string &id) { if (!here(id)) return false; npcCalls += "stop;"; return true; };
        a.npcRelease = [this, here](const std::string &id) { if (!here(id)) return false; npcCalls += "release;"; return true; };
        a.npcInfo = [here](const std::string &id, int &px, int &py, int &fc, bool &walking, bool &scripted) { if (!here(id)) return false; px = 40; py = 50; fc = 1; walking = true; scripted = false; return true; };
        a.areaSize = [this](int &aw, int &ah) { if (inRoom) return false; aw = w; ah = h; return true; };
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
    // area.walkable / area.size
    CHECK(h.runString("m", "assert(area.walkable(100, 100) == true and area.walkable(10, 100) == false and area.walkable(100, 280) == false); local w, hh = area.size(); assert(w == 448 and hh == 288)"));
    CHECK(h.runString("m", "assert(area.walkable(5000000, 0) == false)")); // far outside: no, and no error
    CHECK(!h.runString("m", "area.walkable(1.5, 2)"));
    f.inRoom = true;
    CHECK(h.runString("m", "assert(area.walkable(100, 100) == false and area.size() == nil)"));
    f.inRoom = false;
    CHECK(h.runString("m", "assert(object.setType('m:obj/s1', 'm:objtype/hit') == true and object.setType('m:obj/zz', 'm:objtype/hit') == false and object.setType('m:obj/s1', 'nope:objtype/x') == false and object.setType('bad id,', 'm:objtype/hit') == false)") && f.lastObj == "m:obj/s1" && f.lastType == "m:objtype/hit");
    { // npc.walkTo / face / play / stop / release / info, and Npc.hooks(id).onTick
        f.npcCalls.clear();
        CHECK(h.runString("m", R"(
            assert(npc.walkTo('m:npc/n', 120, 80) == true and npc.walkTo('m:npc/zz', 1, 1) == false)
            assert(npc.face('m:npc/n', 'up') == true and npc.face('m:npc/zz', 'up') == false)
            assert(npc.play('m:npc/n', 'm:anim/dance') == true and npc.play('m:npc/n', nil) == true and npc.play('m:npc/n', 'nope:anim/x') == false)
            assert(npc.stop('m:npc/n') == true and npc.release('m:npc/n') == true and npc.stop('m:npc/zz') == false and npc.release('bad id,') == false)
            local i = npc.info('m:npc/n'); assert(i.x == 40 and i.y == 50 and i.facing == 'right' and i.walking == true and i.scripted == false)
            assert(npc.info('m:npc/zz') == nil)
        )"));
        CHECK(f.npcCalls == "walk120,80;face2;playm:anim/dance;play;stop;release;");
        CHECK(!h.runString("m", "npc.face('m:npc/n', 'sideways')")); // a facing that is not one
        CHECK(!h.runString("m", "npc.walkTo('m:npc/n', 1.5, 2)"));
        CHECK(h.runString("m", R"(
            _G.npcSeen = {}
            local N = Npc.hooks('m:npc/n')
            function N:onTick(ctx) local n = ctx.npc; local i = npc.info('m:npc/n'); npcSeen = { n.id, n.x, n.y, n.facing, n.walking, n.scripted, ctx.tick, i and i.x } end
        )"));
        CHECK(h.fireNpcTick("m:npc/n", 33, 44, 2, true, false, 9));
        CHECK(h.runString("m", "assert(npcSeen[1] == 'm:npc/n' and npcSeen[2] == 33 and npcSeen[3] == 44 and npcSeen[4] == 'up' and npcSeen[5] == true and npcSeen[6] == false and npcSeen[7] == 9 and npcSeen[8] == 40)")); // (npc.info inside a hook, which runs as a coroutine)
        CHECK(!h.fireNpcTick("m:npc/other", 1, 2, 3, false, false, 1)); // no hook for that one
    }
    { // the HUD: Game.hooks().onHud(g) and Area.hooks(id).onHud(g) draw over the world, the game's first; g.clear is for scenes only
        gp::ScriptHost hh;
        CHECK(hh.runString("m", R"(
            local G = Game.hooks()
            function G:onHud(g) g.rect(1, 2, 30, 4, {255, 0, 0}) end
            local A = Area.hooks('m:area/t')
            function A:onHud(g) g.num(7, 10, 20); g.text('m:str/x', 5, 6); g.sprite('m:anim/heart', 8, 9) end
            local B = Area.hooks('m:area/clear')
            function B:onHud(g) g.clear({0, 0, 0}) end
            )"));
        const std::vector<gp::DrawCmd> &cmds = hh.drawHud("m:area/t");
        CHECK(cmds.size() == 4 && cmds[0].kind == gp::DrawCmd::Kind::Rect && cmds[0].w == 30 && cmds[1].kind == gp::DrawCmd::Kind::Num && cmds[1].num == 7 &&
              cmds[2].kind == gp::DrawCmd::Kind::Text && cmds[3].kind == gp::DrawCmd::Kind::Sprite && cmds[3].str == "m:anim/heart");
        CHECK(hh.drawHud("m:area/other").size() == 1);       // only the game's
        CHECK(hh.drawHud("m:area/clear").size() == 1);       // g.clear failed (it would wipe the game): the hook is switched off, the game's rect is still drawn
        CHECK(!hh.errors().empty());
        CHECK(hh.runString("m", "local ok = pcall(function() local x = rect end)")); // (the sandbox is unchanged)
        gp::ScriptHost none;
        CHECK(none.drawHud("m:area/t").empty()); // nothing hooked: nothing drawn
        CHECK(!none.runString("m", "g.rect(1, 1, 1, 1, {0,0,0})")); // draw calls only exist inside onHud / onDraw
    }
    { // with nothing wired the calls are harmless
        gp::ScriptHost h2;
        CHECK(h2.runString("m", "assert(player.pos() == nil and player.setPos(1, 2) == false and player.react('happy') == false and area.go('a:b', 1, 2) == false and area.current() == '')"));
        CHECK(h2.runString("m", "player.lock(); player.unlock(); camera.shake()"));
        CHECK(h2.runString("m", "assert(area.walkable(1, 1) == false and area.size() == nil and object.setType('a', 'b') == false and npc.walkTo('a', 1, 2) == false and npc.info('a') == nil and npc.release('a') == false)"));
    }
    std::printf(fails ? "script_world_test: %d failure(s)\n" : "script_world_test: all passed\n", fails);
    return fails ? 1 : 0;
}
