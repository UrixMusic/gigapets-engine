#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <set>

namespace gp
{
    // Flat key=value store. Unknown keys are ignored and missing keys fall back to defaults,
    // so adding a field never breaks an old save.
    struct SaveData
    {
        std::map<std::string, std::string> kv;

        bool has(const std::string &k) const { return kv.count(k) != 0; }

        void setInt(const std::string &k, int v) { kv[k] = std::to_string(v); }
        int getInt(const std::string &k, int def = 0) const
        {
            auto it = kv.find(k);
            return it == kv.end() ? def : std::atoi(it->second.c_str());
        }
        void setUInt(const std::string &k, uint32_t v) { kv[k] = std::to_string(v); }
        uint32_t getUInt(const std::string &k, uint32_t def = 0) const
        {
            auto it = kv.find(k);
            return it == kv.end() ? def : uint32_t(std::strtoul(it->second.c_str(), nullptr, 10));
        }
        void setStr(const std::string &k, const std::string &v) { kv[k] = v; }
        std::string getStr(const std::string &k, const std::string &def = "") const
        {
            auto it = kv.find(k);
            return it == kv.end() ? def : it->second;
        }

        // comma-separated ints: personality, care stats, friends, tricks
        void setList(const std::string &k, const std::vector<int> &v)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i)
                s += (i ? "," : "") + std::to_string(v[i]);
            kv[k] = s;
        }
        std::vector<int> getList(const std::string &k) const
        {
            std::vector<int> out;
            auto it = kv.find(k);
            if (it == kv.end())
                return out;
            std::istringstream is(it->second);
            std::string part;
            while (std::getline(is, part, ','))
                if (!part.empty())
                    out.push_back(std::atoi(part.c_str()));
            return out;
        }

        // item:count pairs: inventory
        void setMap(const std::string &k, const std::map<int, int> &m)
        {
            std::string s;
            for (const auto &[a, b] : m)
                s += (s.empty() ? "" : ",") + std::to_string(a) + ":" + std::to_string(b);
            kv[k] = s;
        }
        std::map<int, int> getMap(const std::string &k) const
        {
            std::map<int, int> out;
            auto it = kv.find(k);
            if (it == kv.end())
                return out;
            std::istringstream is(it->second);
            std::string part;
            while (std::getline(is, part, ','))
            {
                int a = 0, b = 0;
                if (std::sscanf(part.c_str(), "%d:%d", &a, &b) == 2)
                    out[a] = b;
            }
            return out;
        }
        // id -> count. The count follows the LAST ':' because ids contain ':'.
        void setIdMap(const std::string &k, const std::map<std::string, int> &m)
        {
            std::string s;
            for (const auto &[id, n] : m)
                s += (s.empty() ? "" : ",") + id + ":" + std::to_string(n);
            kv[k] = s;
        }
        std::map<std::string, int> getIdMap(const std::string &k) const
        {
            std::map<std::string, int> out;
            auto it = kv.find(k);
            if (it == kv.end())
                return out;
            std::istringstream is(it->second);
            std::string part;
            while (std::getline(is, part, ','))
            {
                const size_t c = part.rfind(':');
                if (c != std::string::npos && c > 0)
                    out[part.substr(0, c)] = std::atoi(part.c_str() + c + 1);
            }
            return out;
        }

        // a set of ids, comma-separated
        void setIdList(const std::string &k, const std::set<std::string> &v)
        {
            std::string s;
            for (const auto &id : v)
                s += (s.empty() ? "" : ",") + id;
            kv[k] = s;
        }
        std::set<std::string> getIdList(const std::string &k) const
        {
            std::set<std::string> out;
            auto it = kv.find(k);
            if (it == kv.end())
                return out;
            std::istringstream is(it->second);
            std::string part;
            while (std::getline(is, part, ','))
                if (!part.empty())
                    out.insert(part);
            return out;
        }
    };

    inline bool saveToFile(const std::string &path, const SaveData &s)
    {
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f)
                return false;
            for (const auto &[k, val] : s.kv)
            {
                std::string v = val;
                for (char &c : v)
                    if (c == '\n' || c == '\r')
                        c = ' ';
                f << k << '=' << v << '\n';
            }
            if (!f)
                return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec)
        {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(tmp, path, ec);
        }
        return !ec;
    }

    inline bool loadFromFile(const std::string &path, SaveData &s)
    {
        std::ifstream f(path);
        if (!f)
            return false;
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const size_t eq = line.find('=');
            if (eq != std::string::npos)
                s.kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return true;
    }

    inline std::string slotPath(int slot) { return "save" + std::to_string(slot) + ".txt"; }
    inline bool slotValid(int slot)
    {
        SaveData s;
        return loadFromFile(slotPath(slot), s) && s.has("version");
    }

    struct SlotInfo
    {
        bool full = false;
        std::string name;
        int species = 0, colour = -1, appearance = -1;
    };

    inline SlotInfo peekSlot(int slot)
    {
        SaveData s;
        SlotInfo i;
        if (!loadFromFile(slotPath(slot), s) || !s.has("version"))
            return i;
        i.full = true;
        i.name = s.getStr("name");
        i.species = s.getInt("species");
        i.colour = s.getInt("colour", -1);
        i.appearance = s.getInt("appearance", i.colour);
        return i;
    }
}