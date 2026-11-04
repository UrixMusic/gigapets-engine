// engine_registry_test.cpp - engine.get / set / list / has / describe through the registry, and save.get / set / keys with the
// Game onLoad / onSave hooks. Needs only registry.hpp, script_host.hpp and Lua.
#include "../mods/script_host.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                             \
    do                                                       \
    {                                                        \
        if (!(c))                                            \
        {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while (0)

int main()
{
    // --- the registry on its own ---
    {
        gp::EngineRegistry r;
        int i = 5;
        unsigned char u = 7;
        float f = 1.5f;
        bool b = false;
        std::string s = "hi";
        r.expose("t.i", &i);
        r.expose("t.u", &u);
        r.expose("t.f", &f, "a float");
        r.expose("t.b", &b);
        r.expose("t.s", &s);
        long calc = 3;
        r.exposeFn("t.ro", "int", [&] { return gp::EngineValue::ofInt(calc * 2); });
        r.expose("bad name", &i); // refused: a space
        CHECK(r.size() == 6 && !r.has("bad name"));
        CHECK(r.get("t.i")->i == 5 && r.get("t.u")->i == 7 && r.get("t.f")->r == 1.5 && !r.get("t.b")->b && r.get("t.s")->s == "hi");
        CHECK(r.set("t.i", gp::EngineValue::ofInt(-9)).empty() && i == -9);
        CHECK(r.set("t.i", gp::EngineValue::ofReal(4.0)).empty() && i == 4);          // a whole float is a whole number
        CHECK(!r.set("t.i", gp::EngineValue::ofReal(4.5)).empty() && i == 4);         // not whole
        CHECK(!r.set("t.i", gp::EngineValue::ofBool(true)).empty() && i == 4);
        CHECK(!r.set("t.u", gp::EngineValue::ofInt(300)).empty() && u == 7);          // outside the type
        CHECK(!r.set("t.u", gp::EngineValue::ofInt(-1)).empty() && u == 7);
        CHECK(r.set("t.u", gp::EngineValue::ofInt(255)).empty() && u == 255);
        CHECK(r.set("t.f", gp::EngineValue::ofInt(3)).empty() && f == 3.0f);
        CHECK(!r.set("t.f", gp::EngineValue::ofStr("x")).empty());
        CHECK(r.set("t.b", gp::EngineValue::ofBool(true)).empty() && b);
        CHECK(r.set("t.s", gp::EngineValue::ofStr("yo")).empty() && s == "yo");
        CHECK(!r.set("t.s", gp::EngineValue::ofInt(1)).empty());
        CHECK(r.set("t.ro", gp::EngineValue::ofInt(1)).find("read-only") != std::string::npos);
        CHECK(r.get("t.ro")->i == 6 && r.kindOf("t.ro") == "int" && r.noteOf("t.f") == "a float" && r.kindOf("t.f") == "number");
        CHECK(!r.get("nope") && !r.set("nope", gp::EngineValue::ofInt(1)).empty());
        CHECK(r.names("t.").size() == 6 && r.names("t.i").size() == 1);
    }
    // --- engine.* from Lua ---
    {
        gp::EngineRegistry r;
        int speed = 4;
        double mul = 1.0;
        bool godMode = false;
        std::string name = "Rex";
        r.expose("pet.speed", &speed);
        r.expose("pet.speedMul", &mul);
        r.expose("game.god", &godMode);
        r.expose("pet.name", &name);
        gp::ScriptHost h;
        h.setRegistry(&r);
        CHECK(h.runString("m",
                          "assert(engine.get('pet.speed') == 4 and math.type(engine.get('pet.speed')) == 'integer')\n"
                          "assert(engine.get('pet.speedMul') == 1.0 and engine.get('game.god') == false and engine.get('pet.name') == 'Rex')\n"
                          "engine.set('pet.speed', 6); engine.set('pet.speedMul', 1.5); engine.set('game.god', true); engine.set('pet.name', 'Max')\n"
                          "engine.set('pet.speedMul', 2)\n" // an integer is fine for a number
                          "assert(engine.has('pet.speed') and not engine.has('pet.nope'))\n"
                          "local l = engine.list('pet.'); assert(#l == 3 and l[1] == 'pet.name' and l[2] == 'pet.speed' and l[3] == 'pet.speedMul')\n"
                          "assert(#engine.list() == 4)\n"
                          "local k, n = engine.describe('pet.speedMul'); assert(k == 'number' and n == '')\n"
                          "assert(engine.describe('x') == nil)\n"));
        CHECK(speed == 6 && mul == 2.0 && godMode && name == "Max");
        CHECK(h.errors().empty());
        // mistakes are script errors that name the problem, and change nothing
        auto bad = [&](const char *code, const char *expect)
        {
            gp::ScriptHost g;
            g.setRegistry(&r);
            const bool ok = g.runString("m", code);
            CHECK(!ok);
            bool found = false;
            for (const std::string &e : g.errors())
                found = found || e.find(expect) != std::string::npos;
            if (!found)
            {
                std::printf("expected an error mentioning '%s' for: %s\n", expect, code);
                for (const std::string &e : g.errors())
                    std::printf("  got: %s\n", e.c_str());
                ++fails;
            }
        };
        bad("engine.get('pet.nope')", "no engine value 'pet.nope'");
        bad("engine.set('pet.nope', 1)", "no engine value 'pet.nope'");
        bad("engine.set('pet.speed', 'fast')", "expects a whole number");
        bad("engine.set('pet.speed', 1.5)", "expects a whole number");
        bad("engine.set('game.god', 1)", "expects true or false");
        bad("engine.set('pet.speed', {})", "expects a number, a boolean or a string");
        bad("engine.set('pet.speed', 2^70)", "expects a whole number");
        CHECK(speed == 6);
        gp::ScriptHost none; // no registry: reading is an error, has is false
        CHECK(none.runString("m", "assert(engine.has('a') == false); assert(#engine.list() == 0)"));
        CHECK(!none.runString("m", "engine.get('a')"));
    }
    // --- save.* and the load / save hooks ---
    {
        gp::EngineRegistry r;
        double mul = 1.0;
        r.expose("pet.speedMul", &mul);
        std::map<std::string, std::string> kv;
        {
            gp::ScriptHost h; // first session: the mod sets a speed and chooses to keep it
            h.setRegistry(&r);
            CHECK(h.runString("m",
                              "local G = Game.hooks()\n"
                              "function G:onLoad(ctx) engine.set('pet.speedMul', save.get('speed', 1.0)) ; loaded = (loaded or 0) + 1 end\n"
                              "function G:onSave(ctx) save.set('speed', engine.get('pet.speedMul')) end\n"));
            CHECK(h.fireGameLoad());
            CHECK(mul == 1.0); // nothing saved yet: the default
            CHECK(h.runString("m", "engine.set('pet.speedMul', 1.75); save.set('n', 42); save.set('flag', true); save.set('who', 'Rex Jr.')"));
            CHECK(h.fireGameSave());
            kv = h.modSaveKv();
            CHECK(kv.count("mod.m.speed") && kv.count("mod.m.n") && kv.count("mod.m.flag") && kv.count("mod.m.who") && kv.size() == 4);
            CHECK(h.runString("m", "local k = save.keys(); assert(#k == 4 and k[1] == 'flag' and k[4] == 'who')"));
            CHECK(h.runString("m", "save.set('n', nil); assert(save.get('n') == nil and save.get('n', 'x') == 'x'); assert(#save.keys() == 3)"));
            CHECK(h.runString("other", "assert(save.get('speed') == nil and #save.keys() == 0)")); // another mod's keys are its own
        }
        {
            gp::ScriptHost h; // second session: the slot is loaded
            h.setRegistry(&r);
            kv["unrelated"] = "x"; // not a mod key: ignored
            h.loadModSave(kv);
            CHECK(h.runString("m",
                              "local G = Game.hooks()\n"
                              "function G:onLoad(ctx) engine.set('pet.speedMul', save.get('speed', 1.0)) end\n"));
            mul = 1.0;
            CHECK(h.fireGameLoad());
            CHECK(mul == 1.75);
            CHECK(h.runString("m", "assert(save.get('n') == 42 and math.type(save.get('n')) == 'integer' and save.get('flag') == true and save.get('who') == 'Rex Jr.' and math.type(save.get('speed')) == 'float')"));
            CHECK(h.errors().empty());
        }
        { // profile.*: the same calls, kept for the whole game, apart from the slot's save
            gp::ScriptHost h;
            CHECK(!h.profileDirty());
            CHECK(h.runString("m", "profile.set('unlocked_fox', true); profile.set('n', 3); save.set('s', 1)"));
            CHECK(h.profileDirty());
            const auto p = h.profileKv();
            CHECK(p.size() == 2 && p.count("mod.m.unlocked_fox") && p.count("mod.m.n") && !h.modSaveKv().count("mod.m.unlocked_fox"));
            CHECK(h.runString("m", "assert(profile.get('unlocked_fox') == true and profile.get('s') == nil and #profile.keys() == 2 and save.get('n') == nil)"));
            h.profileSaved();
            CHECK(!h.profileDirty());
            gp::ScriptHost h2; // the next session
            h2.loadProfile(p);
            CHECK(h2.runString("m", "assert(profile.get('unlocked_fox') == true and profile.get('n') == 3)") && !h2.profileDirty());
            CHECK(h2.runString("other", "assert(profile.get('n') == nil)")); // a mod's own keys
        }
        auto bad = [&](const char *code, const char *expect)
        {
            gp::ScriptHost g;
            const bool ok = g.runString("m", code);
            CHECK(!ok);
            bool found = false;
            for (const std::string &e : g.errors())
                found = found || e.find(expect) != std::string::npos;
            if (!found)
            {
                std::printf("expected an error mentioning '%s' for: %s\n", expect, code);
                ++fails;
            }
        };
        bad("save.set('bad key', 1)", "a save key is");
        bad("save.set('k', {})", "expects a number, a boolean or a string");
        bad("save.set('k', 'a\\nb')", "line break");
        bad("save.set('k', string.rep('x', 1001))", "at most 1000");
        bad("save.set('k', 0/0)", "finite");
        bad("for i = 1, 501 do save.set('k' .. i, i) end", "at most 500 keys");
    }
    if (fails)
        std::printf("%d check(s) failed\n", fails);
    else
        std::printf("engine_registry_test: all passed\n");
    return fails ? 1 : 0;
}
