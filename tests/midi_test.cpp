// midi_test.cpp - a MIDI file as a song for the ROM's player (engine/midi.hpp): timing, tempo changes, notes, programs, drums, controllers,
// pitch bend, sustain, and the files it refuses.
#include "../src/engine/audio/midi.hpp"
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
static void vlq(Bytes &b, uint32_t v)
{
    uint8_t t[4];
    int n = 0;
    t[n++] = v & 0x7f;
    while (v >>= 7)
        t[n++] = uint8_t(0x80 | (v & 0x7f));
    while (n)
        b.push_back(t[--n]);
}
struct Track
{
    Bytes b;
    Track &ev(uint32_t delta, std::initializer_list<int> data)
    {
        vlq(b, delta);
        for (int d : data)
            b.push_back(uint8_t(d));
        return *this;
    }
    Track &tempo(uint32_t delta, uint32_t us) { return ev(delta, {0xff, 0x51, 3, int(us >> 16), int((us >> 8) & 255), int(us & 255)}); }
    Track &end(uint32_t delta = 0) { return ev(delta, {0xff, 0x2f, 0}); }
};
static Bytes file(int format, int division, const std::vector<Track> &tracks)
{
    Bytes f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, uint8_t(format), 0, uint8_t(tracks.size()), uint8_t(division >> 8), uint8_t(division & 255)};
    for (const Track &t : tracks)
    {
        f.insert(f.end(), {'M', 'T', 'r', 'k'});
        const uint32_t n = uint32_t(t.b.size());
        f.insert(f.end(), {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)});
        f.insert(f.end(), t.b.begin(), t.b.end());
    }
    return f;
}
static const rau::Event *nth(const rau::Song &s, char kind, int n)
{
    for (const auto &e : s.ev)
        if (e.k == kind && n-- == 0)
            return &e;
    return nullptr;
}

