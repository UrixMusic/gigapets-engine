// audio_names_test.cpp - the readable names of the ROM's sounds and songs ("base:sound/kaboing", "base:music/ufo"), and the script calls that take them.
#include "../src/mods/script_host.hpp"
#include <cstdio>
#include <set>

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

int main()
{
    using namespace gp;
    { // the tables: unique names and numbers, plain lowercase names, every one resolves both ways
        for (int pass = 0; pass < 2; ++pass)
        {
            int c = 0;
            const AudioName *t = pass == 0 ? soundNames(c) : songNames(c);
            std::set<std::string> names;
            std::set<int> numbers;
            bool ok = true;
            for (int i = 0; i < c; ++i)
            {
                const std::string n = t[i].name;
                ok = ok && names.insert(n).second && numbers.insert(t[i].number).second && !n.empty() && n.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos &&
                     !(n[0] >= '0' && n[0] <= '9');
                const std::string id = std::string(pass == 0 ? "base:sound/" : "base:music/") + n, num = std::string(pass == 0 ? "base:sound/" : "base:music/") + std::to_string(t[i].number);
                const int a = pass == 0 ? romSoundNumber(id) : romSongNumber(id), b = pass == 0 ? romSoundNumber(num) : romSongNumber(num);
                ok = ok && a == t[i].number && b == t[i].number && (pass == 0 ? soundNameOf(t[i].number) : songNameOf(t[i].number)) == n;
            }
            CHECK(ok);
            CHECK(pass == 0 ? c == 80 : c == 9);
        }
    }
    CHECK(romSoundNumber("base:sound/kaboing") == 91 && romSoundNumber("base:sound/91") == 91 && romSoundNumber("base:sound/click_button") == 89 && romSoundNumber("base:sound/sine") == 0);
    CHECK(romSongNumber("base:music/ufo") == 8 && romSongNumber("base:music/8") == 8 && romSongNumber("base:music/magma_mountain") == 3 && romSongNumber("base:music/gigaville") == 4);
    CHECK(romSoundNumber("base:sound/96") == -1 && romSoundNumber("base:sound/") == -1 && romSoundNumber("base:sound/nope") == -1 && romSoundNumber("base:music/ufo") == -1 && romSoundNumber("mod:sound/kaboing") == -1);
    CHECK(romSoundNumber("base:sound/0x53") == -1 && romSoundNumber("base:sound/00000084") == -1 && romSongNumber("base:music/64") == -1 && romSongNumber("base:sound/ufo") == -1);
    CHECK(soundNameOf(66).empty() && soundNameOf(84).empty() && soundNameOf(-1).empty() && songNameOf(9).empty()); // the mini pet's sounds have no name yet

    // the registry: the ROM's names, else a mod's own sound
    audioRegistry().clear();
    audioRegistry().sounds.push_back({});
    audioRegistry().sounds.back().id = "m:sound/meow";
    CHECK(soundNumberOf("base:sound/kaboing") == 91 && soundNumberOf("m:sound/meow") == kModSoundBase && soundNumberOf("m:sound/none") == -1);
    audioRegistry().clear();

    { // the script calls
        ScriptHost h;
        int sound = -1, vol = -1, song = -2;
        h.api().playSound = [&](int id, int v) { sound = id; vol = v; };
        h.api().playMusic = [&](int n) { song = n; };
        CHECK(h.runString("m", "sound.play('base:sound/kaboing')") && sound == 91 && vol == 255);
        CHECK(h.runString("m", "sound.play('base:sound/84', 100)") && sound == 84 && vol == 100);
        CHECK(h.runString("m", "sound.play(45)") && sound == 45);
        CHECK(h.runString("m", "sound.music('base:music/ufo')") && song == 8);
        CHECK(h.runString("m", "sound.music('base:music/gigaville')") && song == 4);
        CHECK(h.runString("m", "sound.music(3)") && song == 3);
        CHECK(!h.runString("m", "sound.play('base:sound/nope')"));
        CHECK(!h.runString("m", "sound.music('base:music/nope')"));
    }
    std::printf(fails ? "audio_names_test: %d failure(s)\n" : "audio_names_test: all passed\n", fails);
    return fails ? 1 : 0;
}
