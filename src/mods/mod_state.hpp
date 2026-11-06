// mod_state.hpp - what the player chose on the Mods screen: which mods are off and the order they load in. Kept in a small text file (mods.txt in the data folder):
//   order <id>   one line per mod, first = loads first
//   off <id>     a mod that is switched off
#pragma once
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "manifest.hpp"

namespace gp
{
    struct ModState
    {
        std::vector<std::string> order;
        std::set<std::string> off;
        bool isOff(const std::string &id) const { return off.count(id) != 0; }
    };

    inline ModState loadModState(const std::string &path)
    {
        ModState s;
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::istringstream is(line);
            std::string key, id;
            if (!(is >> key >> id) || !validModId(id))
                continue;
            if (key == "order")
                s.order.push_back(id);
            else if (key == "off")
                s.off.insert(id);
        }
        return s;
    }

    inline bool saveModState(const ModState &s, const std::string &path)
    {
        std::ofstream f(path, std::ios::trunc);
        if (!f)
            return false;
        for (const std::string &id : s.order)
            f << "order " << id << "\n";
        for (const std::string &id : s.off)
            f << "off " << id << "\n";
        return bool(f);
    }

    // Takes the mods that are off out of `mods`; a mod that requires one that is off goes too. `why` gets a line for each of those (not for the ones the player switched off).
    inline void applyModState(std::vector<ModManifest> &mods, const ModState &state, std::vector<std::string> *why = nullptr)
    {
        std::set<std::string> off = state.off;
        for (bool again = true; again;)
        {
            again = false;
            for (const ModManifest &m : mods)
            {
                if (off.count(m.id))
                    continue;
                for (const std::string &r : m.needs)
                    if (off.count(r))
                    {
                        off.insert(m.id);
                        if (why)
                            why->push_back("mod '" + m.id + "' is left out: it requires '" + r + "', which is off");
                        again = true;
                        break;
                    }
            }
        }
        mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const ModManifest &m)
                                  { return off.count(m.id) != 0; }),
                   mods.end());
    }

    // One row of the Mods screen
    struct ModEntry
    {
        ModManifest m;
        bool on = true;     // will load
        std::string note;   // why it will not (or a problem), "" if fine
    };

    // Every mod folder, in the order the Mods screen lists them: the player's order (mods not in it last, alphabetically). `on` says whether it loads with this state.
    inline std::vector<ModEntry> scanMods(const fs::path &modsDir, const ModState &state)
    {
        std::vector<ModEntry> all;
        if (!fs::exists(modsDir))
            return all;
        for (const auto &de : fs::directory_iterator(modsDir))
        {
            if (!de.is_directory() || !fs::exists(de.path() / "manifest.json"))
                continue;
            ModEntry e;
            std::string err;
            if (parseManifest(de.path(), e.m, err))
                all.push_back(std::move(e));
        }
        std::vector<ModManifest> ms;
        for (const ModEntry &e : all)
            ms.push_back(e.m);
        std::vector<std::string> errs;
        sortMods(ms, errs, &state.order); // a missing dependency leaves it unsorted: the list is then just the order tried
        std::vector<ModEntry> out;
        for (const ModManifest &m : ms)
            for (ModEntry &e : all)
                if (e.m.id == m.id)
                    out.push_back(e);
        if (out.size() != all.size()) // sortMods failed: fall back to alphabetical
            out = all;
        std::vector<ModManifest> alive;
        for (const ModEntry &e : out)
            alive.push_back(e.m);
        std::vector<std::string> why;
        applyModState(alive, state, &why);
        for (ModEntry &e : out)
        {
            bool live = false;
            for (const ModManifest &m : alive)
                live = live || m.id == e.m.id;
            e.on = live;
            if (!live && !state.isOff(e.m.id))
                for (const std::string &w : why)
                    if (w.find("'" + e.m.id + "'") == 4)
                        e.note = w.substr(w.find("it requires"));
        }
        return out;
    }
} // namespace gp
