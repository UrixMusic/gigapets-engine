// midi_render_test.cpp <rom> - a MIDI file played with the ROM's instruments: through the Audio class, like a mod's `music` record with a .mid file.
#include "../src/engine/audio.hpp"
#include <filesystem>
#include <fstream>
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

using Bytes = std::vector<uint8_t>;
static void writeMidi(const std::string &path, const Bytes &trk)
{
    Bytes w = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0, 'M', 'T', 'r', 'k', 0, 0, 0, uint8_t(trk.size())};
    w.insert(w.end(), trk.begin(), trk.end());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(w.data()), std::streamsize(w.size()));
}
static double energy(const AudioClip &c, size_t from, size_t to)
{
    double e = 0;
    for (size_t i = from * 2; i < std::min(to * 2, c.pcm.size()); ++i)
        e += double(c.pcm[i]) * double(c.pcm[i]);
    return e;
}

int main(int argc, char **argv)
{
    Rom rom(argc > 1 ? argv[1] : "rom.u7");
    const std::string dir = (std::filesystem::temp_directory_path() / "gp_midirender_").string();
    // instrument 1 plays middle C for a quarter note (half a second at the default tempo) at once with a kick drum; the file is one second long
    //            delta  event
    const Bytes plain = {0x00, 0xc0, 1, 0x00, 0x90, 60, 110, 0x00, 0x99, 1, 110, 0x83, 0x60, 0x80, 60, 0, 0x00, 0x89, 1, 0, 0x83, 0x60, 0xff, 0x2f, 0x00};
    // the same with the note bent up an octave a little way in (range 12 semitones)
    const Bytes bent = {0x00, 0xc0, 1, 0x00, 0xb0, 101, 0, 0x00, 0xb0, 100, 0, 0x00, 0xb0, 6, 12, 0x00, 0x90, 60, 110, 0x00, 0x99, 1, 110,
                        0x60, 0xe0, 0x7f, 0x7f, 0x83, 0x00, 0x80, 60, 0, 0x00, 0x89, 1, 0, 0x83, 0x60, 0xff, 0x2f, 0x00};
    // FL Studio's slide (CtoE.mid): a C that rises to an E over one measure at 140 bpm, written as two notes that start and end together
    const Bytes slide = {0x00, 0xc0, 1, 0x00, 0x90, 76, 100, 0x00, 0x90, 72, 100, 0x83, 0x00, 0x80, 76, 0, 0x00, 0x80, 72, 0, 0x00, 0xff, 0x2f, 0x00};
    writeMidi(dir + "slide.mid", slide);
    writeMidi(dir + "plain.mid", plain);
    writeMidi(dir + "bent.mid", bent);

    Audio a; // not opened: nothing plays, but the clips are made
    a.setRom(rom);
    AudioRegistry &reg = audioRegistry();
    reg.clear();
    auto addSong = [&](const char *id, const std::string &path, double semitones = 0, double loopStart = 0, double loopEnd = 0) -> ModSound &
    {
        ModSound m;
        m.id = id;
        m.path = path;
        m.semitones = semitones;
        m.loopStart = loopStart;
        m.loopEnd = loopEnd;
        m.midi = true;
        m.gmNumbers = false; // these files use the ROM's numbers
        reg.songs.push_back(m);
        return reg.songs.back();
    };
    addSong("t:music/plain", dir + "plain.mid");
    addSong("t:music/bent", dir + "bent.mid");
    {
        ModSound &o = addSong("t:music/other", dir + "plain.mid"); // another instrument and drum
        o.programs[1] = 5;
        o.drums[1] = 2;
    }
    addSong("t:music/faster", dir + "plain.mid", 12, 0.2, 0.6);
    addSong("t:music/chord", dir + "slide.mid");
    addSong("t:music/slide", dir + "slide.mid").slideChannels = {0};
    const AudioClip *chord = a.musicClipFor(kModMusicBase + 4), *glide = a.musicClipFor(kModMusicBase + 5);
    CHECK(chord && glide && chord->frames() == glide->frames() && chord->pcm != glide->pcm); // the slide is not the chord
    const AudioClip *p = a.musicClipFor(kModMusicBase), *b = a.musicClipFor(kModMusicBase + 1), *o = a.musicClipFor(kModMusicBase + 2), *f = a.musicClipFor(kModMusicBase + 3);
    CHECK(p && b && o && f);
    if (p && b && o && f)
    {
        const size_t n = p->frames();
        CHECK(n > 40000 && n < 50000); // one second (the tail is folded back over the start, so the clip is as long as the song)
        CHECK(energy(*p, 0, n) > 1e9);
        CHECK(energy(*p, 0, 4000) > 1e8); // it starts at once
        CHECK(b->frames() == n);
        CHECK(p->pcm != b->pcm);  // the bend changed the sound
        CHECK(p->pcm != o->pcm);  // so did the other instrument
        CHECK(f->frames() < n * 6 / 10 && f->loopEnd > 0 && f->loopStart > 0 && f->loopEnd <= f->frames()); // an octave up: half as long, the loop points carried over
        std::cout << "one second song: " << n << " frames, energy " << energy(*p, 0, n) << "\n";
    }
    // without the ROM there are no instruments
    {
        Audio none;
        CHECK(none.musicClipFor(kModMusicBase) == nullptr);
    }
    for (const char *n : {"plain.mid", "bent.mid", "slide.mid"})
        std::filesystem::remove(dir + n);
    if (g_fail)
        return 1;
    std::cout << "midi_render_test: all passed\n";
    return 0;
}
