// mod_state_test.cpp - the Mods screen's data: names and descriptions in manifests, which mods are off, the player's order, and what loading does with them.
#define SDL_MAIN_HANDLED
#include "../src/mods/mod_loader.hpp"
#include "../src/game/frontend/fe_mods.hpp"
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
    std::ofstream(p) << s;
}
static std::string ids(const std::vector<ModEntry> &l)
{
    std::string s;
    for (const ModEntry &e : l)
        s += (s.empty() ? "" : ",") + e.m.id + (e.on ? "" : "!");
    return s;
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "gp_modstate_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "alpha/manifest.json", R"({"id":"alpha","version":"1.2","game_api":"1","name":"Alpha Pack","description":"First."})");
    writeFile(root / "beta/manifest.json", R"({"id":"beta","version":"1.0","game_api":"1","requires":["alpha"],"trusted":true})");
    writeFile(root / "gamma/manifest.json", R"({"id":"gamma","version":"1.0","game_api":"1"})");

    { // manifest: display name (the id without one), description, trusted
        ModManifest m;
        std::string err;
        CHECK(parseManifest(root / "alpha", m, err) && m.name == "Alpha Pack" && m.description == "First." && !m.hasIcon);
        CHECK(parseManifest(root / "beta", m, err) && m.name == "beta" && m.trusted);
    }
    { // state file round trip; junk lines are ignored
        const std::string f = (root / "mods.txt").string();
        ModState s;
        s.order = {"gamma", "alpha"};
        s.off = {"beta"};
        CHECK(saveModState(s, f));
        std::ofstream(f, std::ios::app) << "off Bad Id\nnonsense\n";
        const ModState r = loadModState(f);
        CHECK(r.order == s.order && r.off == s.off);
        CHECK(loadModState((root / "none.txt").string()).order.empty());
    }
    { // the list: alphabetical with no state; the player's order wins where dependencies allow
        CHECK(ids(scanMods(root, ModState{})) == "alpha,beta,gamma");
        ModState s;
        s.order = {"gamma", "beta", "alpha"}; // beta may not go before alpha
        CHECK(ids(scanMods(root, s)) == "gamma,alpha,beta");
    }
    { // off: a mod that requires one that is off is left out too, and says why
        ModState s;
        s.off = {"alpha"};
        const auto l = scanMods(root, s);
        CHECK(ids(l) == "alpha!,beta!,gamma");
        CHECK(l[1].note.find("alpha") != std::string::npos && l[0].note.empty());
        std::vector<ModManifest> ms;
        for (const ModEntry &e : l)
            ms.push_back(e.m);
        std::vector<std::string> why;
        applyModState(ms, s, &why);
        CHECK(ms.size() == 1 && ms[0].id == "gamma" && why.size() == 1);
    }
    { // loading follows the state
        ContentDB db;
        ModState s;
        s.off = {"alpha"};
        s.order = {"gamma"};
        ModReport rep = importMods(db, root, &s);
        CHECK(rep.errors.empty() && rep.loaded == std::vector<std::string>{"gamma"});
        ModState s2;
        s2.order = {"gamma"};
        ContentDB db2;
        ModReport rep2 = importMods(db2, root, &s2);
        CHECK(rep2.loaded == (std::vector<std::string>{"gamma", "alpha", "beta"}));
        ContentDB db3;
        CHECK(importMods(db3, root).loaded == (std::vector<std::string>{"alpha", "beta", "gamma"})); // no state: as before
    }
    { // the screen: Z switches, order mode picks up / moves / puts down, everything saved
        fe::ModsUi ui;
        ui.open(root.string(), (root / "ui_mods.txt").string());
        CHECK(ids(ui.list) == "alpha,beta,gamma" && ui.cur == 0);
        ui.moveCursor(1);
        CHECK(ui.cur == 1);
        ui.moveCursor(1);
        ui.press(); // gamma off
        CHECK(ids(ui.list) == "alpha,beta,gamma!");
        ui.press();
        ui.toggleOrder();
        ui.moveCursor(-1); // to beta
        ui.press();        // picked up
        CHECK(ui.held);
        ui.moveCursor(1); // beta below gamma: allowed (gamma has no relation to beta)
        CHECK(ids(ui.list) == "alpha,gamma,beta" && ui.cur == 2);
        ui.moveCursor(-1);
        ui.moveCursor(-1); // above alpha: it requires alpha, the list shows where it really loads
        CHECK(ids(ui.list) == "alpha,beta,gamma");
        ui.press(); // put down
        CHECK(!ui.held);
        const ModState saved = loadModState((root / "ui_mods.txt").string());
        CHECK(saved.order.size() == 3 && saved.off.empty());
        ui.toggleOrder();
        ui.cur = 0;
        ui.press(); // alpha off: beta too
        CHECK(ids(ui.list) == "alpha!,beta!,gamma");
    }
    CHECK((fe::wrapLines("one two three four", 9) == std::vector<std::string>{"one two", "three", "four"}));
    fs::remove_all(root, ec);
    if (g_fail)
        return 1;
    std::cout << "OK mod_state_test\n";
    return 0;
}
