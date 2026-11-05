// export_frames.hpp - the pets' animation frames as PNG files, to paint over and use in a mod (--export-pet-frames <folder> [mod id]).
//
// For each base species (dog, monkey, panda) and each role (sit, eat, dig, ... see speciesRoles()), the frames of that animation are written as RGBA PNGs drawn
// with the species' own colours, and one `anims.json` holds the matching `anim` records (anchors and offsets as the game has them). Copy the folder into a mod
// as `assets/pet` and `anims.json` into its `data` folder: the records say `"palette": 479` (the colours the pictures are drawn with), so the game recolours any
// picture made from them with the pet's own colours (potions, washes, the colour machine). Edit a picture in any editor with those colours; a pixel that is not
// exactly one of them is matched to the nearest. (Palette entries that share a colour are told apart by a blue one or two steps higher: see referenceColors.)
#pragma once
#include "../content/db.hpp"
#include "../game/npc_anim_ids.hpp"
#include "../loaders/import.hpp"
#include "../loaders/png.hpp"
#include "manifest.hpp"
#include <filesystem>
#include <fstream>

namespace gp
{
    // Returns "" or what went wrong.
    inline std::string exportPetFrames(const Rom &rom, ContentDB &db, const std::filesystem::path &outDir, const std::string &modId)
    {
        namespace fs = std::filesystem;
        static const char *const kSpecies[3] = {"dog", "monkey", "panda"};
        json records = json::array();
        std::error_code ec;
        int pictures = 0;
        for (int sp = 0; sp < 3; ++sp)
        {
            const SpeciesArt &art = artOf(sp);
            const int pal = art.colorBase; // the species' own colours
            std::vector<std::pair<std::string, int>> roles;
            for (const auto &r : speciesRoles())
                roles.push_back({r.first, art.*(r.second)});
            for (int k = 0; k < 3; ++k)
                roles.push_back({"trick" + std::to_string(k + 1), art.trickMain[k]});
            for (const auto &[role, anim] : roles)
            {
                if (anim < 0)
                    continue;
                std::vector<DecodedSprite> sprites;
                std::vector<std::pair<int, int>> offsets;
                Palette palette;
                if (!decodeAnimIndexed(rom, anim, sprites, offsets) || !readPalette(rom, pal, palette))
                    continue;
                const std::vector<RefColor> ref = referenceColors(palette); // the colours that say which index a pixel is
                const std::string base = std::string("assets/pet/") + kSpecies[sp] + "/" + role + "/";
                fs::create_directories(outDir / "pet" / kSpecies[sp] / role, ec);
                json frames = json::array();
                for (size_t i = 0; i < sprites.size(); ++i)
                {
                    const DecodedSprite &d = sprites[i];
                    std::vector<uint8_t> rgba(d.idx.size() * 4, 0);
                    for (size_t p = 0; p < d.idx.size(); ++p)
                    {
                        const size_t v = d.idx[p];
                        if (v && v < ref.size() && ref[v].ok)
                        {
                            rgba[p * 4] = ref[v].r;
                            rgba[p * 4 + 1] = ref[v].g;
                            rgba[p * 4 + 2] = ref[v].b;
                            rgba[p * 4 + 3] = 255;
                        }
                    }
                    char name[16];
                    std::snprintf(name, sizeof name, "%02zu.png", i);
                    if (!writePng((outDir / "pet" / kSpecies[sp] / role / name).string(), d.w, d.h, rgba))
                        return "cannot write " + (outDir / "pet" / kSpecies[sp] / role / name).string();
                    frames.push_back({{"png", base + name}, {"anchorX", d.anchorX}, {"anchorY", d.anchorY}, {"dx", offsets[i].first}, {"dy", offsets[i].second}});
                    ++pictures;
                }
                records.push_back({{"type", "anim"}, {"mode", "add"}, {"id", modId + ":anim/" + kSpecies[sp] + "_" + role}, {"palette", pal}, {"frames", frames}});
            }
        }
        std::ofstream f(outDir / "anims.json", std::ios::binary);
        if (!f)
            return "cannot write " + (outDir / "anims.json").string();
        f << records.dump(1);
        std::ofstream r(outDir / "README.txt", std::ios::binary);
        r << "The pets' animation frames, drawn with each species' own colours.\n"
             "Copy the 'pet' folder into your mod as assets/pet and anims.json into its data folder (the records are named " << modId << ":anim/<species>_<role>).\n"
             "Paint over a picture in any editor, keeping its size and the colours it has; play it with player.play(\"" << modId << ":anim/dog_sit\").\n"
             "The records say \"palette\": the colours the pictures were drawn with. The game recolours them with the colours the pet wears.\n";
        std::printf("wrote %d pictures in %s\n", pictures, outDir.string().c_str());
        return {};
    }
} // namespace gp
