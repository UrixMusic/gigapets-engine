// native_mod_test.cpp - the player's say-so for native mods: the hash of a dll, the allowed list, the question on the Mods screen, the run marker.
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
    std::ofstream(p, std::ios::binary) << s;
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "gp_native_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "mods/nat/manifest.json", R"({"id":"nat","version":"1.0","game_api":"1","name":"Nat","native":"native/x.dll"})");
    writeFile(root / "mods/nat/native/x.dll", "first version");
    writeFile(root / "mods/plain/manifest.json", R"({"id":"plain","version":"1.0","game_api":"1","name":"Plain"})");
    const std::string dll = (root / "mods/nat/native/x.dll").string();
    const std::string allowFile = (root / "allowed.txt").string();

    // hash and allowed list
    CHECK(nativeHash(dll).size() == 16);
    CHECK(nativeHash(dll) == nativeHash(dll));
    CHECK(nativeHash((root / "nope.dll").string()).empty());
    NativeAllow a;
    CHECK(!a.allowed("nat", dll));
    CHECK(a.allow("nat", dll));
    CHECK(a.allowed("nat", dll));
    CHECK(saveNativeAllow(a, allowFile));
    NativeAllow b = loadNativeAllow(allowFile);
    CHECK(b.allowed("nat", dll));
    writeFile(dll, "second version"); // another dll: not allowed any more, and "changed"
    CHECK(!b.allowed("nat", dll));
    CHECK(b.changed("nat", dll));
    CHECK(!b.changed("other", dll));

    // the loader reports the dll of a native mod; a bad path is an error
    ModReport rep;
    {
        ContentDB db;
        rep = importMods(db, (root / "mods").string());
    }
    CHECK(rep.natives.count("nat") == 1 && rep.natives.count("plain") == 0);
    writeFile(root / "bad/manifest.json", R"({"id":"bad","version":"1.0","game_api":"1","native":"../x.dll"})");
    {
        ContentDB db;
        ModReport r2 = importMods(db, (root / "bad").string()); // (a folder of its own: this one is the mod)
        (void)r2;
    }
    writeFile(root / "mods2/bad/manifest.json", R"({"id":"bad","version":"1.0","game_api":"1","native":"../x.dll"})");
    {
        ContentDB db;
        ModReport r2 = importMods(db, (root / "mods2").string());
        CHECK(!r2.errors.empty() && r2.natives.empty());
    }

    // the Mods screen: Z on a native mod that is not allowed asks first; No leaves things as they were; Allow switches it on
    writeFile(dll, "third version");
    fs::create_directories(root / "data");
    gp::dataDir() = (root / "data").string() + "/";
    {
        fe::ModsUi ui;
        ui.open((root / "mods").string(), (root / "data/mods.txt").string());
        CHECK(ui.list.size() == 2);
        int natIdx = ui.list[0].m.id == "nat" ? 0 : 1;
        ui.cur = natIdx;
        CHECK(ui.needsOk(ui.list[size_t(natIdx)]));
        ui.press();
        CHECK(ui.confirm == natIdx && ui.confirmYes == 0);
        ui.moveCursor(1); // the answer swaps
        CHECK(ui.confirmYes == 1);
        ui.moveCursor(1);
        CHECK(ui.confirmYes == 0);
        ui.press(); // No
        CHECK(ui.confirm < 0 && !ui.changed);
        ui.press();
        CHECK(ui.cancelConfirm() && ui.confirm < 0); // X closes it too
        CHECK(!ui.cancelConfirm());
        ui.press();
        ui.moveCursor(1);
        ui.press(); // Allow
        CHECK(ui.confirm < 0 && ui.changed);
        CHECK(!ui.needsOk(ui.list[size_t(ui.cur)]));
        CHECK(loadNativeAllow(nativeAllowPath()).allowed("nat", dll)); // kept in the file
        // now Z switches it off and on like any mod
        ui.press();
        CHECK(!ui.list[size_t(ui.cur)].on);
        ui.press();
        CHECK(ui.list[size_t(ui.cur)].on && ui.confirm < 0);
        // a changed dll asks again
        writeFile(dll, "fourth version");
        ui.open((root / "mods").string(), (root / "data/mods.txt").string());
        for (int i = 0; i < int(ui.list.size()); ++i)
            if (ui.list[size_t(i)].m.id == "nat")
                ui.cur = i;
        ui.press();
        CHECK(ui.confirm >= 0);
    }

    // the run marker is there while it lives and gone after
    {
        const std::string mk = (root / "running.txt").string();
        {
            NativeRunMarker m;
            m.arm(mk);
            CHECK(fileExists(mk));
        }
        CHECK(!fileExists(mk));
    }
    fs::remove_all(root, ec);
    if (g_fail)
        return 1;
    std::cout << "native_mod_test: ok\n";
    return 0;
}
