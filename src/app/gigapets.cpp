// gigapets.cpp - the whole game in one program: front end -> gameplay -> (F10) -> front end ...
// Usage: build gigapets rom.u7 [--slot N] [--area N] [--collision]
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
    if (argc < 2)
    {
        std::printf("usage: gigapets <rom> [--slot N] [--area N] [--collision]\n");
        return 1;
    }
    int slot = -1, area = 0; // area 0 = what `walk rom.u7 0` used
    bool collision = false;
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--collision"))
            collision = true;
        else if (!std::strcmp(argv[i], "--slot") && i + 1 < argc)
            slot = std::clamp(std::atoi(argv[++i]), 0, 2);
        else if (!std::strcmp(argv[i], "--area") && i + 1 < argc)
            area = std::atoi(argv[++i]);
    }

    try
    {
        gp::AppHost host(argv[1], "gigapets");
        bool skipBoot = false;
        for (;;)
        {
            if (slot < 0)
            {
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
