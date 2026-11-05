// fe_strings.hpp - the words of the front end (title, intro, house and load screens, the Options screen) as text records, so a mod can reword them.
//
// Each is the text "base:str/fe/<key>" in the front end's content db (put there before the mods load); a mod changes one with a text record in mode "replace" (`id: "base:str/fe/back"`).
// feText(key) is what the screens draw: the record's text, or this table's default if there is none.
#pragma once
#include <string>
#include "../../content/db.hpp"

namespace gp::fe
{
    struct FeString
    {
        const char *key;
        const char *text;
    };
    inline const FeString kFeStrings[] = {
        {"intro_next", "Next you will:"}, {"intro_pet", "* Choose a pet"}, {"intro_name", "* Name it"}, {"intro_house", "* Pick a house"}, {"intro_continue", "Press Select to continue"},
        {"house_title", "Select your house."}, {"back", "Back"}, {"empty", "Empty"},
        {"opt_sfx", "SFX"}, {"opt_interpolate", "Interpolate"}, {"opt_scale", "Scale"}, {"opt_scale_mode", "Scale Mode"}, {"opt_window_mode", "Window Mode"}, {"opt_aspect", "Aspect Ratio"},
        {"opt_retro", "Retro Audio"}, {"opt_live", "Music Playback"}, {"opt_test", "Test Mode"}, {"key_prefix", "Key: "}, {"key_press", "Press a key"},
        {"on", "On"}, {"off", "Off"}, {"perfect", "Perfect"}, {"stretch", "Stretch"}, {"live", "Live"}, {"baked", "Baked"},
        {"windowed", "Windowed"}, {"fullscreen", "Fullscreen"}, {"borderless", "Borderless"},
    };

    // The db the screens read their words from (set when the front end starts; null = the defaults)
    inline const ContentDB *&feDb()
    {
        static const ContentDB *db = nullptr;
        return db;
    }
    // Puts the defaults into `db` as texts (before the mods are loaded, so they can replace them)
    inline void importFeStrings(ContentDB &db)
    {
        for (const FeString &s : kFeStrings)
            db.put(TextDef{std::string("base:str/fe/") + s.key, s.text});
    }
    inline std::string feText(const std::string &key)
    {
        if (key.empty())
            return {};
        std::string fallback = key;
        for (const FeString &s : kFeStrings)
            if (key == s.key)
                fallback = s.text;
        return feDb() ? feDb()->textOr("base:str/fe/" + key, fallback) : fallback;
    }
} // namespace gp::fe
