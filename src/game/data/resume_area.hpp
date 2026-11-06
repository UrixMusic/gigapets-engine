// resume_area.hpp - which area a save starts in. The original always starts at the pet's house and never saves a position, so
// nothing is written unless the area asks for it (AreaDef::resume, set by a mod with "resume": true; off by default).
// The area is saved by its id (mod area numbers are handed out at load and can change when mods change). At start the area must
// still exist and still ask for it; otherwise the game starts where it always did.
#pragma once
#include "save.hpp"
#include <string>

namespace gp
{
    struct ResumeSpot
    {
        std::string area; // an area id
        int x = 0, y = 0; // where the pet stood
    };

    // Writes the spot if the area asks to be resumed, otherwise removes it, so a save made anywhere else starts at the usual place.
    inline void saveResume(SaveData &s, const std::string &areaId, bool resume, int x, int y)
    {
        if (areaId.empty() || !resume)
        {
            s.kv.erase("resumeArea");
            s.kv.erase("resumeX");
            s.kv.erase("resumeY");
            return;
        }
        s.setStr("resumeArea", areaId);
        s.setInt("resumeX", x);
        s.setInt("resumeY", y);
    }

    inline bool loadResume(const SaveData &s, ResumeSpot &out)
    {
        const std::string a = s.getStr("resumeArea");
        if (a.empty())
            return false;
        out.area = a;
        out.x = s.getInt("resumeX", 0);
        out.y = s.getInt("resumeY", 0);
        return true;
    }
} // namespace gp
