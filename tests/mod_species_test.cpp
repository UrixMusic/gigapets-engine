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
         "bowlX":300,"facesRight":true,"loadPicture":"fox:anim/idle","loadX":10,"loadY":20,"voice":[1,2,3,4,5,6,7,8,9,10,11,12,13],"raisedAnims":["fox:anim/up"],
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

    { // loading the mods again (the front end and then the game do) gives the same numbers, not new ones
        ContentDB again = baseDb();
        ModReport r2 = importMods(again, root);
        CHECK(r2.errors.empty() && speciesArts().size() == baseCount + 2);
        const SpeciesDef *f2 = again.species("fox:species/fox");
        CHECK(f2 && f2->legacyIndex == int(baseCount) && speciesNumber("fox:species/fox") == int(baseCount));
    }
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
        CHECK(a.loadPicture == a.idleBase && isModAnim(a.loadPicture) && a.loadX == 10 && a.loadY == 20 && a.loadTile == -1);
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
        CHECK(a.loadTile == 0x9f0 && a.loadX == 0 && a.loadY == 0x7c && a.loadPicture == -1); // a clone keeps the base species' big picture
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

    { // the starter room: only what the record says; a patch can change a base species too
        fs::remove_all(root / "fox");
        writeFile(root / "st" / "manifest.json", R"({"id":"st","version":"1.0","game_api":"1"})");
        solidPng(root / "st" / "a.png");
        writeFile(root / "st" / "data" / "a.json", R"([
 {"type":"palette","mode":"add","id":"st:pal/red","colors":[[255,0,0]]},
 {"type":"anim","mode":"add","id":"st:anim/a","frames":[{"png":"a.png"}]},
 {"type":"species","mode":"add","id":"st:species/clone","based_on":"base:species/0"},
 {"type":"species","mode":"add","id":"st:species/shown","based_on":"base:species/0","starter_kit":2,
  "starter":{"room":true,"colors":[500,"st:pal/red"],"rare_colors":[501],"requires":["seen_it","other:key"]}},
 {"type":"species","mode":"patch","id":"base:species/0","starter":{"room":false}}
])");
        ContentDB d3 = baseDb();
        d3.species("base:species/0");
        if (SpeciesDef *b0 = d3.speciesMutable("base:species/0"))
            b0->starterRoom = true;
        ModReport r3 = importMods(d3, root);
        for (const auto &e : r3.errors)
            std::cerr << e << "\n";
        CHECK(r3.errors.empty());
        const SpeciesDef *c = d3.species("st:species/clone"), *sh = d3.species("st:species/shown"), *b = d3.species("base:species/0");
        CHECK(c && !c->starterRoom && c->starterColors.empty() && c->kitSpecies == 0);
        CHECK(sh && sh->starterRoom && sh->kitSpecies == 2 && sh->starterColors.size() == 2 && sh->starterColors[0] == 500 && isModPalette(sh->starterColors[1]));
        CHECK(sh && sh->starterRareColors == std::vector<int>{501} && sh->starterRequires.size() == 2 && sh->starterRequires[1] == "other:key");
        CHECK(b && !b->starterRoom); // the patch took the base species out of the room
        fs::remove_all(root / "st");
    }

    { // a roaming pet npc of a mod's species: found by id once every mod is loaded, its number in speciesOrAnim
        writeFile(root / "np" / "manifest.json", R"({"id":"np","version":"1.0","game_api":"1"})");
        solidPng(root / "np" / "a.png");
        writeFile(root / "np" / "data" / "a.json", R"([
 {"type":"anim","mode":"add","id":"np:anim/a","frames":[{"png":"a.png"}]},
 {"type":"npc","mode":"add","id":"np:npc/pet","based_on":"base:npc/12","species":"np:species/s"},
 {"type":"species","mode":"add","id":"np:species/s","art":{"walkUp":"np:anim/a","walkDown":"np:anim/a","walkSide":"np:anim/a","idleBase":"np:anim/a"}}
])");
        ContentDB d4 = baseDb();
        NpcDef pet;
        pet.id = "base:npc/12";
        pet.area = 0;
        pet.speciesOrAnim = 1;
        pet.anim = "base:anim/139";
        d4.put(std::move(pet));
        ModReport r4 = importMods(d4, root);
        for (const auto &e : r4.errors)
            std::cerr << e << "\n";
        CHECK(r4.errors.empty());
        const NpcDef *n = d4.npc("np:npc/pet"), *basePet = d4.npc("base:npc/12");
        const SpeciesDef *sp = d4.species("np:species/s");
        CHECK(n && sp && n->speciesId == "np:species/s" && n->speciesOrAnim == sp->legacyIndex && sp->legacyIndex >= 3);
        CHECK(basePet && basePet->speciesOrAnim == 1 && basePet->speciesId.empty());
        fs::remove_all(root / "np");
    }

    // mistakes are reported and add nothing
    auto bad = [&](const std::string &rec, const std::string &want)
    {
        const size_t n = baseCount; // every load starts again from the base species
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
    { // an npc naming a species nobody defined, and a stationary one
        fs::remove_all(root / "bad");
        writeFile(root / "bad" / "manifest.json", R"({"id":"bad","version":"1.0","game_api":"1"})");
        writeFile(root / "bad" / "data" / "x.json", R"([{"type":"npc","mode":"add","id":"bad:npc/a","based_on":"base:npc/12","species":"bad:species/none"}])");
        ContentDB d5 = baseDb();
        NpcDef pet;
        pet.id = "base:npc/12";
        pet.anim = "base:anim/139";
        d5.put(std::move(pet));
        ModReport r5 = importMods(d5, root);
        CHECK(mentions(r5.errors, "unknown species 'bad:species/none'"));
        writeFile(root / "bad" / "data" / "x.json", R"([{"type":"npc","mode":"add","id":"bad:npc/a","based_on":"base:npc/12","stationary":true,"lines":["base:text/1","base:text/1","base:text/1"],"species":"base:species/0"}])");
        ContentDB d6 = baseDb();
        NpcDef pet2;
        pet2.id = "base:npc/12";
        pet2.anim = "base:anim/139";
        d6.put(std::move(pet2));
        CHECK(mentions(importMods(d6, root).errors, "roaming pets"));
    }

    CHECK(bad(head + R"("based_on":"base:species/0","starter":{"colors":["bad:nope"]}})", "starter 'colors'"));
    CHECK(bad(head + R"("based_on":"base:species/0","starter":{"zzz":1}})", "'starter'"));
    CHECK(bad(head + R"("based_on":"base:species/0","starter_kit":7})", "starter_kit"));

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
