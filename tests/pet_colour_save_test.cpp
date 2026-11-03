// pet_colour_save_test.cpp - the pet's colour when it is a mod palette: the save keeps the palette's id, the numbers hold the species
// default, and a load turns the id back into this session's number.
#include "../game/pet_colour_save.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c)                                             \
    do                                                       \
    {                                                        \
        if (!(c))                                            \
        {                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while (0)

// what saveGame does: savePlayer writes the numbers, then the ids are added
static void saveAll(gp::SaveData &s, int baseColour, int appearance, int fallback, const std::function<gp::Id(int)> &idOf)
{
    s.setInt("colour", baseColour);
    s.setInt("appearance", appearance);
    gp::saveColourIds(s, baseColour, appearance, fallback, idOf);
}

int main()
{
    using namespace gp;
    const int ember = kModPaletteBase + 3, other = kModPaletteBase + 9;
    auto idOf = [&](int n) -> Id { return n == ember ? "m:pal/ember" : n == other ? "m:pal/other" : Id(); };
    auto numberOf = [&](const Id &id) -> int { return id == "m:pal/ember" ? kModPaletteBase + 7 : id == "m:pal/other" ? -1 : -1; }; // a later session

    { // an ordinary colour: numbers only, and old id keys are removed
        SaveData s;
        s.kv["colourId"] = "m:pal/old";
        s.kv["appearanceId"] = "m:pal/old";
        saveAll(s, 0x1e5, 0x1e5, 0x1e1, idOf);
        CHECK(!s.has("colourId") && !s.has("appearanceId") && s.getInt("colour") == 0x1e5 && s.getInt("appearance") == 0x1e5);
    }
    { // a mod colour: the id is saved, the numbers hold the species default
        SaveData s;
        saveAll(s, ember, ember, 0x1e1, idOf);
        CHECK(s.getStr("colourId") == "m:pal/ember" && s.getStr("appearanceId") == "m:pal/ember");
        CHECK(s.getInt("colour") == 0x1e1 && s.getInt("appearance") == 0x1e1);
        // next session the palette has another number
        loadColourIds(s, numberOf);
        CHECK(s.getInt("colour") == kModPaletteBase + 7 && s.getInt("appearance") == kModPaletteBase + 7);
    }
    { // a potion over a mod base colour: only the base is a mod's; the shown colour is a normal number
        SaveData s;
        saveAll(s, ember, 0x206, 0x1e1, idOf);
        CHECK(s.getStr("colourId") == "m:pal/ember" && !s.has("appearanceId") && s.getInt("colour") == 0x1e1 && s.getInt("appearance") == 0x206);
        loadColourIds(s, numberOf);
        CHECK(s.getInt("colour") == kModPaletteBase + 7 && s.getInt("appearance") == 0x206);
    }
    { // the mod is gone: the default numbers stay
        SaveData s;
        saveAll(s, other, other, 0x1e1, idOf);
        loadColourIds(s, numberOf);
        CHECK(s.getInt("colour") == 0x1e1 && s.getInt("appearance") == 0x1e1);
    }
    { // a mod number nobody can name is saved as the default
        SaveData s;
        saveAll(s, kModPaletteBase + 99, kModPaletteBase + 99, 0x1e1, idOf);
        CHECK(!s.has("colourId") && s.getInt("colour") == kModPaletteBase + 99); // (cannot happen in the game: numbers come from the db)
    }
    { // a palette id typed straight into colour= / appearance= (editing a save by hand) works too, and a missing one means the species' colour
        SaveData s;
        s.kv["colour"] = "m:pal/ember";
        s.kv["appearance"] = "m:pal/ember";
        loadColourIds(s, numberOf);
        CHECK(s.getInt("colour") == kModPaletteBase + 7 && s.getInt("appearance") == kModPaletteBase + 7 && !s.has("colourId"));
        SaveData gone;
        gone.kv["colour"] = "m:pal/other"; // numberOf says -1
        gone.kv["appearance"] = "m:pal/other";
        loadColourIds(gone, numberOf);
        CHECK(!gone.has("colour") && !gone.has("appearance"));
        SaveData plain; // ordinary numbers are left alone, ids or not
        plain.setInt("colour", 504);
        plain.kv["appearanceId"] = "m:pal/other"; // an id that is gone keeps the number the save holds
        plain.setInt("appearance", 505);
        loadColourIds(plain, numberOf);
        CHECK(plain.getInt("colour") == 504 && plain.getInt("appearance") == 505);
    }
    { // getIntStrict: whole numbers only (an id read as a number used to give 0)
        SaveData s;
        s.kv["a"] = "12";
        s.kv["b"] = "-3";
        s.kv["c"] = "m:pal/x";
        s.kv["d"] = "";
        s.kv["e"] = "12x";
        CHECK(s.getIntStrict("a", -1) == 12 && s.getIntStrict("b", -1) == -3 && s.getIntStrict("c", -1) == -1 && s.getIntStrict("d", -1) == -1 &&
              s.getIntStrict("e", -1) == -1 && s.getIntStrict("none", 7) == 7);
    }
    { // peekSlot (the front end's slot list) reports the id of a mod colour, from colourId, from appearanceId or typed by hand
        dataDir() = (std::filesystem::temp_directory_path() / "gp_peekslot_").string();
        auto write = [&](int slot, const std::map<std::string, std::string> &kv)
        {
            SaveData sd;
            sd.kv = kv;
            saveToFile(slotPath(slot), sd);
        };
        write(0, {{"version", "3"}, {"name", "A"}, {"species", "1"}, {"colour", "504"}, {"appearance", "505"}});
        write(1, {{"version", "3"}, {"name", "B"}, {"colour", "479"}, {"appearance", "479"}, {"appearanceId", "m:pal/ember"}, {"colourId", "m:pal/ember"}});
        write(2, {{"version", "3"}, {"name", "C"}, {"colour", "m:pal/typed"}, {"appearance", "m:pal/typed"}});
        write(3, {{"version", "3"}, {"name", "D"}, {"colour", "479"}, {"colourId", "m:pal/basecolour"}, {"appearance", "505"}}); // a potion over a mod colour
        const SlotInfo a0 = peekSlot(0), a1 = peekSlot(1), a2 = peekSlot(2), a3 = peekSlot(3);
        CHECK(a0.full && a0.species == 1 && a0.colour == 504 && a0.appearance == 505 && a0.appearanceId.empty());
        CHECK(a1.appearance == 479 && a1.appearanceId == "m:pal/ember"); // the number is the species default, the id says what is worn
        CHECK(a2.appearance == -1 && a2.colour == -1 && a2.appearanceId == "m:pal/typed");
        CHECK(a3.appearance == 505 && a3.appearanceId.empty()); // wearing a potion colour: the number is what is drawn
        for (int i = 0; i < 4; ++i)
            std::remove(slotPath(i).c_str());
    }
    if (fails)
        std::printf("%d check(s) failed\n", fails);
    else
        std::printf("pet_colour_save_test: all passed\n");
    return fails ? 1 : 0;
}
