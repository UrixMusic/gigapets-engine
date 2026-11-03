// pet_colour_save.hpp - the pet's colour in the save when it is a mod's palette.
//
// A mod palette's number depends on load order, so the save keeps its id ("colourId", "appearanceId") next to the usual numbers.
// The numbers hold the species default meanwhile, so everything that reads only the numbers (the front end's slot list, a visit
// to another house) shows a valid colour; if the mod is gone the game starts with that default.
#pragma once
#include <functional>
#include <string>
#include "../content/records.hpp"
#include "save.hpp"

namespace gp
{
    // idOf(number) = the palette id of a mod palette's number ("" if none).
    inline void saveColourIds(SaveData &s, int baseColour, int appearance, int fallback, const std::function<Id(int)> &idOf)
    {
        const std::pair<const char *, int> keys[2] = {{"colour", baseColour}, {"appearance", appearance}};
        for (const auto &[key, value] : keys)
        {
            const std::string idKey = std::string(key) + "Id";
            const Id id = isModPalette(value) ? idOf(value) : Id();
            if (id.empty())
                s.kv.erase(idKey);
            else
            {
                s.kv[idKey] = id;
                s.setInt(key, fallback);
            }
        }
    }

    // numberOf(id) = the palette's number this session, -1 if the mod or palette is not there. The id is "colourId" / "appearanceId", or
    // (as a convenience for editing a save by hand) a palette id written straight into "colour=" / "appearance=". A palette that is gone
    // leaves the number the save holds, or removes a value that was an id, so the pet gets the species' own colour.
    inline void loadColourIds(SaveData &s, const std::function<int(const Id &)> &numberOf)
    {
        for (const char *key : {"colour", "appearance"})
        {
            Id id;
            const auto idIt = s.kv.find(std::string(key) + "Id");
            const auto it = s.kv.find(key);
            if (idIt != s.kv.end())
                id = idIt->second;
            else if (it != s.kv.end() && it->second.find(':') != std::string::npos)
                id = it->second; // a palette id typed where a number goes
            if (id.empty())
                continue;
            const int n = numberOf(id);
            if (n >= 0)
                s.setInt(key, n);
            else if (it != s.kv.end() && it->second == id)
                s.kv.erase(key); // an id that cannot be found is no number: back to the species' own colour
        }
    }
} // namespace gp
