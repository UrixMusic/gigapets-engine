// custom_instrument_test.cpp <rom> - a mod's instruments and drums: the sound engine plays their samples (with a loop, an envelope, zones),
// and the instrument / drum records load, number and report mistakes.
#include "../src/engine/audio/audio.hpp"
#include "../src/mods/mod_loader.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace gp;
namespace fs = std::filesystem;

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

static double energy(const std::vector<float> &f, int rate, double from, double to)
{
    double e = 0;
    for (size_t i = size_t(from * rate) * 2; i < std::min(size_t(to * rate) * 2, f.size()); ++i)
        e += double(f[i]) * f[i];
    return e;
}
static rau::RomAudio::CustomZone sine(int rate, double seconds, bool loop, double loopFrom, double loopTo)
{
    rau::RomAudio::CustomZone z;
    z.rate = uint32_t(rate);
    z.root = 69;
    for (int i = 0; i < int(seconds * rate); ++i)
        z.pcm.push_back(0.8f * float(std::sin(2 * 3.14159265358979 * 440.0 * i / rate)));
    z.loop = loop;
    z.loopStart = uint32_t(loopFrom * rate);
    z.loopEnd = uint32_t(loopTo * rate);
    return z;
}
static rau::Song oneNote(int program, int pb, int ch, int ticks)
{
    rau::Song s;
    rau::Event p;
    p.k = 'p';
    p.ch = ch;
    p.v = program;
    s.ev.push_back(p);
    rau::Event n;
    n.k = 'n';
    n.ch = ch;
    n.pb = pb;
    n.dur = ticks;
    n.vol = 127;
    n.vel = 127;
    n.pan = 64;
    s.ev.push_back(n);
    s.end = ticks + 100;
    return s;
}
static void writeFile(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
static void writeWav(const fs::path &p)
{
    std::vector<uint8_t> w = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 8, 0, 0, 0, 0, 0, 1, 0, 2, 0, 3, 0};
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(w.data()), std::streamsize(w.size()));
}
static void writeMidi(const fs::path &p)
{
    const std::vector<uint8_t> trk = {0x00, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 0, 0x00, 0xff, 0x2f, 0x00}; // one note on channel 1
    std::vector<uint8_t> w = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0, 'M', 'T', 'r', 'k', 0, 0, 0, uint8_t(trk.size())};
    w.insert(w.end(), trk.begin(), trk.end());
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(w.data()), std::streamsize(w.size()));
}
static bool mentions(const std::vector<std::string> &v, const std::string &s)
{
    for (const auto &e : v)
        if (e.find(s) != std::string::npos)
            return true;
    return false;
}

