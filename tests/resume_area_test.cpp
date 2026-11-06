// A save remembers where the pet was only in an area that asks for it, and forgets it everywhere else.
#include "../src/game/data/resume_area.hpp"
#include <cstdio>
#include <iostream>

static int g_fail = 0;
#define CHECK(c)                                                                      \
    do                                                                                \
    {                                                                                 \
        if (!(c))                                                                     \
        {                                                                             \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n";                    \
            ++g_fail;                                                                 \
        }                                                                             \
    } while (0)

int main()
{
    gp::SaveData s;
    gp::ResumeSpot r;
    CHECK(!gp::loadResume(s, r)); // a fresh save has none

    gp::saveResume(s, "cavemod:area/cave", true, 160, 120);
    CHECK(gp::loadResume(s, r) && r.area == "cavemod:area/cave" && r.x == 160 && r.y == 120);

    gp::saveResume(s, "cavemod:area/cave", true, -5, 7); // a later save replaces it
    CHECK(gp::loadResume(s, r) && r.x == -5 && r.y == 7);

    gp::saveResume(s, "cavemod:area/cave", false, 1, 1); // an area that does not ask: forgotten
    CHECK(!gp::loadResume(s, r) && !s.has("resumeArea") && !s.has("resumeX") && !s.has("resumeY"));

    gp::saveResume(s, "base:area/0", false, 10, 10); // the base game's areas do not, by default
    CHECK(!gp::loadResume(s, r));
    gp::saveResume(s, "base:area/0", true, 10, 20); // unless a mod makes one
    CHECK(gp::loadResume(s, r) && r.area == "base:area/0" && r.y == 20);

    gp::saveResume(s, "", true, 1, 2); // no area at all: forgotten
    CHECK(!gp::loadResume(s, r));

    // the values survive the save file's text form
    gp::saveResume(s, "my_mod:area/the-shed", true, 300, 400);
    const std::string path = "resume_area_test.sav";
    CHECK(gp::saveToFile(path, s));
    gp::SaveData back;
    CHECK(gp::loadFromFile(path, back) && gp::loadResume(back, r) && r.area == "my_mod:area/the-shed" && r.x == 300 && r.y == 400);
    std::remove(path.c_str());

    if (g_fail)
        std::cerr << g_fail << " check(s) failed\n";
    else
        std::cout << "resume_area_test: all passed\n";
    return g_fail ? 1 : 0;
}
