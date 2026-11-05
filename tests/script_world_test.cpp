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
    std::string lastObj, lastType, npcCalls, plays;
    bool playing = false;
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
        a.petAnimNumber = [](const std::string &what) -> int
        {
            if (what == "sit") return 20;
            if (what == "base:anim/34") return 34;
            if (what == "m:anim/swing") return 0x30001;
            return -1;
        };
        a.petPlay = [this](int an, bool loop, int rate, const std::vector<int> &next)
        {
            if (busy) return false;
            plays += std::to_string(an) + (loop ? "L" : "o") + std::to_string(rate);
            for (int n : next) plays += "," + std::to_string(n);
            plays += ";";
            playing = true;
            return true;
        };
        a.petStopAnim = [this] { playing = false; plays += "stop;"; };
        a.petPlaying = [this] { return playing; };
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
    { // player.play / stopAnim / playing
        f.busy = false;
        CHECK(h.runString("m", R"(
            assert(player.play('sit') == true)
            assert(player.play('base:anim/34', { loop = true, rate = 8 }) == true)
            assert(player.play('m:anim/swing', { next = { 'sit', 34 } }) == true)
            assert(player.play(5) == true)
            assert(player.playing() == true); player.stopAnim(); assert(player.playing() == false)
        )"));
        CHECK(f.plays == "20o14;34L8;196609o14,20,34;5o14;stop;");
        CHECK(!h.runString("m", "player.play('nonsense')"));       // no such animation: an error that says what is allowed
        CHECK(!h.runString("m", "player.play('sit', { rate = 0 })")); // frames a second 1 to 60
        CHECK(!h.runString("m", "player.play(99999)"));
        CHECK(!h.runString("m", "player.play('sit', { next = { 'nonsense' } })"));
        f.busy = true;
        CHECK(h.runString("m", "assert(player.play('sit') == false)")); // the pet is busy
        f.busy = false;
    }
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
    { // onPostDraw: over the finished frame; g.rect has blend modes
        gp::ScriptHost hh;
        CHECK(hh.runString("m", R"(
            local G = Game.hooks()
            function G:onPostDraw(g) g.rect(0, 0, 320, 240, {200, 150, 100}, true, 'mul'); g.rect(0, 0, 10, 10, {9, 9, 9, 50}, true, 'add'); g.rect(0, 0, 1, 1, {0,0,0}) end
            )"));
        const std::vector<gp::DrawCmd> &c = hh.drawPost();
        CHECK(c.size() == 3 && c[0].blend == 2 && c[1].blend == 1 && c[2].blend == 0);
        CHECK(hh.runString("m", "local G = Game.hooks(); function G:onPostDraw(g) g.rect(0,0,1,1,{0,0,0},true,'xor') end"));
        hh.drawPost();
        CHECK(!hh.errors().empty());
        gp::ScriptHost none;
        CHECK(none.drawPost().empty());
    }
    { // richer drawing: shapes, canvases, blits, meshes and sprite transforms become draw commands; mistakes are script errors
        gp::ScriptHost hh;
        CHECK(hh.runString("m", R"(
            local G = Game.hooks()
            function G:onPostDraw(g)
              g.line(0, 0, 10, 10, {1, 2, 3}, 4)
              g.circle(5, 6, 7, {9, 9, 9, 50}, false, 'add')
              g.poly({0, 0, 10, 0, 5, 9}, {1, 1, 1})
              g.canvas('c1', 32, 16, function() g.rect(0, 0, 4, 4, {5, 5, 5}) end)
              g.blit('c1', 3, 4, { scale = 2, angle = 45, tint = {10, 20, 30}, alpha = 0.5, mode = 'mul', pivot = {1, 2} })
              g.blit('@frame', 0, 0)
              g.mesh('@frame', { {0, 0, 0, 0}, {10, 0, 1, 0}, {0, 10, 0, 1, {255, 0, 0}} })
              g.sprite('m:anim/a', 1, 2, { scale = 3, angle = 90, flip = true })
            end
            )"));
        const std::vector<gp::DrawCmd> &c = hh.drawPost();
        using K = gp::DrawCmd::Kind;
        CHECK(c.size() == 10);
        if (c.size() == 10)
        {
            CHECK(c[0].kind == K::Line && c[0].thick == 4 && c[0].x2 == 10);
            CHECK(c[1].kind == K::Circle && c[1].w == 7 && !c[1].fill && c[1].blend == 1);
            CHECK(c[2].kind == K::Poly && c[2].pts.size() == 6 && c[2].fill);
            CHECK(c[3].kind == K::CanvasBegin && c[3].str == "c1" && c[3].w == 32 && c[4].kind == K::Rect && c[5].kind == K::CanvasEnd);
            CHECK(c[6].kind == K::Blit && c[6].sx == 2.f && c[6].angle == 45.f && c[6].tg == 20 && c[6].ta == 128 && c[6].blend == 2 && c[6].hasPivot && c[6].pivotY == 2.f);
            CHECK(c[7].kind == K::Blit && c[7].str == "@frame");
            CHECK(c[8].kind == K::Mesh && c[8].pts.size() == 24 && c[8].idx.size() == 3 && c[8].pts[8 + 4] == 255.f && c[8].pts[16 + 4] == 255.f && c[8].pts[16 + 5] == 0.f);
            CHECK(c[9].kind == K::Sprite && c[9].sx == 3.f && c[9].sy == 3.f && c[9].angle == 90.f && c[9].flip);
        }
        CHECK(hh.errors().empty());
        const char *bad[] = {
            "g.canvas('a', 10, 10, function() g.canvas('b', 5, 5, function() end) end)",           // canvases do not nest
            "g.canvas('@frame', 10, 10, function() end)",                                           // @ is not a canvas name
            "g.canvas('a', 5000, 10, function() end)",                                              // too big
            "g.blit('bad name!', 0, 0)",                                                            // a name of letters, digits and _ - : /
            "g.mesh('@frame', { {0, 0, 0, 0} })",                                                  // 3 vertices at least
            "g.mesh('@frame', { {0, 0, 0, 0}, {1, 1, 1, 1}, {2, 2, 0, 0} }, { 1, 2, 9 })",         // a vertex that is not there
            "g.poly({0, 0, 1, 1}, {1, 1, 1})",                                                      // 3 corners at least
            "g.line(0, 0, 1, 1, {1, 1, 1}, 0)",                                                     // a width of at least 1
            "g.sprite('m:anim/a', 1, 1, { scale = 'big' })",                                        // a number
            "g.blit('c', 0, 0, { mode = 'xor' })",                                                  // a known mode
        };
        for (const char *b : bad)
        {
            gp::ScriptHost bh;
            CHECK(bh.runString("m", std::string("local G = Game.hooks(); function G:onPostDraw(g) ") + b + " end"));
            bh.drawPost();
            CHECK(!bh.errors().empty());
        }
        CHECK(hh.drawPost().size() == 10); // (and it works again the next frame: nothing is left open)
    }
    { // g.shader: uniforms as numbers or lists of 2..4 numbers; mistakes are script errors
        gp::ScriptHost hh;
        CHECK(hh.runString("m", "local G = Game.hooks(); function G:onPostDraw(g) g.shader('m:shader/a', { strength = 0.5, tint = { 1, 0.5, 0.25 } }); g.shader('m:shader/b') end"));
        const std::vector<gp::DrawCmd> &c = hh.drawPost();
        CHECK(c.size() == 2 && c[0].kind == gp::DrawCmd::Kind::Shader && c[0].str == "m:shader/a" && c[0].uniforms.size() == 2 && c[1].uniforms.empty());
        if (c.size() == 2 && c[0].uniforms.size() == 2)
        {
            bool tintOk = false, strengthOk = false;
            for (const auto &[name, v] : c[0].uniforms)
            {
                tintOk = tintOk || (name == "tint" && v.size() == 3 && v[1] == 0.5f);
                strengthOk = strengthOk || (name == "strength" && v.size() == 1 && v[0] == 0.5f);
            }
            CHECK(tintOk && strengthOk);
        }
        const char *bad[] = {"g.shader('bad id!')", "g.shader('m:s', { [1] = 2 })", "g.shader('m:s', { ['1x'] = 2 })", "g.shader('m:s', { a = 'x' })", "g.shader('m:s', { a = { 1, 2, 3, 4, 5 } })",
                             "g.shader('m:s', { a = {} })", "g.shader('m:s', { a = { 1, 'x' } })"};
        for (const char *b : bad)
        {
            gp::ScriptHost bh;
            CHECK(bh.runString("m", std::string("local G = Game.hooks(); function G:onPostDraw(g) ") + b + " end"));
            bh.drawPost();
            CHECK(!bh.errors().empty());
        }
    }
    { // onPostWorld, g.keep and the textures of g.shader
        gp::ScriptHost hh;
        CHECK(hh.runString("m", R"(
            local G = Game.hooks()
            function G:onPostWorld(g) g.shader('m:shader/a', {}, { textures = { world = 'world', other = 'o2' } }); g.keep('world') end
            )"));
        const std::vector<gp::DrawCmd> &c = hh.drawPostWorld();
        CHECK(c.size() == 2 && c[0].kind == gp::DrawCmd::Kind::Shader && c[0].shaderTex.size() == 2 && c[1].kind == gp::DrawCmd::Kind::Keep && c[1].str == "world");
        CHECK(hh.drawPost().empty()); // the two hooks are separate
        const char *bad[] = {"g.keep('@frame')", "g.keep('bad name!')", "g.shader('m:s', {}, { textures = { a = 5 } })", "g.shader('m:s', {}, { textures = { ['9x'] = 'w' } })",
                             "g.shader('m:s', {}, { textures = { a = '@frame' } })", "g.shader('m:s', {}, { textures = { a = 'w', b = 'w', c = 'w', d = 'w', e = 'w' } })"};
        for (const char *b : bad)
        {
            gp::ScriptHost bh;
            CHECK(bh.runString("m", std::string("local G = Game.hooks(); function G:onPostWorld(g) ") + b + " end"));
            bh.drawPostWorld();
            CHECK(!bh.errors().empty());
        }
    }
    { // with nothing wired the calls are harmless
        gp::ScriptHost h2;
        CHECK(h2.runString("m", "assert(player.pos() == nil and player.setPos(1, 2) == false and player.react('happy') == false and area.go('a:b', 1, 2) == false and area.current() == '')"));
        CHECK(h2.runString("m", "player.lock(); player.unlock(); camera.shake()"));
        CHECK(h2.runString("m", "assert(area.walkable(1, 1) == false and area.size() == nil and object.setType('a', 'b') == false and npc.walkTo('a', 1, 2) == false and npc.info('a') == nil and npc.release('a') == false and player.play(5) == false and player.playing() == false)"));
    }
    std::printf(fails ? "script_world_test: %d failure(s)\n" : "script_world_test: all passed\n", fails);
    return fails ? 1 : 0;
}
