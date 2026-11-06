// gigapets.cpp - the whole game in one program: front end -> gameplay -> (F10) -> front end ...
// Usage: build gigapets rom.u7 [--slot N] [--area N] [--collision] [--smooth]
//   --smooth  : optional sub-pixel camera / interpolated movement / smooth fades, in the front end and in gameplay
//               (F4 in the front end, F5 in gameplay toggles it live)
//   no --slot : start at the front end (logos, main menu, new/load)
//   --slot N  : skip the front end and go straight into that save slot (the old `walk` behaviour)
//
// Stage 0 "unity build": the two old programs are #included so build.bat keeps working unchanged
// (one .cpp in, one .exe out). GP_NO_MAIN turns off their standalone main()s.
#define GP_NO_MAIN
#ifndef GP_SPLIT // GP_SPLIT: walk.cpp and frontend_demo.cpp are compiled apart (see the makefile); otherwise one big translation unit
#include "walk.cpp"
#include "frontend_demo.cpp"
#else
#define GP_STB_IMPL // the one copy of the ogg decoder is compiled here
#include "app_includes.hpp"
extern bool smoothCam; // walk.cpp
#endif
#include "../mods/export_base.hpp"
#include "../mods/export_frames.hpp"
#include "../engine/audio/instrument_export.hpp"
#include "../engine/audio/midi_export.hpp"
#include "../engine/audio/midi.hpp"

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Packaged game: no arguments. Everything is looked up next to the exe: rom/ (the user's ROM), data/ (saves, settings),
    // mods/. With a ROM path on the command line it behaves as before (development).
    std::string romPath;
    if (argc >= 2)
        romPath = argv[1];
    else
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (char *base = SDL_GetBasePath())
        {
            fs::current_path(base, ec);
            SDL_free(base);
        }
        fs::create_directories("data", ec);
        gp::dataDir() = "data/";
        if (fs::is_directory("rom", ec))
            for (const auto &e : fs::directory_iterator("rom", ec))
            {
                std::string ext = e.path().extension().string();
                for (char &c : ext)
                    c = char(std::tolower((unsigned char)c));
                if (e.is_regular_file() && (ext == ".u7" || ext == ".bin" || ext == ".rom"))
                {
                    romPath = e.path().string();
                    break;
                }
            }
        if (romPath.empty())
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Gigapets",
                                     "ROM not found.\nPut your ROM file (rom.u7) in the \"rom\" folder next to the game and start it again.", nullptr);
            return 1;
        }
    }
    int slot = -1, area = 0; // area 0 = what `walk rom.u7 0` used
    bool collision = false;
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--collision"))
            collision = true;
        else if (!std::strcmp(argv[i], "--smooth"))
            smoothCam = true;
        else if (!std::strcmp(argv[i], "--slot") && i + 1 < argc)
            slot = std::clamp(std::atoi(argv[++i]), 0, 2);
        else if (!std::strcmp(argv[i], "--area") && i + 1 < argc)
            area = std::atoi(argv[++i]);
    }

    // `--export-music <folder>`: the ROM's songs as MIDI files (song_0.mid ...) and a list of where each plays, then quit
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--export-music"))
        {
            try
            {
                gp::Rom rom(romPath);
                const std::string why = gp::exportMusic(rom, argv[i + 1]);
                if (!why.empty())
                {
                    std::fprintf(stderr, "export failed: %s\n", why.c_str());
                    return 1;
                }
                std::printf("exported the songs to %s (see songs.txt)\n", argv[i + 1]);
                return 0;
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "export failed: %s\n", e.what());
                return 1;
            }
        }

    // `--export-instruments <folder>`: the ROM's instruments and drums as audition WAV files and a list of their numbers, then quit
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--export-instruments"))
        {
            try
            {
                gp::Rom rom(romPath);
                const std::string why = gp::exportInstruments(rom, argv[i + 1]);
                if (!why.empty())
                {
                    std::fprintf(stderr, "export failed: %s\n", why.c_str());
                    return 1;
                }
                std::printf("exported the instruments and drums to %s (see instruments.txt)\n", argv[i + 1]);
                return 0;
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "export failed: %s\n", e.what());
                return 1;
            }
        }

    // `--export-pet-frames <folder> [mod id]`: the pets' animation frames as PNGs plus matching anim records, to paint over and use in a mod; then quit
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--export-pet-frames"))
        {
            try
            {
                gp::Rom rom(romPath);
                gp::ContentDB db;
                gp::importRom(rom, db, 0);
                const std::string modId = i + 2 < argc && argv[i + 2][0] != '-' ? argv[i + 2] : "mymod";
                const std::string why = gp::exportPetFrames(rom, db, argv[i + 1], modId);
                if (!why.empty())
                {
                    std::fprintf(stderr, "export failed: %s\n", why.c_str());
                    return 1;
                }
                return 0;
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "export failed: %s\n", e.what());
                return 1;
            }
        }

    // `--validate-mods`: load the base game, every mod's data and scripts, print what is wrong (or "ok"), and quit with 1 if anything is; nothing is started or saved
    for (int i = 2; i < argc; ++i)
        if (!std::strcmp(argv[i], "--validate-mods"))
        {
            try
            {
                gp::Rom rom(romPath);
                gp::ContentDB db;
                gp::importRom(rom, db, 0);
                gp::fe::importFeStrings(db);
                gp::ModReport rep = gp::importMods(db, "mods");
                gp::ScriptHost scripts;
                if (rep.ok()) // scripts are checked against the content that did load
                    gp::checkModScripts(db, rep, scripts, "mods");
                for (const auto &w : rep.warnings)
                    std::printf("warning: %s\n", w.c_str());
                for (const auto &e : rep.errors)
                    std::printf("error: %s\n", e.c_str());
                for (const auto &id : rep.trusted)
                    std::printf("trusted: %s (its scripts run without the sandbox: io, os, require, load ...)\n", id.c_str());
                std::printf("%zu mod(s) loaded, %zu error(s), %zu warning(s)\n", rep.loaded.size(), rep.errors.size(), rep.warnings.size());
                return rep.ok() ? 0 : 1;
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "validate failed: %s\n", e.what());
                return 1;
            }
        }

    // `--export-base <folder>`: write the base game's areas, tiles and objects as JSON and PNG for editing tools, then quit
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--export-base"))
        {
            try
            {
                gp::Rom rom(romPath);
                gp::ContentDB db;
                gp::importRom(rom, db, 0);
                std::string why = gp::exportBase(db, argv[i + 1]);
                if (why.empty())
                    why = gp::exportPets(db, rom, argv[i + 1]);
                if (!why.empty())
                {
                    std::fprintf(stderr, "export failed: %s\n", why.c_str());
                    return 1;
                }
                std::printf("exported the base game to %s\n", argv[i + 1]);
                return 0;
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "export failed: %s\n", e.what());
                return 1;
            }
        }

    try
    {
        gp::AppHost host(romPath.c_str(), "gigapets");
        {
            const gp::Settings s = gp::loadSettings(); // the options screen's volume and music switch
            gp::audio().open("audio");
            gp::audio().setRetro(s.retroAudio);
            gp::audio().setLive(s.liveMusic);
            gp::applyInputSettings(s); // the controller and mouse (input_bridge.hpp)
            gp::inputBridge().openAll();
            gp::inputBridge().renderer = host.ctx.ren; // (for the mouse position scripts read)
            gp::audio().setRom(*host.ctx.rom);
            gp::audio().setVolume(s.volume);
            gp::audio().setMusicVolume(s.musicVolume);
            gp::audio().setSfxVolume(s.sfxVolume);
            if (s.interpolate)
                smoothCam = true; // (--smooth turns it on regardless)
            gp::keys().load(s.keys);
            gp::applyDisplay(host.ctx.win, host.ctx.ren, gp::DisplaySettings{s.scale, s.perfectScale, s.windowMode, s.aspect});
        }
        bool skipBoot = false;
        for (;;)
        {
            if (slot < 0)
            {
                gp::audio().playMusic(gp::rau::kMenuSong);
                slot = runFrontEnd(host.ctx, /*launchGame*/ true, skipBoot, -1);
                if (slot == kFrontReload) // the Mods screen changed the mods: the front end starts again with them
                {
                    slot = -1;
                    skipBoot = true;
                    continue;
                }
                if (slot < 0)
                    break; // window closed in the front end
            }
            const int r = runWalk(host.ctx, slot, area, collision);
            if (r != gp::kRunToMenu)
                return r == gp::kRunError ? 1 : 0;
            slot = -1; // back to the menu
            skipBoot = true;
        }
    }
    catch (const std::exception &e)
    {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}