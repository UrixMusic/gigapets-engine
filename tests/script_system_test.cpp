// script_system_test.cpp - engine systems a mod can wrap or replace (System.hooks): before changes the values or cancels, the first replace takes the job over,
// after sees the result; with no hooks the default runs untouched; unknown systems are an error; mods do not overwrite each other.
#include "../src/mods/script_host.hpp"
#include "../src/mods/script_check.hpp"
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

struct Run
{
    int applied = 0;     // what the "engine" did
    bool defaulted = false;
};
static Run decay(gp::ScriptHost &h, double amount, std::string stat = "hunger")
{
    Run r;
    gp::SystemCtx c;
    c.set("stat", stat).set("amount", amount);
    r.defaulted = h.system("care.decay", c, [&] { r.applied = int(c.num("amount")); });
    return r;
}

int main()
{
    { // no hooks: the default, untouched
        gp::ScriptHost h;
        const Run r = decay(h, 3);
        CHECK(r.defaulted && r.applied == 3);
    }
    { // before changes the amount; after sees it; the order across mods is the load order
        gp::ScriptHost h;
        CHECK(h.runString("a", "log_ = ''; local S = System.hooks('care.decay'); function S:before(ctx) ctx.amount = ctx.amount * 2; log_ = log_ .. 'a' end; function S:after(ctx) log_ = log_ .. 'A' .. ctx.amount end"));
        CHECK(h.runString("b", "local S = System.hooks('care.decay'); function S:before(ctx) ctx.amount = ctx.amount + 1; log_ = log_ .. 'b' end"));
        const Run r = decay(h, 3);
        CHECK(r.defaulted && r.applied == 7); // (3 * 2) + 1
        CHECK(h.runString("m", "assert(log_ == 'abA7', log_)"));
        CHECK(h.errors().empty());
    }
    { // return false from before cancels everything: no default, no after
        gp::ScriptHost h;
        CHECK(h.runString("a", "ran_ = 0; local S = System.hooks('care.decay'); function S:before(ctx) if ctx.stat == 'hunger' then return false end end; function S:after(ctx) ran_ = ran_ + 1 end"));
        Run r = decay(h, 3, "hunger");
        CHECK(!r.defaulted && r.applied == 0);
        r = decay(h, 3, "tiredness");
        CHECK(r.defaulted && r.applied == 3);
        CHECK(h.runString("m", "assert(ran_ == 1, ran_)")); // after ran once, for the tiredness one
    }
    { // replace takes over: the default is skipped; only the first mod with a replace runs
        gp::ScriptHost h;
        CHECK(h.runString("a", "n_ = 0; local S = System.hooks('care.decay'); function S:replace(ctx) n_ = n_ + 1 end"));
        CHECK(h.runString("b", "local S = System.hooks('care.decay'); function S:replace(ctx) n_ = n_ + 10 end"));
        const Run r = decay(h, 3);
        CHECK(!r.defaulted && r.applied == 0);
        CHECK(h.runString("m", "assert(n_ == 1, n_)"));
    }
    { // a replace that errors falls back to the default, and is switched off
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:replace(ctx) error('boom') end"));
        Run r = decay(h, 4);
        CHECK(r.defaulted && r.applied == 4);
        CHECK(h.errors().size() == 1);
        r = decay(h, 4);
        CHECK(r.defaulted && r.applied == 4 && h.errors().size() == 1);
    }
    { // ctx values: strings too, and a script cannot turn a number into something else
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:before(ctx) ctx.amount = 'lots'; ctx.stat = 5 end"));
        const Run r = decay(h, 3);
        CHECK(r.defaulted && r.applied == 3); // the wrong types are ignored
    }
    { // a replace can do the work with the normal API
        gp::ScriptHost h;
        int care = 50;
        h.api().adjustCare = [&](const std::string &, int d) { care += d; return d; };
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:replace(ctx) player.adjustCare(ctx.stat, -1) end"));
        const Run r = decay(h, 9);
        CHECK(!r.defaulted && care == 49);
    }
    { // unknown system names are an error at registration; System.list shows the real ones
        gp::ScriptHost h;
        CHECK(!h.runString("a", "System.hooks('care.decya')"));
        CHECK(h.errors().size() == 1 && h.errors()[0].find("no such system") != std::string::npos);
        CHECK(h.runString("b", "local l = System.list(); assert(#l >= 1 and l[1].name == 'care.decay' and l[1].what and l[1].fields)"));
    }
    { // validation: a misspelt function is reported like any other hook
        gp::ScriptHost h;
        CHECK(h.runString("a", "local S = System.hooks('care.decay'); function S:befor(ctx) end"));
        gp::HookTargets t;
        const auto problems = gp::validateHooks(h, t);
        CHECK(problems.size() == 1 && problems[0].find("befor") != std::string::npos && problems[0].find("before") != std::string::npos);
    }
    std::printf(fails ? "script_system_test: %d failure(s)\n" : "script_system_test: all passed\n", fails);
    return fails ? 1 : 0;
}
