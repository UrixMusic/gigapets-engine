// mod_list.hpp - the mods a save was made with, kept in its header as `mods=a@1.0,b@2.1` (the loaded mods in load order, with their versions), so loading it
// with other mods installed can say so. It only warns: a save still loads (what a missing mod added is handled by each record type's own rules).
#pragma once
#include <map>
#include <string>
#include <vector>

namespace gp
{
    // "a@1.0,b@2.1" for the mods in `loaded` (ids in load order), with their versions.
    inline std::string modListString(const std::vector<std::string> &loaded, const std::map<std::string, std::string> &versions)
    {
        std::string out;
        for (const std::string &id : loaded)
        {
            if (!out.empty())
                out += ',';
            const auto v = versions.find(id);
            out += id + '@' + (v == versions.end() ? "0" : v->second);
        }
        return out;
    }

    // What differs between the list a save has (`saved`, "" if the save has none) and the mods loaded now: one message per difference, empty if they match.
    inline std::vector<std::string> modListChanges(const std::string &saved, const std::string &now)
    {
        const auto parse = [](const std::string &s)
        {
            std::vector<std::pair<std::string, std::string>> out;
            size_t i = 0;
            while (i <= s.size() && !s.empty())
            {
                size_t j = s.find(',', i);
                if (j == std::string::npos)
                    j = s.size();
                const std::string item = s.substr(i, j - i);
                const size_t at = item.rfind('@');
                if (!item.empty())
                    out.push_back(at == std::string::npos ? std::make_pair(item, std::string()) : std::make_pair(item.substr(0, at), item.substr(at + 1)));
                i = j + 1;
            }
            return out;
        };
        std::vector<std::string> msgs;
        if (saved.empty() && now.empty())
            return msgs;
        if (saved == now)
            return msgs;
        const auto a = parse(saved), b = parse(now);
        std::map<std::string, std::string> was, is;
        for (const auto &p : a)
            was[p.first] = p.second;
        for (const auto &p : b)
            is[p.first] = p.second;
        for (const auto &p : a)
            if (!is.count(p.first))
                msgs.push_back("mod '" + p.first + "' (" + p.second + ") was used when this was saved, but is not loaded now");
            else if (is[p.first] != p.second)
                msgs.push_back("mod '" + p.first + "' was version " + p.second + " when this was saved, and is " + is[p.first] + " now");
        for (const auto &p : b)
            if (!was.count(p.first))
                msgs.push_back("mod '" + p.first + "' (" + p.second + ") was not loaded when this was saved");
        if (msgs.empty()) // the same mods in another order
            msgs.push_back("the mods load in a different order than when this was saved");
        return msgs;
    }
} // namespace gp
