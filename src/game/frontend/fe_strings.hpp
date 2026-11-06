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
        {"opt_retro", "Retro Audio"}, {"opt_live", "Music Playback"}, {"opt_controller", "Controller"}, {"opt_mouse", "Mouse"}, {"opt_mouse_left", "Left Button"}, {"opt_mouse_right", "Right Button"}, {"opt_mouse_middle", "Middle Button"}, {"opt_mouse_wheel", "Mouse Wheel"},
        {"role_none", "Nothing"}, {"role_select", "Select"}, {"role_cancel", "Cancel"}, {"role_menu", "Menu"}, {"role_walk", "Walk"}, {"role_cancel_walk", "Cancel / Walk"}, {"opt_test", "Test Mode"}, {"key_prefix", "Key: "}, {"key_press", "Press a key"},
        {"on", "On"}, {"off", "Off"}, {"perfect", "Perfect"}, {"stretch", "Stretch"}, {"live", "Live"}, {"baked", "Baked"},
        {"mods", "Mods"}, {"mods_title", "Mods"}, {"mods_none", "No mods installed."}, {"mods_version", "Version "}, {"mods_on", "On"}, {"mods_off", "Off"}, {"mods_trusted", "Trusted: runs freely"},
        {"mods_help", "{select}: on/off  {menu}: order  {cancel}: back"}, {"mods_help_order", "{select}: pick up  {menu}: done  {cancel}: back"}, {"mods_help_held", "Up/Down: move  {select}: put down"},
        {"mods_note", "Leaving this screen applies the changes."},
        {"mods_native", "Native code: allowed"}, {"mods_native_ask", "Native code: needs your OK"}, {"mods_native_changed", "Native code changed: needs your OK again"},
        {"native_title", "Native code"}, {"native_warn", "This mod runs code that nothing checks. It can do anything you can do on this computer: read and change your files, your saves, anything. Only allow it if you trust where it came from."},
        {"native_no", "No, leave it"}, {"native_yes", "Allow it"}, {"native_help", "Left/Right: choose  {select}: confirm  {cancel}: back"},
        {"mods_safe", "The last game ended badly: native mods stay off for the next one."}, {"mods_problem", " problem:"}, {"mods_problems", " problems (the first):"},
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
