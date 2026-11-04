// profile_test.cpp - the profile file and the rule for who a new game offers (SpeciesDef::starterRoom / starterRequires).
#include "../src/game/profile.hpp"
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

int main()
{
    const std::map<std::string, std::string> p = {{"mod.fox.seen", "b1"}, {"mod.fox.no", "b0"}, {"mod.fox.n", "i3"}, {"mod.fox.zero", "i0"},
                                                  {"mod.fox.f", "n0.5"}, {"mod.fox.s", "sx"}, {"mod.fox.empty", "s"}, {"mod.other.k", "b1"}};
    CHECK(profileTruthy(p, "fox", "seen") && !profileTruthy(p, "fox", "no") && profileTruthy(p, "fox", "n") && !profileTruthy(p, "fox", "zero"));
    CHECK(profileTruthy(p, "fox", "f") && profileTruthy(p, "fox", "s") && !profileTruthy(p, "fox", "empty") && !profileTruthy(p, "fox", "missing"));
    CHECK(profileTruthy(p, "fox", "other:k") && !profileTruthy(p, "other", "seen") && profileTruthy(p, "x", "fox:seen"));

    SpeciesDef sd;
    sd.id = "fox:species/fox";
    CHECK(!starterOffered(sd, p)); // not in the room unless the record says so
    sd.starterRoom = true;
    CHECK(starterOffered(sd, p) && starterOffered(sd, {}));
    sd.starterRequires = {"seen"};
    CHECK(starterOffered(sd, p) && !starterOffered(sd, {}));
    sd.starterRequires = {"seen", "other:k"};
    CHECK(starterOffered(sd, p));
    sd.starterRequires = {"seen", "other:missing"};
    CHECK(!starterOffered(sd, p));

    const std::string old = dataDir();
    dataDir() = (std::filesystem::temp_directory_path() / "gp_profile_").string();
    saveProfileFile(p);
    CHECK(loadProfileFile() == p);
    std::filesystem::remove(profilePath());
    CHECK(loadProfileFile().empty()); // none yet
    dataDir() = old;

    if (g_fail)
        return 1;
    std::cout << "profile_test: all passed\n";
    return 0;
}