int main()
{
    std::string why;
    midi::Options opt;
    opt.gmPrograms = opt.gmDrums = false; // these tests use the ROM's own numbers; the General MIDI ones are tested below
    {   // timing: 480 ticks to the quarter at 120 bpm = half a second a quarter; the player counts 200 ticks a second
        midi::Result r;
        Track t;
        t.ev(0, {0x90, 60, 100}).ev(480, {0x80, 60, 0}).ev(480, {0x90, 62, 80}).ev(240, {0x80, 62, 0}).end(240);
        CHECK(midi::toSong(file(0, 480, {t}), opt, r, why));
        CHECK(r.notes == 2 && r.song.ev.size() == 2);
        const rau::Event *a = nth(r.song, 'n', 0), *b = nth(r.song, 'n', 1);
        CHECK(a && b && a->t == 0 && a->dur == 100 && a->pb == 60 && a->vel == 100 && a->ch == 0);
        CHECK(b && b->t == 200 && b->dur == 50 && b->pb == 62 && b->vel == 80);
        CHECK(a && a->vol == 127 && a->pan == 64); // the defaults
        CHECK(r.song.end == 300 && std::abs(r.seconds - 1.5) < 1e-9); // the track's end counts
    }
    {   // tempo changes, in the second track of a format 1 file; a note on with velocity 0 is a note off; running status
        midi::Result r;
        Track conductor, notes;
        conductor.tempo(0, 500000).tempo(480, 250000).end(960);
        notes.ev(0, {0x90, 60, 90}).ev(480, {60, 0}) // running status: 0x90 again, velocity 0
            .ev(0, {0x90, 64, 90}).ev(480, {0x80, 64, 0}).end();
        CHECK(midi::toSong(file(1, 480, {conductor, notes}), opt, r, why));
        const rau::Event *a = nth(r.song, 'n', 0), *b = nth(r.song, 'n', 1);
        CHECK(a && b && a->dur == 100 && b->t == 100 && b->dur == 50); // after the first quarter the quarter is a quarter of a second
        CHECK(r.song.end >= 150);
    }
    {   // programs, volume, expression, pan, and the instruments the ROM does not have
        midi::Result r;
        Track t;
        t.ev(0, {0xc0, 3}).ev(0, {0xb0, 7, 100}).ev(0, {0xb0, 11, 64}).ev(0, {0xb0, 10, 20}).ev(0, {0x90, 60, 127}).ev(100, {0x80, 60, 0})
            .ev(0, {0xc1, 40}).ev(0, {0x91, 61, 100}).ev(100, {0x81, 61, 0}).ev(0, {0xc1, 40}).end();
        CHECK(midi::toSong(file(0, 480, {t}), opt, r, why));
        const rau::Event *p0 = nth(r.song, 'p', 0), *p1 = nth(r.song, 'p', 1), *n0 = nth(r.song, 'n', 0);
        CHECK(p0 && p0->ch == 0 && p0->v == 3 && p1 && p1->ch == 1 && p1->v == 0); // 40 is not one of the 15: instrument 0
        CHECK(n0 && n0->vol == 100 * 64 / 127 && n0->pan == 20);
        CHECK(r.warnings.size() == 1 && r.warnings[0].find("program 40") != std::string::npos); // said once
        midi::Options mapped;
        mapped.gmPrograms = false;
        mapped.programs[40] = 9;
        midi::Result r2;
        CHECK(midi::toSong(file(0, 480, {t}), mapped, r2, why) && nth(r2.song, 'p', 1)->v == 9 && r2.warnings.empty());
    }
    {   // drums: channel 10, the note is the ROM's drum number unless mapped; others are left out with a warning
        midi::Result r;
        Track t;
        t.ev(0, {0x99, 2, 100}).ev(10, {0x89, 2, 0}).ev(0, {0x99, 36, 100}).ev(10, {0x89, 36, 0}).ev(0, {0x99, 38, 100}).ev(10, {0x89, 38, 0}).end();
        midi::Options o;
        o.gmDrums = false;
        o.drums[36] = 5;
        CHECK(midi::toSong(file(0, 480, {t}), o, r, why));
        CHECK(r.notes == 2 && nth(r.song, 'n', 0)->pb == 2 && nth(r.song, 'n', 0)->ch == 9 && nth(r.song, 'n', 1)->pb == 5);
        CHECK(r.warnings.size() == 1 && r.warnings[0].find("drum note 38") != std::string::npos);
    }
    {   // pitch bend: a bend event in cents; a note that starts while bent starts with the bend; the range can be changed
        midi::Result r;
        Track t;
        t.ev(0, {0x90, 60, 100}).ev(100, {0xe0, 0x7f, 0x7f}) // full up
            .ev(100, {0xe0, 0, 0x40}) // centre again
            .ev(100, {0x80, 60, 0}).ev(0, {0xe0, 0, 0x60}) // a quarter up while nothing plays
            .ev(10, {0x90, 62, 100}).ev(100, {0x80, 62, 0})
            .ev(0, {0xb0, 101, 0}).ev(0, {0xb0, 100, 0}).ev(0, {0xb0, 6, 12}).ev(0, {0xe0, 0, 0x7f}).end();
        CHECK(midi::toSong(file(0, 480, {t}), opt, r, why));
        const rau::Event *b0 = nth(r.song, 'B', 0), *b1 = nth(r.song, 'B', 1), *b2 = nth(r.song, 'B', 2), *b3 = nth(r.song, 'B', 3);
        CHECK(b0 && std::abs(b0->v - 200) <= 1 && b0->t == 21); // 100 ticks at 960 a second = 0.104 s = 21 of the player's ticks
        CHECK(b1 && b1->v == 0);
        CHECK(b2 && std::abs(b2->v - 100) <= 1);                                // the quarter up held before the note...
        CHECK(b3 && b3->t > b2->t && std::abs(b3->v - 100) <= 1 && nth(r.song, 'n', 1)->t >= b3->t); // ...and repeated at the note's start
        CHECK(nth(r.song, 'B', 4) && nth(r.song, 'B', 4)->v > 1100 && nth(r.song, 'B', 4)->v <= 1200); // range 12: nearly an octave
    }
    {   // a "loop end" marker says where the song loops; a note still sounding rings on past it
        Track t;
        t.ev(0, {0x90, 60, 100}).ev(960, {0x80, 60, 0}).end();
        Track m;
        m.ev(0, {0x90, 60, 100}).ev(480, {0xff, 0x06, 8, 'l', 'o', 'o', 'p', ' ', 'e', 'n', 'd'}).ev(480, {0x80, 60, 0}).end();
        midi::Result r, plain;
        CHECK(midi::toSong(file(0, 480, {m}), opt, r, why) && r.song.end == 100 && nth(r.song, 'n', 0)->dur == 200);
        CHECK(midi::toSong(file(0, 480, {t}), opt, plain, why) && plain.song.end == 200); // without it, the song is as long as its last note
    }
    {   // sustain: a note released under the pedal ends when the pedal comes up
        midi::Result r;
        Track t;
        t.ev(0, {0xb0, 64, 127}).ev(0, {0x90, 60, 100}).ev(96, {0x80, 60, 0}).ev(384, {0xb0, 64, 0}).end();
        CHECK(midi::toSong(file(0, 480, {t}), opt, r, why));
        CHECK(nth(r.song, 'n', 0)->dur == 100); // 480 ticks, not 96
    }
    {   // a note still on at the end ends with the track
        midi::Result r;
        Track t;
        t.ev(0, {0x90, 60, 100}).end(480);
        CHECK(midi::toSong(file(0, 480, {t}), opt, r, why) && nth(r.song, 'n', 0)->dur == 100);
    }
    {   // the same note twice at once: each off closes the oldest
        midi::Result r;
        Track t;
        t.ev(0, {0x90, 60, 100}).ev(240, {0x90, 60, 100}).ev(240, {0x80, 60, 0}).ev(240, {0x80, 60, 0}).end();
        CHECK(midi::toSong(file(0, 480, {t}), opt, r, why));
        CHECK(nth(r.song, 'n', 0)->dur == 100 && nth(r.song, 'n', 1)->dur == 100);
    }
    {   // files it refuses
        midi::Result r;
        CHECK(!midi::toSong({1, 2, 3}, opt, r, why) && why.find("not a MIDI") != std::string::npos);
        Track t;
        t.ev(0, {0x90, 60, 100}).end(10);
        CHECK(!midi::toSong(file(2, 480, {t}), opt, r, why) && why.find("format 2") != std::string::npos);
        CHECK(!midi::toSong(file(0, 0xe728, {t}), opt, r, why) && why.find("SMPTE") != std::string::npos);
        Track silent;
        silent.tempo(0, 500000).end();
        CHECK(!midi::toSong(file(0, 480, {silent}), opt, r, why) && why.find("no notes") != std::string::npos);
        Bytes cut = file(0, 480, {t});
        cut.resize(cut.size() - 3);
        CHECK(midi::toSong(cut, opt, r, why) || !why.empty()); // cut short: either read what is there or say so, never crash
        Track bad;
        bad.b = {0x00, 0x40, 0x10}; // data with no status
        CHECK(!midi::toSong(file(0, 480, {bad}), opt, r, why) && why.find("status") != std::string::npos);
    }
    { // General MIDI: program changes and drum notes of any GM player's song
        midi::Options g; // the default: General MIDI
        auto prog = [&](int program, midi::Result &r) { // the ROM instrument a GM program ends up as
            Track t;
            t.ev(0, {0xc0, program}).ev(0, {0x90, 60, 100}).ev(10, {0x80, 60, 0}).end();
            return midi::toSong(file(0, 480, {t}), g, r, why) ? nth(r.song, 'p', 0)->v : -99;
        };
        midi::Result r;
        CHECK(prog(56, r) == 13 && r.warnings.empty());  // Trumpet
        CHECK(prog(11, r) == 0 && r.warnings.empty());   // Vibraphone
        CHECK(prog(0, r) == 2 && prog(32, r) == 1 && prog(45, r) == 5 && prog(25, r) == 8 && prog(55, r) == 12 && prog(54, r) == 9);
        CHECK(prog(40, r) == 11 && r.warnings.size() == 1 && r.warnings[0].find("General MIDI program 40") != std::string::npos); // a violin: the strings family
        CHECK(prog(19, r) == 11);                        // church organ: organs
        CHECK(prog(73, r) == 4 && prog(71, r) == 7);     // flute, clarinet
        for (int p = 0; p < 128; ++p)
            CHECK(prog(p, r) >= 0 && prog(p, r) < gm::kRomInstruments); // every program ends up as an instrument the ROM has
        for (int i = 0; i < gm::kRomInstruments; ++i)
            CHECK(gm::programToRom(gm::romInstrument(i).program) == i); // each instrument's own GM program comes back to it
        midi::Options mapped = g;
        mapped.programs[56] = 4;
        Track t;
        t.ev(0, {0xc0, 56}).ev(0, {0x90, 60, 100}).ev(10, {0x80, 60, 0}).end();
        CHECK(midi::toSong(file(0, 480, {t}), mapped, r, why) && nth(r.song, 'p', 0)->v == 4); // the record's map wins
        midi::Options rom = g;
        rom.gmPrograms = false;
        CHECK(midi::toSong(file(0, 480, {t}), rom, r, why) && nth(r.song, 'p', 0)->v == 0 && r.warnings.size() == 1); // read as the ROM's numbers: 56 is out of range
        // drums
        Track d;
        d.ev(0, {0x99, 36, 100}).ev(10, {0x89, 36, 0}).ev(0, {0x99, 38, 100}).ev(10, {0x89, 38, 0}).ev(0, {0x99, 42, 100}).ev(10, {0x89, 42, 0})
            .ev(0, {0x99, 5, 100}).ev(10, {0x89, 5, 0}).ev(0, {0x99, 30, 100}).ev(10, {0x89, 30, 0}).end();
        CHECK(midi::toSong(file(0, 480, {d}), g, r, why));
        CHECK(r.notes == 4 && nth(r.song, 'n', 0)->pb == 1 && nth(r.song, 'n', 1)->pb == 3 && nth(r.song, 'n', 2)->pb == 0 && nth(r.song, 'n', 3)->pb == 5);
        CHECK(r.warnings.size() == 1 && r.warnings[0].find("drum note 30") != std::string::npos);
        for (int i = 0; i < gm::kRomDrums; ++i)
            CHECK(gm::drumToRom(gm::romDrum(i).note) == i); // each drum's own GM note comes back to it
        // a channel named with a word
        auto named = [&](const std::string &name)
        {
            Track nt;
            std::vector<int> bytes = {0xff, 0x03, int(name.size())};
            for (char c : name)
                bytes.push_back(c);
            vlq(nt.b, 0);
            for (int x : bytes)
                nt.b.push_back(uint8_t(x));
            nt.ev(0, {0x90, 60, 100}).ev(10, {0x80, 60, 0}).end();
            midi::Result nr;
            midi::toSong(file(1, 96, {nt}), g, nr, why);
            return nth(nr.song, 'p', 0) ? nth(nr.song, 'p', 0)->v : -1;
        };
        CHECK(named("Trumpet lead") == 13 && named("my PIZZICATO strings") == 5 && named("Strings") == 11 && named("Bass") == 1 && named("Piano") == 2);
        CHECK(named("3x Osc 1") == -1 && named("Trumpet [i2]") == 2); // a number in brackets wins
    }
    { // instruments from a track's name ("Lead [i3]") or the channels map, where the file has no program changes (as FL writes it)
        auto named = [](const std::string &name, std::initializer_list<int> notes)
        {
            Track t;
            std::vector<int> d = {0xff, 0x03, int(name.size())};
            for (char c : name)
                d.push_back(c);
            vlq(t.b, 0);
            for (int x : d)
                t.b.push_back(uint8_t(x));
            for (int n : notes)
                t.ev(0, {0x90, n, 100}).ev(96, {0x80, n, 0});
            return t;
        };
        auto programOf = [&](const midi::Result &r, int ch) // the last program event of a channel at time 0
        {
            int v = -1;
            for (const auto &e : r.song.ev)
                if (e.k == 'p' && e.ch == ch && e.t == 0)
                    v = e.v;
            return v;
        };
        midi::Result r;
        CHECK(midi::toSong(file(1, 96, {named("Lead [i3]", {60})}), opt, r, why) && programOf(r, 0) == 3 && r.warnings.empty());
        CHECK(midi::toSong(file(1, 96, {named("Bass [i12] soft", {40})}), opt, r, why) && programOf(r, 0) == 12);
        CHECK(midi::toSong(file(1, 96, {named("3x Osc 1", {60})}), opt, r, why) && programOf(r, 0) == -1);       // no brackets: the default instrument
        CHECK(midi::toSong(file(1, 96, {named("Lead [fast]", {60})}), opt, r, why) && programOf(r, 0) == -1);   // not a number
        CHECK(midi::toSong(file(1, 96, {named("Lead [i20]", {60})}), opt, r, why) && programOf(r, 0) == -1 && r.warnings.size() == 1 && r.warnings[0].find("instrument 20") != std::string::npos);
        midi::Options mapped;
        mapped.channels[0] = 7;
        CHECK(midi::toSong(file(1, 96, {named("Lead [i3]", {60})}), mapped, r, why) && programOf(r, 0) == 7); // the record's map wins over the name
        Track pc = named("Lead [i3]", {60});
        pc.ev(0, {0xc0, 5}).ev(0, {0x90, 62, 100}).ev(96, {0x80, 62, 0}).end();
        CHECK(midi::toSong(file(1, 96, {pc}), opt, r, why) && programOf(r, 0) == 3 && !nth(r.song, 'p', 1)); // a channel chosen by its name ignores the file's program changes
        { // as FL writes it: names like "Bass[2]" (the number is FL's own) and a program 0 on every channel
            Track fl = named("Bass[2]", {40});
            fl.ev(0, {0xc0, 0}).ev(0, {0xc0, 0}).end();
            CHECK(midi::toSong(file(1, 96, {fl}), opt, r, why) && programOf(r, 0) == 1 && !nth(r.song, 'p', 1));
            Track unnamed = named("3x Osc 1", {40});
            unnamed.ev(0, {0xc0, 5}).ev(0, {0x90, 62, 100}).ev(96, {0x80, 62, 0}).end();
            CHECK(midi::toSong(file(1, 96, {unnamed}), opt, r, why) && nth(r.song, 'p', 0) && nth(r.song, 'p', 0)->v == 5); // no name to go by: the program change is the instrument
        }
        Track drum = named("Drums [4]", {});
        drum.ev(0, {0x99, 1, 100}).ev(10, {0x89, 1, 0}).end();
        CHECK(midi::toSong(file(1, 96, {drum}), opt, r, why) && programOf(r, 9) == -1); // not for the drum channel
    }
    { // slides as FL Studio writes them (CtoE.mid and EtoC.mid, 140 bpm, one measure): two notes starting and ending together, the target first
        auto fromHex = [](const std::string &h)
        {
            Bytes b;
            for (size_t i = 0; i + 1 < h.size(); i += 3)
                b.push_back(uint8_t(std::stoi(h.substr(i, 2), nullptr, 16)));
            return b;
        };
        const Bytes ctoe = fromHex("4d 54 68 64 00 00 00 06 00 01 00 02 00 60 4d 54 72 6b 00 00 00 13 00 ff 51 03 06 8a 1b 00 ff 58 04 04 02 18 08 00 ff 2f 00 4d 54 72 6b "
                                   "00 00 00 21 00 ff 03 08 33 78 20 4f 73 63 20 31 00 90 4c 64 00 90 48 64 83 00 80 4c 40 00 80 48 40 00 ff 2f 00 ");
        const Bytes etoc = fromHex("4d 54 68 64 00 00 00 06 00 01 00 02 00 60 4d 54 72 6b 00 00 00 13 00 ff 51 03 06 8a 1b 00 ff 58 04 04 02 18 08 00 ff 2f 00 4d 54 72 6b "
                                   "00 00 00 21 00 ff 03 08 33 78 20 4f 73 63 20 31 00 90 48 64 00 90 4c 64 83 00 80 48 40 00 80 4c 40 00 ff 2f 00 ");
        { // not named as a slide channel: it is the chord it looks like
            midi::Result r;
            CHECK(midi::toSong(ctoe, opt, r, why) && r.notes == 2 && !nth(r.song, 'B', 0));
        }
        midi::Options sl;
        sl.slideChannels = {0};
        for (int rising = 1; rising >= 0; --rising)
        {
            midi::Result r;
            CHECK(midi::toSong(rising ? ctoe : etoc, sl, r, why));
            CHECK(r.notes == 1);
            const rau::Event *n = nth(r.song, 'n', 0);
            CHECK(n && n->pb == (rising ? 72 : 76) && n->t == 0 && n->dur >= 342 && n->dur <= 344); // one measure at 140 bpm: 4 x 0.4286 s = 343 ticks
            int count = 0, prev = 0;
            bool smooth = true;
            const rau::Event *last = nullptr;
            for (const auto &e : r.song.ev)
                if (e.k == 'B')
                {
                    ++count;
                    if (rising ? e.v < prev : e.v > prev)
                        if (e.t < n->dur) // (the last one puts the bend back to 0)
                            smooth = false;
                    prev = e.v;
                    if (e.t < n->dur)
                        last = &e;
                }
            CHECK(count >= 340 && smooth);
            CHECK(last && std::abs(last->v) >= 395 && std::abs(last->v) <= 400 && (last->v > 0) == bool(rising)); // four semitones towards the target
            CHECK(prev == 0); // and back to the note's own pitch at the end
            CHECK(r.song.end >= 343);
        }
        { // slides in the middle of a note (FL: a slide note starts where the glide starts and ends where it ends), one after another; the pitch is held between
            midi::Result r;
            Track t;
            // C4 for four beats; slide notes: E4 for the 2nd beat, G4 for the 4th; a separate note on another channel is left alone
            t.ev(0, {0x90, 60, 100}).ev(0, {0x91, 72, 100}).ev(480, {0x90, 64, 100}).ev(480, {0x80, 64, 0}).ev(480, {0x90, 67, 100}).ev(480, {0x80, 67, 0}).ev(0, {0x80, 60, 0})
                .ev(0, {0x81, 72, 0}).end();
            CHECK(midi::toSong(file(0, 480, {t}), sl, r, why));
            CHECK(r.notes == 2); // the C and the other channel's note; the two slides are not notes of their own
            const rau::Event *c = nth(r.song, 'n', 0);
            CHECK(c && c->pb == 60 && c->dur == 400); // four beats at 120 bpm = 2 s
            auto bendAt = [&](int tick) // the last bend of channel 0 at or before this player tick
            {
                int v = 0;
                for (const auto &e : r.song.ev)
                    if (e.k == 'B' && e.ch == 0 && e.t <= tick)
                        v = e.v;
                return v;
            };
            CHECK(bendAt(50) == 0);                        // before the first slide (beat 1 is at 100)
            CHECK(std::abs(bendAt(150) - 200) <= 2);       // half way through the slide up to E (+400): 200 cents
            CHECK(bendAt(210) == 400 && bendAt(290) == 400); // held at E after it
            CHECK(std::abs(bendAt(350) - 550) <= 2);       // the next slide goes on from E to G (+700): half way = 550
            CHECK(bendAt(399) >= 695 && bendAt(399) <= 700);
            CHECK(bendAt(400) == 0);                       // the note ended: back to its own pitch
            int others = 0;
            for (const auto &e : r.song.ev)
                if (e.k == 'B' && e.ch != 0)
                    ++others;
            CHECK(others == 0 && nth(r.song, 'n', 1)->ch == 1);
        }
        { // a note that is not inside another is not a slide, and neither is a note of the same pitch
            midi::Result r;
            Track t;
            t.ev(0, {0x90, 60, 100}).ev(240, {0x90, 64, 100}).ev(480, {0x80, 60, 0}).ev(240, {0x80, 64, 0}).ev(0, {0x90, 62, 100}).ev(0, {0x90, 62, 100}).ev(100, {0x80, 62, 0})
                .ev(0, {0x80, 62, 0}).end();
            CHECK(midi::toSong(file(0, 480, {t}), sl, r, why) && r.notes == 4);
            CHECK(!nth(r.song, 'B', 0));
        }
    }
    if (g_fail)
        return 1;
    std::cout << "midi_test: all passed\n";
    return 0;
}
