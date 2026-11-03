// mod_species_test.cpp - the `species` record: a mod's own pet species (from scratch or a clone of another one) and its animation numbers.
#include "../src/mods/mod_loader.hpp"
#include "../src/loaders/png.hpp"
#include "../src/game/save_player.hpp"
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
static void solidPng(const fs::path &p)
{
    fs::create_directories(p.parent_path());
    std::vector<uint8_t> px(4 * 4 * 4, 200);
    writePng(p.string(), 4, 4, px);
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
    ItemDef gem;
    gem.id = "base:item/173";
    db.put(std::move(gem));
    SpeciesDef dog;
    dog.id = "base:species/0";
    dog.legacyIndex = 0;
    dog.defaultPalette = 0x1df;
    dog.startHealth = 0x41;
    dog.startMoneyMin = 0x28;
    dog.startMoneyMax = 0x3d;
    dog.starterItems = {{"base:item/173", 1}};
    db.put(std::move(dog));
    return db;
}

int main()
{
    const size_t baseCount = speciesArts().size(); // the base game's three
    CHECK(baseCount == 3);
    const fs::path root = fs::temp_directory_path() / "gp_speciestest";
    fs::remove_all(root);
    writeFile(root / "fox" / "manifest.json", R"({"id":"fox","version":"1.0","game_api":"1"})");
    solidPng(root / "fox" / "a.png");
    writeFile(root / "fox" / "data" / "a.json", R"([
 {"type":"anim","mode":"add","id":"fox:anim/up","frames":[{"png":"a.png"}]},
 {"type":"anim","mode":"add","id":"fox:anim/down","frames":[{"png":"a.png"}]},
 {"type":"anim","mode":"add","id":"fox:anim/side","frames":[{"png":"a.png"}]},
 {"type":"anim","mode":"add","id":"fox:anim/idle","frames":[{"png":"a.png"}]}
])");
    writeFile(root / "fox" / "data" / "s.json", R"([
 {"type":"species","mode":"add","id":"fox:species/fox","default_palette":99,"own_illness":4,
  "start":{"health":70,"hunger":50},"money":[10,20],"starter_items":[{"item":"base:item/173","count":2}],
  "art":{"walkUp":"fox:anim/up","walkDown":"fox:anim/down","walkSide":"fox:anim/side","idleBase":"fox:anim/idle","dig":120,
         "bowlX":300,"facesRight":true,"voice":[1,2,3,4,5,6,7,8,9,10,11,12,13],"raisedAnims":["fox:anim/up"],
         "trickRules":[{"startSound":7,"startSoundDelay":1.5},{"chainReactions":[{"roll":2,"anim":"fox:anim/idle","sound":9}]}]}},
 {"type":"species","mode":"add","id":"fox:species/bigdog","based_on":"base:species/0","own_illness":8,"art":{"walkSide":"fox:anim/side","wideShadow":true}}
])");
    ContentDB db = baseDb();
    ModReport rep = importMods(db, root);
    if (!rep.errors.empty())
        for (const auto &e : rep.errors)
            std::cerr << e << "\n";
    CHECK(rep.errors.empty());
    CHECK(speciesArts().size() == baseCount + 2);

    const SpeciesDef *fox = db.species("fox:species/fox");
    CHECK(fox && fox->legacyIndex == int(baseCount) && db.speciesByLegacy(int(baseCount)) == fox);
    CHECK(fox && fox->defaultPalette == 99 && fox->ownIllness == 4 && fox->startHealth == 70 && fox->startHunger == 50 && fox->startMoneyMin == 10 && fox->startMoneyMax == 20);
    CHECK(fox && fox->starterItems.size() == 1 && fox->starterItems[0].count == 2);
    if (fox)
    {
        const SpeciesArt &a = artOf(fox->legacyIndex);
        CHECK(isModAnim(a.walkUp) && modAnimName(a.walkUp) == "fox:anim/up" && isModAnim(a.idleBase) && modAnimName(a.walkSide) == "fox:anim/side");
        CHECK(a.dig == 120 && a.bowlX == 300 && a.facesRight && a.voice[0] == 1 && a.voice[12] == 13);
        CHECK(a.raisedAnims.size() == 1 && a.raisedAnims[0] == a.walkUp && a.sitDown == -1 && a.walkSideStyle == -1);
        CHECK(a.trickRules[0].startSound == 7 && a.trickRules[0].startSoundDelay == 1.5 && a.trickRules[1].chainReactions.size() == 1);
        CHECK(a.trickRules[1].chainReactions[0].sound == 9 && a.trickRules[1].chainReactions[0].anim == a.idleBase);
        CHECK(knownSpecies(fox->legacyIndex) && !knownSpecies(int(baseCount) + 2));
        CHECK(animKey(a.walkUp, 0x1df) == "fox:anim/up" && animKey(5, 0x1df) == "base:anim/5@1df" && animKey(5, -1) == "base:anim/5");
    }
    const SpeciesDef *big = db.species("fox:species/bigdog");
    CHECK(big && big->legacyIndex == int(baseCount) + 1 && big->ownIllness == 8 && big->defaultPalette == 0x1df && big->starterItems.size() == 1);
    if (big)
    {
        const SpeciesArt &a = artOf(big->legacyIndex), &d = artOf(0);
        CHECK(a.wideShadow && !d.wideShadow && isModAnim(a.walkSide) && a.walkUp == d.walkUp && a.trickMain[0] == d.trickMain[0]);
        CHECK(a.trickRules[2].chainAnim == d.trickRules[2].chainAnim && a.bowlX == d.bowlX);
    }
    CHECK(artOf(0).walkSide == artOf(0).walkSide && !isModAnim(artOf(0).walkSide)); // the base species are untouched

    { // a save keeps a mod's species by id (its number changes with the mods), a base one as its number
        SaveData sv;
        saveSpecies(sv, int(baseCount) + 1);
        CHECK(sv.getInt("species") == 0 && sv.getStr("speciesId") == "fox:species/bigdog" && loadSpecies(sv) == int(baseCount) + 1);
        saveSpecies(sv, 2);
        CHECK(sv.getInt("species") == 2 && !sv.has("speciesId") && loadSpecies(sv) == 2);
        SaveData gone; // a mod that is no longer there: back to the number saved beside the id
        gone.setInt("species", 0);
        gone.setStr("speciesId", "removed:species/x");
        CHECK(loadSpecies(gone) == 0 && speciesNumber("removed:species/x") == -1 && speciesNumber("base:species/1") == 1);
    }

    // mistakes are reported and add nothing
    auto bad = [&](const std::string &rec, const std::string &want)
    {
        const size_t n = speciesArts().size();
        fs::remove_all(root / "bad");
        writeFile(root / "bad" / "manifest.json", R"({"id":"bad","version":"1.0","game_api":"1"})");
        solidPng(root / "bad" / "a.png");
        writeFile(root / "bad" / "data" / "x.json", "[{\"type\":\"anim\",\"mode\":\"add\",\"id\":\"bad:anim/a\",\"frames\":[{\"png\":\"a.png\"}]}," + rec + "]");
        ContentDB d2 = baseDb();
        fs::remove_all(root / "fox");
        ModReport r = importMods(d2, root);
        const bool ok = mentions(r.errors, want) && speciesArts().size() == n;
        if (!ok)
            for (const auto &e : r.errors)
                std::cerr << "  got: " << e << "\n";
        return ok;
    };
    const std::string head = R"({"type":"species","mode":"add","id":"bad:species/x",)";
    CHECK(bad(head + R"("art":{"walkUp":"bad:anim/a"}})", "walkUp, walkDown"));            // from scratch needs the walks
    CHECK(bad(head + R"("based_on":"base:species/9"})", "'based_on'"));
    CHECK(bad(head + R"("based_on":"base:species/0","art":{"walkUp":"bad:anim/none"}})", "no anim"));
    CHECK(bad(head + R"("based_on":"base:species/0","art":{"nonsense":1}})", "unknown art field"));
    CHECK(bad(head + R"("based_on":"base:species/0","art":{"voice":[1,2]}})", "exactly 13"));
    CHECK(bad(head + R"("based_on":"base:species/0","art":{"idleStyle":"bad:anim/a"}})", "must be a number"));
    CHECK(bad(head + R"("based_on":"base:species/0","starter_items":[{"item":"base:item/999"}]})", "starter item"));
    CHECK(bad(head + R"("based_on":"base:species/0","art":{"trickRules":[{"zzz":1}]}})", "unknown field"));
    CHECK(bad(head + R"("based_on":"base:species/0","art":{"trickNames":["base:text/1","base:text/1","nope"]}})", "trickNames"));

    fs::remove_all(root);
    if (g_fail)
    {
        std::cerr << g_fail << " failed\n";
        return 1;
    }
    std::cout << "mod_species_test: all passed\n";
    return 0;
}
