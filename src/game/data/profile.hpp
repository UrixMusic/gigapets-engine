// profile.hpp - what the mods keep for the whole game, not one save slot (profile.get / profile.set in a script): data/profile.txt,
// "mod.<mod>.<key>=<typed text>" like the slots' own mod keys. The front end reads it to decide what a new game offers (a species
// whose record says starter.requires), so it needs no scripts running.
#pragma once
#include <map>
#include <string>
#include "save.hpp"
#include "../../content/records.hpp"

namespace gp
{

    inline std::string profilePath() { return dataDir() + "profile.txt"; }

    inline std::map<std::string, std::string> loadProfileFile()
    {
        SaveData s;
        loadFromFile(profilePath(), s);
        return s.kv;
    }
    inline void saveProfileFile(const std::map<std::string, std::string> &kv)
    {
        SaveData s;
        s.kv = kv;
        saveToFile(profilePath(), s);
    }

    // True when the profile holds a set value for this key: true, a number that is not 0, a string that is not empty.
    // `key` is "name" (a key of mod `ownMod`) or "mod:name" (another mod's).
    inline bool profileTruthy(const std::map<std::string, std::string> &kv, const std::string &ownMod, const std::string &key)
    {
        const size_t c = key.find(':');
        const std::string full = "mod." + (c == std::string::npos ? ownMod + "." + key : key.substr(0, c) + "." + key.substr(c + 1));
        const auto it = kv.find(full);
        if (it == kv.end() || it->second.empty())
            return false;
        const std::string &v = it->second;
        switch (v[0])
        {
        case 'b':
            return v.size() > 1 && v[1] == '1';
        case 'i':
        case 'n':
            return std::strtod(v.c_str() + 1, nullptr) != 0.0;
        case 's':
            return v.size() > 1;
        }
        return false;
    }

    // May a new game offer this species (SpeciesDef::starterRoom and starterRequires)? The mod is the one whose prefix the species id has.
    inline bool starterOffered(const SpeciesDef &sd, const std::map<std::string, std::string> &profile)
    {
        if (!sd.starterRoom)
            return false;
        const std::string mod = sd.id.substr(0, sd.id.find(':'));
        for (const std::string &k : sd.starterRequires)
            if (!profileTruthy(profile, mod, k))
                return false;
        return true;
    }

} // namespace gp
