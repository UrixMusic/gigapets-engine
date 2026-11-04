// instrument_export_test.cpp <rom> - `--export-instruments`: a WAV for each of the ROM's instruments and drums, and the list of their numbers.
#include "../src/engine/instrument_export.hpp"
#include <iostream>

using namespace gp;

static int g_fail = 0;
#define CHECK(c)                                                   \
    do                                                             \
    {                                                              \
        if (!(c))                                                  \
        {                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; \
            ++g_fail;                                              \
        }                                                          \
    } while (0)

int main(int argc, char **argv)
{
    Rom rom(argc > 1 ? argv[1] : "rom.u7");
    namespace fs = std::filesystem;
    const std::string dir = (fs::temp_directory_path() / "gp_instexport").string();
    fs::remove_all(dir);
    const std::string why = exportInstruments(rom, dir);
    CHECK(why.empty());
    int instruments = 0, drums = 0;
    for (int i = 0; i < 15; ++i)
    {
        char n[64];
        std::snprintf(n, sizeof n, "instrument_%02d.wav", i);
        PcmData p;
        std::string w;
        if (fs::exists(fs::path(dir) / n) && loadSoundFile((fs::path(dir) / n).string(), p, w))
        {
            ++instruments;
            int peak = 0;
            for (int16_t s : p.pcm)
                peak = std::max(peak, std::abs(int(s)));
            CHECK(p.rate == 44100 && p.channels == 2 && p.frames() > 44100 * 5 && peak > 500); // a scale of 17 notes, audible
        }
    }
    for (int i = 0; i < 12; ++i)
    {
        char n[64];
        std::snprintf(n, sizeof n, "drum_%02d.wav", i);
        PcmData p;
        std::string w;
        if (fs::exists(fs::path(dir) / n) && loadSoundFile((fs::path(dir) / n).string(), p, w))
        {
            ++drums;
            int peak = 0;
            for (int16_t s : p.pcm)
                peak = std::max(peak, std::abs(int(s)));
            CHECK(peak > 200);
        }
    }
    CHECK(instruments == 14 && drums == 12);
    for (int i = 0; i < 15; ++i)
    {
        char n[64];
        std::snprintf(n, sizeof n, "instrument_%02d.wav", i);
        CHECK((i < 14) == fs::exists(fs::path(dir) / n)); // 0..13; the 15th group is the drums
    }
    std::string text;
    {
        std::ifstream f(fs::path(dir) / "instruments.txt");
        text.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    } // closed before the folder is removed
    CHECK(text.find("instrument 0 = Vibraphone") != std::string::npos && text.find("instrument 13 = Trumpet") != std::string::npos && text.find("instrument 14 =") == std::string::npos && text.find("drum 11 = Low Agogo") != std::string::npos);
    CHECK(text.find("used by song") != std::string::npos && text.find("[3]") != std::string::npos);
    std::cout << text.substr(0, 700) << "...\n";
    fs::remove_all(dir);
    if (g_fail)
        return 1;
    std::cout << "instrument_export_test: all passed\n";
    return 0;
}