int main(int argc, char **argv)
{
    Rom rom(argc > 1 ? argv[1] : "rom.u7");
    const int rate = 22050;
    { // the sound engine
        rau::RomAudio a(rom);
        const size_t programs = a.programCount(), drums = a.drumCount();
        rau::RomAudio::CustomInstrument looped, once;
        looped.program = 14;
        looped.zones = {sine(rate, 0.3, true, 0.05, 0.25)};
        once.program = 15;
        once.zones = {sine(rate, 0.3, false, 0, 0)};
        rau::RomAudio::CustomDrum d0, d12;
        d0.index = 0;
        d0.zone = sine(rate, 0.1, false, 0, 0);
        d12.index = 12;
        d12.zone = sine(rate, 0.1, false, 0, 0);
        a.setCustom({looped, once}, {d0, d12});
        CHECK(a.programCount() == programs + 2);
        CHECK(a.drumCount() == drums + 1);
        // a note held for a second: the loop keeps it sounding, the single shot ends with its sample
        const std::vector<float> held = a.renderSong(oneNote(14, 69, 0, 200), rate), shot = a.renderSong(oneNote(15, 69, 0, 200), rate);
        CHECK(!held.empty() && !shot.empty());
        CHECK(energy(held, rate, 0.7, 0.9) > 10.0);
        CHECK(energy(shot, rate, 0.7, 0.9) < 1e-6 && energy(shot, rate, 0.05, 0.25) > 10.0);
        // the live player is the same sound
        {
            rau::SongPlayer p(a, oneNote(14, 69, 0, 200), rate);
            std::vector<float> live(held.size() * 2, 0.f);
            for (size_t at = 0; at < held.size() / 2 * 2; at += 500)
                p.render(live.data() + at * 2, std::min<size_t>(500, held.size() / 2 * 2 - at));
            CHECK(energy(live, rate, 0.7, 0.9) > 10.0);
        }
        // a higher note plays the sample faster: an octave up, the sample (0.3 s, no loop) is over after 0.15 s
        const std::vector<float> high = a.renderSong(oneNote(15, 81, 0, 200), rate);
        CHECK(energy(high, rate, 0.05, 0.14) > 10.0 && energy(high, rate, 0.2, 0.3) < 1e-6);
        // the replaced ROM drum 0 and the new drum 12 sound; the others are the ROM's
        CHECK(a.drum(0) && a.drum(0)->data && a.drum(12) && a.drum(12)->data && a.drum(1) && !a.drum(1)->data);
        const std::vector<float> kick = a.renderSong(oneNote(0, 0, 9, 100), rate), clap = a.renderSong(oneNote(0, 12, 9, 100), rate);
        CHECK(energy(kick, rate, 0.0, 0.1) > 10.0 && energy(clap, rate, 0.0, 0.1) > 10.0);
        // an envelope: the attack makes the start quieter, the release stops the note at its end
        rau::RomAudio::CustomInstrument slow = looped;
        slow.attack = 0.2;
        slow.release = 0.05;
        a.setCustom({slow}, {});
        const std::vector<float> faded = a.renderSong(oneNote(14, 69, 0, 100), rate);
        CHECK(energy(faded, rate, 0.0, 0.05) < 0.05 * energy(faded, rate, 0.3, 0.35));
        CHECK(energy(faded, rate, 0.8, 0.9) < 1e-6); // the note was half a second; it is over (release 50 ms)
        // zones: low notes use the first sample, high ones the second
        rau::RomAudio::CustomInstrument zoned;
        zoned.program = 14;
        zoned.zones = {sine(rate, 0.3, true, 0, 0), sine(rate, 0.3, true, 0, 0)};
        zoned.zones[0].top = 59;
        for (float &f : zoned.zones[1].pcm)
            f *= 0.25f;
        a.setCustom({zoned}, {});
        const std::vector<float> lo = a.renderSong(oneNote(14, 57, 0, 100), rate), hi = a.renderSong(oneNote(14, 81, 0, 100), rate);
        CHECK(energy(lo, rate, 0.1, 0.3) > 4 * energy(hi, rate, 0.1, 0.3));
        // nothing from an earlier call is kept
        a.setCustom({}, {});
        CHECK(a.programCount() == programs && a.drumCount() == drums && !a.drum(0)->data);
    }

    { // patchSong: a channel's instrument from the start, another instrument number for a program, another drum
        rau::Song in = oneNote(2, 69, 0, 100);
        rau::Event more = in.ev[1];
        more.ch = 9;
        more.pb = 0;
        in.ev.push_back(more);
        const rau::Song out = rau::patchSong(in, {{2, 7}}, {{0, 12}}, {});
        CHECK(out.ev[0].k == 'p' && out.ev[0].v == 7 && out.ev[2].pb == 12 && out.ev.size() == 3);
        const rau::Song by = rau::patchSong(in, {{2, 7}}, {}, {{0, 14}, {3, 1}});
        CHECK(by.ev.size() == 4 && by.ev[0].k == 'p' && by.ev[0].t == 0 && by.ev[0].ch == 0 && by.ev[0].v == 14 && by.ev[1].ch == 3 && by.ev[1].v == 1 && by.ev[2].k == 'n');
    }

    { // the records
        const fs::path root = fs::temp_directory_path() / "gp_instrec";
        fs::remove_all(root);
        writeFile(root / "ins" / "manifest.json", R"({"id":"ins","version":"1.0","game_api":"1"})");
        writeWav(root / "ins" / "audio" / "a.wav");
        writeMidi(root / "ins" / "audio" / "m.mid");
        writeFile(root / "ins" / "data" / "a.json", R"([
 {"type":"instrument","mode":"add","id":"ins:instrument/harp","file":"audio/a.wav","root":62,"loop_start":0.0001,"attack":0.01,"decay":0.2,"sustain":0.5,"release":0.3,"volume":50},
 {"type":"instrument","mode":"add","id":"ins:instrument/pad","zones":[{"file":"audio/a.wav","top":59},{"file":"audio/a.wav","root":72}]},
 {"type":"instrument","mode":"replace","id":"base:instrument/3","file":"audio/a.wav"},
 {"type":"instrument","mode":"replace","id":"ins:instrument/harp","release":0.5},
 {"type":"drum","mode":"add","id":"ins:drum/clap","file":"audio/a.wav","pitch":-2},
 {"type":"drum","mode":"replace","id":"base:drum/5","file":"audio/a.wav","volume":200},
 {"type":"music","mode":"add","id":"ins:music/song","file":"audio/m.mid","channels":{"1":14}},
 {"type":"music","mode":"replace","id":"base:music/4","channels":{"1":"ins:instrument/harp"},"programs":{"2":"base:instrument/5"},"drums":{"0":"ins:drum/clap"}},
 {"type":"music","mode":"add","id":"ins:music/ids","file":"audio/m.mid","programs":{"40":"base:instrument/3"},"drums":{"36":"ins:drum/clap"},"channels":{"1":"ins:instrument/harp","2":"ins:instrument/nope"}}
])");
        ContentDB db;
        const ModReport rep = importMods(db, root);
        for (const auto &e : rep.errors)
            std::cerr << e << "\n";
        CHECK(rep.errors.empty());
        const AudioRegistry &reg = audioRegistry();
        CHECK(reg.instruments.size() == 3 && reg.drums.size() == 2);
        CHECK(modInstrumentNumber("ins:instrument/harp") == 14 && modInstrumentNumber("ins:instrument/pad") == 15 && modInstrumentNumber("base:instrument/3") == 3 && modInstrumentNumber("zz") == -1);
        CHECK(modDrumNumber("ins:drum/clap") == 12 && modDrumNumber("base:drum/5") == 5);
        const ModInstrument &harp = reg.instruments[0];
        CHECK(harp.zones.size() == 1 && harp.zones[0].root == 62 && harp.zones[0].loop && harp.zones[0].gain == 0.5f && harp.attack == 0.01 && harp.sustain == 0.5 && harp.release == 0.5 && harp.decay == 0.2);
        CHECK(reg.instruments[1].zones.size() == 2 && reg.instruments[1].zones[0].top == 59 && reg.instruments[1].zones[1].root == 72 && !reg.instruments[1].zones[0].loop);
        CHECK(reg.drums[0].semitones == -2 && reg.drums[1].gain == 2.f);
        CHECK(mentions(rep.warnings, "ins:instrument/harp is instrument 14") && mentions(rep.warnings, "ins:drum/clap is drum 12"));

        { // instruments named by id: found once every mod is loaded; one that is not there is reported
            const ModSound &ids = reg.songs[1];
            CHECK(songPatchFor(4) && songPatchFor(4)->channelIds.at(0) == "ins:instrument/harp" && songPatchFor(4)->programIds.at(2) == "base:instrument/5" && songPatchFor(4)->drumIds.at(0) == "ins:drum/clap" && !songPatchFor(3) && !reg.songOver.count(4));
            CHECK(ids.programIds.at(40) == "base:instrument/3" && ids.channelIds.size() == 2 && ids.channels.empty());
            std::map<int, int> p, d, c;
            std::vector<std::string> unknown;
            resolveModSound(ids, p, d, c, &unknown);
            CHECK(p.at(40) == 3 && d.at(36) == 12 && c.at(0) == 14 && !c.count(1) && unknown.size() == 1);
            CHECK(mentions(rep.warnings, "no such instrument 'ins:instrument/nope'"));
            CHECK(instrumentIdNumber("base:instrument/13") == 13 && instrumentIdNumber("base:instrument/14") == -1 && drumIdNumber("base:drum/11") == 11 && drumIdNumber("ins:drum/clap") == 12);
            CHECK(instrumentIdTable().at("ins:instrument/pad") == 15 && instrumentIdTable().at("base:instrument/0") == 0);
            // a track named with an id in square brackets
            const std::string nm = "Lead [ins:instrument/harp]";
            std::vector<uint8_t> trk = {0x00, 0xff, 0x03, uint8_t(nm.size())};
            trk.insert(trk.end(), nm.begin(), nm.end());
            const std::vector<uint8_t> rest = {0x00, 0x90, 60, 100, 0x60, 0x80, 60, 0, 0x00, 0xff, 0x2f, 0x00};
            trk.insert(trk.end(), rest.begin(), rest.end());
            std::vector<uint8_t> w = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0, 'M', 'T', 'r', 'k', 0, 0, 0, uint8_t(trk.size())};
            w.insert(w.end(), trk.begin(), trk.end());
            midi::Options mo;
            mo.programCount = 16;
            mo.instrumentIds = instrumentIdTable();
            midi::Result mr;
            std::string why;
            CHECK(midi::toSong(w, mo, mr, why) && !mr.song.ev.empty() && mr.song.ev[0].k == 'p' && mr.song.ev[0].v == 14);
        }

        // the Audio class puts them in the sound engine: a song with a channel on the new instrument plays
        const int gen = reg.generation;
        Audio audio;
        audio.setRom(rom);
        { // the ROM's song 4 with channel 0 on the mod's instrument: the same notes, other sound
            const AudioClip *patched = audio.musicClipFor(4);
            CHECK(patched && patched->frames() > 100000);
            const rau::RomAudio plain(rom);
            const std::vector<float> orig = plain.music(4, Audio::kRate);
            CHECK(patched && patched->frames() == orig.size() / 2);
            double diff = 0;
            for (size_t i = 0; patched && i < orig.size() && i < patched->pcm.size(); i += 7)
                diff += std::fabs(double(patched->pcm[i]) / 32000.0 - orig[i]);
            CHECK(diff > 1.0);
        }
        CHECK(audio.musicClipFor(0) != nullptr); // asking makes it sync
        const AudioClip *song = audio.musicClipFor(kModMusicBase); // a MIDI song with its channel on the mod's instrument 14
        CHECK(song && song->frames() > 1000 && !mentions(rep.warnings, "not one of"));
        CHECK(gen == audioRegistry().generation);
        forgetModAudio();
        CHECK(audioRegistry().instruments.empty() && audioRegistry().generation != gen);

        auto bad = [&](const std::string &rec, const std::string &want) {
            writeFile(root / "bad" / "manifest.json", R"({"id":"bad","version":"1.0","game_api":"1"})");
            writeWav(root / "bad" / "a.wav");
            writeFile(root / "bad" / "data" / "x.json", "[" + rec + "]");
            ContentDB d2;
            const bool ok = mentions(importMods(d2, root).errors, want);
            if (!ok)
                std::cerr << "  wanted: " << want << " for " << rec << "\n";
            return ok;
        };
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x"})", "'file' is required"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"other:instrument/x","file":"a.wav"})", "own prefix"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"base:instrument/3","file":"a.wav"})", "replaced with mode 'replace'"));
        CHECK(bad(R"({"type":"instrument","mode":"replace","id":"base:instrument/14","file":"a.wav"})", "0 to 13"));
        CHECK(bad(R"({"type":"instrument","mode":"replace","id":"bad:instrument/none","file":"a.wav"})", "no such instrument"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x","file":"a.wav","zones":[{"file":"a.wav"}]})", "not both"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x","zones":[]})", "one to 32"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x","zones":[{"file":"a.wav","bad":1}]})", "unknown field"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x","file":"a.wav","root":200})", "root"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x","file":"a.wav","loop_start":2,"loop_end":1})", "'loop_start' must come before"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x","file":"a.wav","sustain":2})", "sustain"));
        CHECK(bad(R"({"type":"instrument","mode":"add","id":"bad:instrument/x","file":"a.wav","loop":true})", "unknown field"));
        CHECK(bad(R"({"type":"music","mode":"replace","id":"base:music/4","channels":{"1":3},"volume":50})", "needs a 'file'"));
        CHECK(bad(R"({"type":"music","mode":"replace","id":"base:music/4","channels":{"17":3}})", "'channels' must be an object"));
        CHECK(bad(R"({"type":"music","mode":"add","id":"bad:music/x","channels":{"1":3}})", "'file' is required"));
        CHECK(bad(R"({"type":"drum","mode":"add","id":"bad:drum/x"})", "'file' is required"));
        CHECK(bad(R"({"type":"drum","mode":"replace","id":"base:drum/12","file":"a.wav"})", "0 to 11"));
        CHECK(bad(R"({"type":"drum","mode":"add","id":"bad:drum/x","file":"a.wav","loop_start":1})", "unknown field"));
        CHECK(bad(R"({"type":"drum","mode":"add","id":"bad:drum/x","file":"a.wav","pitch":99})", "'pitch'"));
        fs::remove_all(root);
        forgetModAudio();
    }

    std::cerr << (g_fail ? std::to_string(g_fail) + " failed\n" : "custom_instrument_test: all passed\n");
    return g_fail ? 1 : 0;
}
