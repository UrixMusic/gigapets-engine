// audio_names.hpp - readable names for the ROM's sounds and songs (no SDL, no ROM).
//
// The game asks for a sound or a song by number. A mod can say "base:sound/kaboing" instead of 84 and "base:music/ufo" instead of 8: both forms mean the
// same thing and either works wherever an id is accepted (sound.play, sound.music, a record that replaces a sound or a song, an area's `music`, a species'
// sounds). The sound names are the author's own, checked by ear; a number with no name (the mini pet's sounds, for now) is only known by its number.
#pragma once
#include <cstdlib>
#include <string>

namespace gp
{
    struct AudioName
    {
        int number;
        const char *name;
    };

    inline const AudioName *soundNames(int &count)
    {
        static const AudioName t[] = {
            {0, "sine"}, {1, "fart"}, {2, "bark1"}, {3, "bark2"}, {4, "bark3"}, {5, "bark4"}, {6, "snore"}, {7, "eat1"}, {8, "eat2"}, {9, "dig"},
            {10, "dog_angry"}, {11, "dog_no"}, {12, "dog_pant_long"}, {13, "dog_pant"}, {14, "dog_whine"}, {15, "gulp"}, {16, "dog_yawn"}, {17, "dog_yelp"},
            {18, "door_shut"}, {19, "door_open"}, {20, "kaching"}, {21, "chime1"}, {22, "hamster_chatter"}, {23, "hamster_yahoo"}, {24, "hamster_chirp1"},
            {25, "hamster_chirp2"}, {26, "helicopter"}, {27, "tomcat_meow"}, {28, "tomcat_purr"}, {29, "tomcat_meow_high"}, {30, "tomcat_meow_low"},
            {31, "tomcat_hiss1"}, {32, "tomcat_hiss2"}, {33, "tomcat_hiss3"}, {34, "tomcat_hiss4"}, {35, "monkey_angry"}, {36, "monkey_no"},
            {37, "monkey_squeal1"}, {38, "monkey_hoohoo"}, {39, "monkey_whine"}, {40, "monkey_squeal2"}, {41, "monkey_chatter1"}, {42, "monkey_chatter2"},
            {43, "monkey_yawn"}, {44, "monkey_yelp"}, {45, "click"}, {46, "boing"}, {47, "eat_low1"}, {48, "eat_low2"}, {49, "panda_angry"}, {50, "panda_no"},
            {51, "deep_pant1"}, {52, "deep_pant2"}, {53, "robot_short1"}, {54, "robot_short2"}, {55, "robot_short3"}, {56, "robot_powerdown"},
            {57, "robot_powerup"}, {58, "robot_long"}, {59, "panda_sad"}, {60, "panda_roar1"}, {61, "panda_roar2"}, {62, "panda_roar3"}, {63, "gulp2"},
            {64, "panda_groan"}, {65, "panda_yelp"}, {74, "pup_whine"}, {75, "pup_pant"}, {76, "pup_bark1"}, {77, "pup_bark2"}, {78, "chime2"},
            {82, "chime3"}, {83, "chime4"}, {89, "click_button"}, {90, "boom"}, {91, "kaboing"}, {92, "ufo_machine"}, {93, "water_bloop"}, {94, "zap"},
            {95, "zap_loop"},
        };
        count = int(sizeof t / sizeof t[0]);
        return t;
    }
    // The songs are named for where they play (RemapZoneIndexToAudioId): zone 0 Gigaville (and the menus) 4, 1 Downtown 6, 2 Vacation Island and 4 Mystery Island 7,
    // 3 the Beach 5, 5 Magma Mountain 3, 6 Mt. Gigalympia 1, 7 the Jungle 2, 8 the UFO 8, 9 the Castle 0.
    inline const AudioName *songNames(int &count)
    {
        static const AudioName t[] = {
            {0, "castle"}, {1, "gigalympia"}, {2, "jungle"}, {3, "magma_mountain"}, {4, "gigaville"}, {5, "beach"}, {6, "downtown"}, {7, "islands"}, {8, "ufo"},
        };
        count = int(sizeof t / sizeof t[0]);
        return t;
    }

    inline std::string soundNameOf(int n)
    {
        int c = 0;
        for (const AudioName *e = soundNames(c); c-- > 0; ++e)
            if (e->number == n)
                return e->name;
        return {};
    }
    inline std::string songNameOf(int n)
    {
        int c = 0;
        for (const AudioName *e = songNames(c); c-- > 0; ++e)
            if (e->number == n)
                return e->name;
        return {};
    }

    namespace audio_names_detail
    {
        // "<prefix><number or name>" -> the number, or -1. `prefix` is "base:sound/" or "base:music/"; a number must be plain digits.
        inline int resolve(const std::string &id, const char *prefix, const AudioName *(*names)(int &), int limit)
        {
            const std::string p = prefix;
            if (id.size() <= p.size() || id.compare(0, p.size(), p) != 0)
                return -1;
            const std::string rest = id.substr(p.size());
            if (rest.find_first_not_of("0123456789") == std::string::npos)
            {
                if (rest.size() > 4)
                    return -1;
                const int n = std::atoi(rest.c_str());
                return n < limit ? n : -1;
            }
            int c = 0;
            for (const AudioName *e = names(c); c-- > 0; ++e)
                if (rest == e->name)
                    return e->number;
            return -1;
        }
    } // namespace audio_names_detail

    // "base:sound/84" or "base:sound/kaboing" -> 84 (0..95), else -1. "base:music/8" or "base:music/ufo" -> 8 (0..63), else -1.
    inline int romSoundNumber(const std::string &id) { return audio_names_detail::resolve(id, "base:sound/", &soundNames, 96); }
    inline int romSongNumber(const std::string &id) { return audio_names_detail::resolve(id, "base:music/", &songNames, 64); }
} // namespace gp
