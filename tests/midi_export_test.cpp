// midi_export_test.cpp <rom> - the ROM's songs written as MIDI and read back (engine/midi_export.hpp + midi.hpp): the same notes come back.
#include "../src/engine/audio/midi.hpp"
#include "../src/engine/audio/midi_export.hpp"
#include <iostream>
#include <set>
#include <tuple>

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
    const rau::RomAudio audio(rom);
    CHECK(audio.songs().size() == 9);
    int totalNotes = 0, bends = 0;
    for (size_t si = 0; si < audio.songs().size(); ++si)
    {
        const rau::Song &orig = audio.songs()[si];
        const std::vector<uint8_t> bytes = midi::songToMidi(orig, audio, "ROM song " + std::to_string(si));
        midi::Options opt; // General MIDI: the default
        opt.programCount = int(audio.programCount()) - 1;
        opt.drumCount = int(audio.drumCount());
        midi::Result back;
        std::string why;
        const bool ok = midi::toSong(bytes, opt, back, why);
        if (!ok)
            std::cerr << "song " << si << ": " << why << "\n";
        CHECK(ok);
        if (!ok)
            continue;
        CHECK(back.warnings.empty());

        using Note = std::tuple<int, int, int, int, int, int, int>; // t, channel, pitch / drum, length, volume, velocity, pan
        std::multiset<Note> a, b;
        for (const rau::Event &e : orig.ev)
            if (e.k == 'n')
                a.insert({e.t, e.ch, e.pb, std::max(1, e.dur), e.vol, std::max(1, e.vel), e.pan});
            else if (e.k == 'b')
                ++bends;
        for (const rau::Event &e : back.song.ev)
            if (e.k == 'n')
                b.insert({e.t, e.ch, e.pb, e.dur, e.vol, e.vel, e.pan});
        totalNotes += int(a.size());
        CHECK(a.size() == b.size());
        CHECK(a == b); // every note: when, which channel, which note or drum, how long, and its volume, velocity and pan
        CHECK(back.song.end == std::max(1, orig.end));

        // the instrument each note plays: the program in force at its start, in the original and in the file read back
        auto programAt = [](const rau::Song &s, int ch, int t)
        {
            int p = 0;
            for (const rau::Event &e : s.ev)
                if (e.k == 'p' && e.ch == ch && e.t <= t)
                    p = e.v;
            return p;
        };
        int wrong = 0;
        for (const rau::Event &e : orig.ev)
            if (e.k == 'n' && e.ch != 9 && programAt(orig, e.ch, e.t) != programAt(back.song, e.ch, e.t))
                ++wrong;
        CHECK(wrong == 0);
        std::printf("song %zu: %zu notes, %.1f s, %zu bytes - read back the same\n", si, a.size(), orig.end / rau::kTicksPerSecond, bytes.size());

        // and it sounds the same: render both
        const std::vector<float> x = audio.renderSong(orig, 22050), y = audio.renderSong(back.song, 22050);
        CHECK(x.size() == y.size());
        double diff = 0, tot = 0;
        for (size_t i = 0; i < std::min(x.size(), y.size()); ++i)
        {
            diff += std::fabs(double(x[i]) - double(y[i]));
            tot += std::fabs(double(x[i]));
        }
        std::printf("   rendered difference %.4f of the signal\n", tot > 0 ? diff / tot : 0.0);
        CHECK(tot > 0 && diff / tot < 0.05); // bends are measured in cents and rounded, so allow a little
    }
    CHECK(totalNotes > 4000);
    CHECK(bends > 50); // song 5 has bends: they went through the cents conversion
    const std::string dir = (std::filesystem::temp_directory_path() / "gp_musicexport").string();
    std::filesystem::remove_all(dir);
    CHECK(exportMusic(rom, dir).empty());
    int files = 0;
    for (int i = 0; i < 9; ++i)
        files += std::filesystem::exists(std::filesystem::path(dir) / ("song_" + std::to_string(i) + ".mid"));
    CHECK(files == 9 && std::filesystem::exists(std::filesystem::path(dir) / "songs.txt"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (g_fail)
        return 1;
    std::cout << "midi_export_test: all passed\n";
    return 0;
}
