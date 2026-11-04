// mod_audio_test.cpp - the `sound` and `music` records (a mod's own, and replacements of the ROM's), the species that name them, and sound.play / sound.music.
#include "../src/mods/mod_loader.hpp"
#include "../src/mods/script_host.hpp"
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
static void writeWav(const fs::path &p)
{
    std::vector<uint8_t> w = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 8, 0, 0, 0, 0, 0, 1, 0, 2, 0, 3, 0};
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(w.data()), std::streamsize(w.size()));
}
static bool mentions(const std::vector<std::string> &v, const std::string &s)
{
    for (const auto &e : v)
        if (e.find(s) != std::string::npos)
            return true;
    return false;
}
static ContentDB baseDb()
{
    ContentDB db;
    db.put(TextDef{"base:text/1", "Hello"});
    SpeciesDef dog;
    dog.id = "base:species/0";
    dog.legacyIndex = 0;
    db.put(std::move(dog));
    return db;
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "gp_audiorec";
    fs::remove_all(root);
    writeFile(root / "snd" / "manifest.json", R"({"id":"snd","version":"1.0","game_api":"1"})");
    writeWav(root / "snd" / "audio" / "a.wav");
    writeFile(root / "snd" / "audio" / "notsound.ogg", "garbage");
    writeFile(root / "snd" / "data" / "a.json", R"([
 {"type":"sound","mode":"add","id":"snd:sound/meow","file":"audio/a.wav","volume":150,"pitch":-3.5},
 {"type":"sound","mode":"add","id":"snd:sound/bark","file":"audio/a.wav"},
 {"type":"sound","mode":"replace","id":"base:sound/84","file":"audio/a.wav","volume":50},
 {"type":"music","mode":"add","id":"snd:music/theme","file":"audio/a.wav","loop_start":0.25,"loop_end":1.5},
 {"type":"music","mode":"replace","id":"base:music/4","file":"audio/a.wav","pitch":12},
 {"type":"sound","mode":"replace","id":"snd:sound/bark","volume":80},
 {"type":"anim","mode":"add","id":"snd:anim/a","frames":[]},
 {"type":"species","mode":"add","id":"snd:species/s","based_on":"base:species/0",
  "art":{"voice":["snd:sound/meow",2,3,4,5,6,7,8,9,10,11,12,13],
         "trickRules":[{"startSound":"snd:sound/bark","chainSounds":["snd:sound/meow",5],"chainReactions":[{"roll":2,"sound":"snd:sound/bark"}]}]}}
])");
    ContentDB db = baseDb();
    ModReport rep = importMods(db, root);
    for (const auto &e : rep.errors)
        std::cerr << e << "\n";
    // (the empty-frames anim is rejected on its own; everything else loads)
    CHECK(mentions(rep.errors, "snd:anim/a"));

    const AudioRegistry &reg = audioRegistry();
    CHECK(reg.sounds.size() == 2 && reg.songs.size() == 1 && reg.soundOver.size() == 1 && reg.songOver.size() == 1);
    const int meow = modSoundNumber("snd:sound/meow"), bark = modSoundNumber("snd:sound/bark");
    CHECK(meow == kModSoundBase && bark == kModSoundBase + 1 && modSongNumber("snd:music/theme") == kModMusicBase);
    CHECK(reg.sounds[0].gain == 1.5f && reg.sounds[0].semitones == -3.5 && reg.sounds[0].path.find("a.wav") != std::string::npos);
    CHECK(reg.sounds[1].gain == 0.8f); // replaced by a later record: only the volume changed, the file stays
    CHECK(reg.soundOver.at(84).gain == 0.5f && reg.songOver.at(4).semitones == 12 && reg.songs[0].loopStart == 0.25 && reg.songs[0].loopEnd == 1.5);
    CHECK(soundFileFor(84) && soundFileFor(meow) && !soundFileFor(5) && songFileFor(4) && songFileFor(kModMusicBase) && !songFileFor(3));

    // a species names a mod's sounds by id, and the ROM's by number
    const SpeciesDef *sd = db.species("snd:species/s");
    CHECK(sd);
    if (sd)
    {
        const SpeciesArt &a = artOf(sd->legacyIndex);
        CHECK(a.voice[0] == meow && a.voice[1] == 2 && a.voice[12] == 13);
        CHECK(a.trickRules[0].startSound == bark && a.trickRules[0].chainSounds.size() == 2 && a.trickRules[0].chainSounds[0] == meow && a.trickRules[0].chainSounds[1] == 5);
        CHECK(a.trickRules[0].chainReactions.size() == 1 && a.trickRules[0].chainReactions[0].sound == bark);
    }

    // loading the mods again gives the same numbers, not more
    {
        ContentDB again = baseDb();
        importMods(again, root);
        CHECK(audioRegistry().sounds.size() == 2 && modSoundNumber("snd:sound/bark") == bark);
    }

    // sound.play / sound.music take the ids too
    {
        ScriptHost h;
        int played = -1, song = -2;
        h.api().playSound = [&](int id, int) { played = id; };
        h.api().playMusic = [&](int s) { song = s; };
        CHECK(h.runString("snd", "sound.play('snd:sound/bark')") && played == bark);
        CHECK(h.runString("snd", "sound.play(84)") && played == 84);
        CHECK(h.runString("snd", "sound.music('snd:music/theme')") && song == kModMusicBase);
        CHECK(h.runString("snd", "sound.music(4)") && song == 4);
        CHECK(h.runString("snd", "sound.music()") && song == -1);
        ScriptHost g;
        CHECK(!g.runString("snd", "sound.play('snd:sound/nope')"));
        ScriptHost g2;
        CHECK(!g2.runString("snd", "sound.music('snd:music/nope')"));
    }

    // mistakes
    auto bad = [&](const std::string &rec, const std::string &want)
    {
        fs::remove_all(root / "snd");
        writeFile(root / "bad" / "manifest.json", R"({"id":"bad","version":"1.0","game_api":"1"})");
        writeWav(root / "bad" / "a.wav");
        writeFile(root / "bad" / "n.ogg", "garbage");
        writeFile(root / "bad" / "data" / "x.json", "[" + rec + "]");
        ContentDB d2 = baseDb();
        const bool ok = mentions(importMods(d2, root).errors, want);
        if (!ok)
            std::cerr << "  wanted: " << want << " for " << rec << "\n";
        return ok;
    };
    CHECK(bad(R"({"type":"sound","mode":"add","id":"bad:sound/x"})", "'file' is required"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"bad:sound/x","file":"missing.wav"})", "missing file"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"bad:sound/x","file":"n.ogg"})", "Ogg"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"bad:sound/x","file":"../a.wav"})", "'..'"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"other:sound/x","file":"a.wav"})", "own prefix"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"base:sound/9","file":"a.wav"})", "replaced with mode 'replace'"));
    CHECK(bad(R"({"type":"sound","mode":"replace","id":"base:sound/999","file":"a.wav"})", "0 to 95"));
    CHECK(bad(R"({"type":"sound","mode":"replace","id":"bad:sound/none","file":"a.wav"})", "no such sound"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"bad:sound/x","file":"a.wav","volume":999})", "'volume'"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"bad:sound/x","file":"a.wav","pitch":99})", "'pitch'"));
    CHECK(bad(R"({"type":"sound","mode":"add","id":"bad:sound/x","file":"a.wav","loop_start":1})", "unknown field"));
    CHECK(bad(R"({"type":"music","mode":"add","id":"bad:music/x","file":"a.wav","loop_start":2,"loop_end":1})", "'loop_start' must come before"));
    CHECK(bad(R"({"type":"music","mode":"add","id":"bad:music/x","file":"a.wav","loop_end":"soon"})", "seconds"));
    CHECK(bad(R"({"type":"species","mode":"add","id":"bad:species/x","based_on":"base:species/0","art":{"voice":["bad:sound/none",2,3,4,5,6,7,8,9,10,11,12,13]}})", "no sound"));

    fs::remove_all(root);
    if (g_fail)
    {
        std::cerr << g_fail << " failed\n";
        return 1;
    }
    std::cout << "mod_audio_test: all passed\n";
    return 0;
}
