// manifest.hpp - mod manifests: parse, validate, dependency-sort.
#pragma once
#include <nlohmann/json.hpp> // put third_party/ on the include path
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gp
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    // Major version only decides compatibility. Major bumps = a handler or param key removed/changed;
    // minor bumps = something added.
    constexpr const char *kGameApi = "1";

    struct ModManifest
    {
        std::string id, version, gameApi;
        std::string name, description;      // "name" (what the Mods screen shows; the id if there is none) and "description"
        int dataVersion = 1;                // "data_version": the version of what the mod keeps in saves (save.set); when it goes up, Game.hooks():onMigrate(ctx) runs on an older save (ctx.from, ctx.to)
        bool hasName = false, hasDescription = false;
        int iconW = 0, iconH = 0;           // the size of icon.png (read from its header), 0 if there is none
        bool hasIcon = false;               // dir/icon.png exists (a 32x32 picture for the Mods screen)
        std::vector<std::string> needs;     // "requires" is a C++20 keyword
        std::vector<std::string> loadAfter; // soft ordering, ignored if that mod isn't installed
        bool trusted = false;               // "trusted": true: its Lua runs without the sandbox (io, os, require, load ...); the player is told at start
        fs::path dir;
    };

    inline bool validModId(const std::string &s)
    {
        if (s.empty() || s == "base")
            return false; // "base" is reserved for the ROM import
        for (char c : s)
            if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_'))
                return false;
        return true;
    }

    inline std::string apiMajor(const std::string &v) { return v.substr(0, v.find('.')); }

    inline bool readJsonFile(const fs::path &p, json &out, std::string &err)
    {
        std::ifstream f(p);
        if (!f)
        {
            err = p.string() + ": cannot open";
            return false;
        }
        try
        {
            out = json::parse(f);
        }
        catch (const json::exception &e)
        {
            err = p.string() + ": " + e.what();
            return false;
        }
        return true;
    }

    inline bool parseManifest(const fs::path &dir, ModManifest &m, std::string &err)
    {
        const fs::path file = dir / "manifest.json";
        json j;
        if (!readJsonFile(file, j, err))
            return false;
        try
        {
            m.dir = dir;
            m.id = j.at("id").get<std::string>();
            m.version = j.value("version", std::string("0"));
            m.gameApi = j.at("game_api").get<std::string>();
            m.needs = j.value("requires", std::vector<std::string>{});
            m.loadAfter = j.value("load_after", std::vector<std::string>{});
            m.trusted = j.value("trusted", false);
            m.dataVersion = std::max(1, j.value("data_version", 1));
            m.hasName = j.contains("name");
            m.hasDescription = j.contains("description");
            m.name = j.value("name", m.id);
            m.description = j.value("description", std::string());
            m.hasIcon = fs::exists(dir / "icon.png");
            if (m.hasIcon) // a PNG starts with 8 bytes of signature, then the IHDR chunk: width and height are the 4 bytes each at offsets 16 and 20
            {
                std::ifstream png(dir / "icon.png", std::ios::binary);
                unsigned char b[24] = {};
                png.read(reinterpret_cast<char *>(b), 24);
                if (png.gcount() == 24)
                {
                    m.iconW = int(b[16] << 24 | b[17] << 16 | b[18] << 8 | b[19]);
                    m.iconH = int(b[20] << 24 | b[21] << 16 | b[22] << 8 | b[23]);
                }
            }
        }
        catch (const json::exception &e)
        {
            err = file.string() + ": " + e.what();
            return false;
        }
        if (!validModId(m.id))
        {
            err = file.string() + ": bad mod id '" + m.id + "' (lowercase letters, digits, _; 'base' is reserved)";
            return false;
        }
        if (m.id != dir.filename().string())
        {
            err = file.string() + ": id '" + m.id + "' must match its folder name '" + dir.filename().string() + "'";
            return false;
        }
        if (apiMajor(m.gameApi) != apiMajor(kGameApi))
        {
            err = file.string() + ": game_api " + m.gameApi + " does not match engine game_api " + kGameApi;
            return false;
        }
        return true;
    }

    // Dependency order: every mod after what it requires / load_after. Ties break alphabetically so the order
    // is the same on every machine. Returns false (with errors) on a missing dependency or a cycle.
    // `order`: the player's order (Mods screen); a mod that comes earlier in it loads earlier when nothing else decides, the ones not in it follow alphabetically.
    inline bool sortMods(std::vector<ModManifest> &mods, std::vector<std::string> &errs, const std::vector<std::string> *order = nullptr)
    {
        std::map<std::string, size_t> rank;
        if (order)
            for (size_t i = 0; i < order->size(); ++i)
                rank.emplace((*order)[i], i);
        std::vector<std::string> keys; // the ids in the order they are tried
        for (const ModManifest &m : mods)
            keys.push_back(m.id);
        std::sort(keys.begin(), keys.end(), [&](const std::string &a, const std::string &b)
                  {
            const size_t ra = rank.count(a) ? rank[a] : size_t(-1), rb = rank.count(b) ? rank[b] : size_t(-1);
            return ra != rb ? ra < rb : a < b; });
        std::map<std::string, size_t> at;
        for (size_t i = 0; i < mods.size(); ++i)
            at[mods[i].id] = i;

        std::map<std::string, std::set<std::string>> deps; // id -> ids that must load first
        bool ok = true;
        for (const ModManifest &m : mods)
        {
            deps[m.id];
            for (const std::string &r : m.needs)
            {
                if (r == "base")
                    continue;
                if (!at.count(r))
                {
                    errs.push_back("mod '" + m.id + "' requires '" + r + "' which is not installed");
                    ok = false;
                }
                else
                    deps[m.id].insert(r);
            }
            for (const std::string &a : m.loadAfter)
                if (at.count(a))
                    deps[m.id].insert(a);
        }
        if (!ok)
            return false;

        std::vector<ModManifest> out;
        std::set<std::string> done;
        while (done.size() < mods.size())
        {
            std::string pick;
            for (const std::string &key : keys)
            {
                const auto &kv = *deps.find(key);
                if (done.count(kv.first))
                    continue;
                bool ready = true;
                for (const std::string &d : kv.second)
                    if (!done.count(d))
                    {
                        ready = false;
                        break;
                    }
                if (ready)
                {
                    pick = kv.first;
                    break;
                }
            }
            if (pick.empty())
            {
                std::string left;
                for (const auto &kv : deps)
                    if (!done.count(kv.first))
                        left += (left.empty() ? "" : ", ") + kv.first;
                errs.push_back("dependency cycle among mods: " + left);
                return false;
            }
            out.push_back(mods[at[pick]]);
            done.insert(pick);
        }
        mods = std::move(out);
        return true;
    }

} // namespace gp