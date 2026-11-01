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
#include "walk.cpp"
#include "frontend_demo.cpp"

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

    try
    {
        gp::AppHost host(romPath.c_str(), "gigapets");
        {
            const gp::Settings s = gp::loadSettings(); // the options screen's volume and music switch
            gp::audio().open("audio");
            gp::audio().setRetro(s.retroAudio);
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