// mod_list_test.cpp - the mod list kept in a save header, and the warnings when it differs from the mods loaded now.
#include "../src/mods/mod_list.hpp"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main()
{
    using namespace gp;
    CHECK(modListString({"a", "b"}, {{"a", "1.0"}, {"b", "2.1"}}) == "a@1.0,b@2.1");
    CHECK(modListString({}, {}).empty());
    CHECK(modListString({"a"}, {}) == "a@0");
    CHECK(modListChanges("a@1.0,b@2.1", "a@1.0,b@2.1").empty());
    CHECK(modListChanges("", "").empty());
    auto c = modListChanges("a@1.0,b@2.1", "a@1.0");
    CHECK(c.size() == 1 && c[0].find("'b'") != std::string::npos && c[0].find("not loaded now") != std::string::npos);
    c = modListChanges("a@1.0", "a@1.1");
    CHECK(c.size() == 1 && c[0].find("version 1.0") != std::string::npos && c[0].find("1.1 now") != std::string::npos);
    c = modListChanges("a@1.0", "a@1.0,c@3");
    CHECK(c.size() == 1 && c[0].find("'c'") != std::string::npos);
    c = modListChanges("a@1.0,b@1", "b@1,a@1.0");
    CHECK(c.size() == 1 && c[0].find("different order") != std::string::npos);
    CHECK(modListChanges("a@1.0", "").size() == 1);
    std::printf(fails ? "mod_list_test: %d failure(s)\n" : "mod_list_test: all passed\n", fails);
    return fails ? 1 : 0;
}
