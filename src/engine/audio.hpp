// audio.hpp - sound effects and music through SDL2.
//
// The game's sounds are looked up by the ROM's own ids: a sound effect is the id PlaySoundEffect is called with
// (0..0x5f), a music track is the index Sound_LookupAndStart is called with. Each one is a WAV file next to the game:
//   audio/sfx/<id>.wav     id in decimal (84.wav) or as written in the code (0x54.wav)
//   audio/music/<n>.wav    looped until stopped
// A missing file is skipped (it is reported once), so the game runs the same with no audio folder.
// The mixer is the device's callback: 44.1 kHz, 16-bit stereo, up to kVoices sounds at once, the master volume (0..100,
// the options screen's) applied to all of it and the music switch to the music only.
#pragma once
#include "rom_audio.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gp
{

    // One decoded sound: interleaved stereo samples at the mixer's rate.
    struct AudioClip
    {
        std::vector<int16_t> pcm;
        size_t frames() const { return pcm.size() / 2; }
    };

    // The mixing arithmetic, apart from SDL so it can be tested: adds `frames` frames of the clip from `pos` into `out`
    // (interleaved stereo, 32-bit accumulators) at `gain` (0..1), wrapping when `loop`. Returns the new position, or
    // the clip's end if it ran out.
    inline size_t mixClip(const AudioClip &c, size_t pos, bool loop, float gain, int32_t *out, size_t frames)
    {
        const size_t n = c.frames();
        if (n == 0)
            return 0;
        for (size_t i = 0; i < frames; ++i)
        {
            if (pos >= n)
            {
                if (!loop)
                    return n;
                pos = 0;
            }
            out[i * 2] += int32_t(float(c.pcm[pos * 2]) * gain);
            out[i * 2 + 1] += int32_t(float(c.pcm[pos * 2 + 1]) * gain);
            ++pos;
        }
        return pos;
    }

    class Audio
    {
    public:
        static constexpr int kRate = 44100, kVoices = 12;

        ~Audio() { close(); }

        // Opens the device. False (and the game stays silent) if there is none.
        bool open(const std::string &dir = "audio")
        {
            dir_ = dir;
            if (dev_)
                return true;
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
            {
                std::printf("audio: %s\n", SDL_GetError());
                return false;
            }
            SDL_AudioSpec want{}, got{};
            want.freq = kRate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 1024;
            want.callback = &Audio::callback;
            want.userdata = this;
            dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
            if (!dev_)
            {
                std::printf("audio: %s\n", SDL_GetError());
                return false;
            }
            SDL_PauseAudioDevice(dev_, 0);
            return true;
        }

        void close()
        {
            if (dev_)
            {
                SDL_CloseAudioDevice(dev_);
                dev_ = 0;
            }
        }

        bool ready() const { return dev_ != 0; }

        // The game's own sounds, decoded from the ROM. A wav file in the audio folder still wins (mods). The ROM must
        // outlive the Audio.
        void setRom(const Rom &rom) { romAudio_ = std::make_unique<rau::RomAudio>(rom); }

        void setVolume(int percent)
        {
            lock();
            volume_ = std::clamp(percent, 0, 100) / 100.f;
            unlock();
        }
        void setMusicEnabled(bool on)
        {
            lock();
            musicOn_ = on;
            unlock();
        }
        // Separate levels for the music and the sound effects, on top of the master volume (0..100 each).
        void setMusicVolume(int percent)
        {
            lock();
            musicVol_ = std::clamp(percent, 0, 100) / 100.f;
            unlock();
        }
        void setSfxVolume(int percent)
        {
            lock();
            sfxVol_ = std::clamp(percent, 0, 100) / 100.f;
            unlock();
        }

        // A sound effect by the ROM's id.
        // volume is PlaySoundEffect's second argument (0..255; the ROM uses 0xbe, 0xc8, 0xff...).
        void playSfx(int id, int volume = 255)
        {
            if (!dev_)
                return;
            const AudioClip *c = clip(dir_ + "/sfx", id);
            if (!c)
                return;
            lock();
            Voice *same = nullptr; // a sound is one voice: playing it again restarts it instead of stacking a copy
            for (Voice &w : voices_)
                if (w.clip && w.id == id)
                    same = &w;
            Voice &v = same ? *same : freeVoice();
            v = Voice{c, 0, false, false, id, std::clamp(volume, 0, 255) / 255.f};
            unlock();
        }

        // Is a sound effect with this id still playing? (IsSoundIdActive)
        bool sfxActive(int id)
        {
            lock();
            bool on = false;
            for (const Voice &v : voices_)
                on = on || (v.clip && v.id == id);
            unlock();
            return on;
        }

        // Music track n, looped. Asking for the one already playing leaves it running.
        void playMusic(int n)
        {
            if (!dev_ || n == music_)
                return;
            const AudioClip *c = clip(dir_ + "/music", n);
            lock();
            music_ = n;
            musicVoice_ = Voice{c, 0, true, true, n};
            unlock();
        }

        // Cuts every sound effect still playing (the ROM's ExitCurrentArea); the music is left to its own switch.
        void stopSfx()
        {
            lock();
            for (Voice &v : voices_)
                v = Voice{};
            unlock();
        }

        // StopSoundChannelsByTag: cuts the playing sounds with this id.
        void stopSfx(int id)
        {
            lock();
            for (Voice &v : voices_)
                if (v.clip && v.id == id)
                    v = Voice{};
            unlock();
        }

        void stopMusic()
        {
            lock();
            music_ = -1;
            musicVoice_ = Voice{};
            unlock();
        }

        int music() const { return music_; }

    private:
        struct Voice
        {
            const AudioClip *clip = nullptr;
            size_t pos = 0;
            bool loop = false, music = false;
            int id = -1;
            float gain = 1.f;
        };

        void lock()
        {
            if (dev_)
                SDL_LockAudioDevice(dev_);
        }
        void unlock()
        {
            if (dev_)
                SDL_UnlockAudioDevice(dev_);
        }
        Voice &freeVoice()
        {
            for (Voice &v : voices_)
                if (!v.clip)
                    return v;
            Voice *oldest = &voices_[0]; // all busy: replace the one furthest along
            for (Voice &v : voices_)
                if (v.pos > oldest->pos)
                    oldest = &v;
            return *oldest;
        }

        static void callback(void *user, Uint8 *stream, int len) { static_cast<Audio *>(user)->mix(reinterpret_cast<int16_t *>(stream), size_t(len) / 4); }

        void mix(int16_t *out, size_t frames)
        {
            std::vector<int32_t> acc(frames * 2, 0);
            for (Voice &v : voices_)
                if (v.clip)
                {
                    v.pos = mixClip(*v.clip, v.pos, false, volume_ * sfxVol_ * v.gain, acc.data(), frames);
                    if (v.pos >= v.clip->frames())
                        v = Voice{};
                }
            if (musicVoice_.clip && musicOn_)
                musicVoice_.pos = mixClip(*musicVoice_.clip, musicVoice_.pos, true, volume_ * musicVol_, acc.data(), frames);
            for (size_t i = 0; i < frames * 2; ++i)
                out[i] = int16_t(std::clamp(acc[i], -32768, 32767));
        }

        // Loads (once) audio/<kind>/<id>.wav, trying the decimal and the 0x name. Null if there is none.
        const AudioClip *clip(const std::string &folder, int id)
        {
            const std::string key = folder + "/" + std::to_string(id);
            auto it = clips_.find(key);
            if (it != clips_.end())
                return it->second.get();
            char hex[32];
            std::snprintf(hex, sizeof hex, "/0x%x.wav", id);
            std::unique_ptr<AudioClip> c = load(folder + "/" + std::to_string(id) + ".wav");
            if (!c)
                c = load(folder + hex);
            if (!c && romAudio_)
                c = fromRom(folder.size() >= 5 && folder.compare(folder.size() - 5, 5, "music") == 0, id);
            if (!c)
                std::printf("audio: no file for %s\n", key.c_str());
            const AudioClip *p = c.get();
            clips_[key] = std::move(c);
            return p;
        }

        // A sample that stops off-centre clicks; ramp the first 2 ms in and the last 6 ms out.
        static void fadeEnds(AudioClip &c)
        {
            const size_t n = c.frames(), in = std::min<size_t>(n / 2, kRate * 2 / 1000), out = std::min<size_t>(n / 2, kRate * 6 / 1000);
            for (size_t i = 0; i < in; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    c.pcm[i * 2 + ch] = int16_t(c.pcm[i * 2 + ch] * float(i) / float(in));
            for (size_t i = 0; i < out; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    c.pcm[(n - 1 - i) * 2 + ch] = int16_t(c.pcm[(n - 1 - i) * 2 + ch] * float(i) / float(out));
        }

        std::unique_ptr<AudioClip> fromRom(bool music, int id) const
        {
            const std::vector<float> f = music ? romAudio_->music(size_t(id), kRate) : romAudio_->sfx(id, kRate);
            if (f.empty())
                return nullptr;
            auto c = std::make_unique<AudioClip>();
            c->pcm.resize(f.size());
            for (size_t i = 0; i < f.size(); ++i)
                c->pcm[i] = int16_t(std::clamp(f[i], -1.f, 1.f) * 32000.f);
            if (!music)
                fadeEnds(*c);
            return c;
        }

        static std::unique_ptr<AudioClip> load(const std::string &path)
        {
            SDL_AudioSpec spec{};
            Uint8 *buf = nullptr;
            Uint32 len = 0;
            if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len))
                return nullptr;
            SDL_AudioCVT cvt;
            std::unique_ptr<AudioClip> out;
            if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, kRate) >= 0)
            {
                cvt.len = int(len);
                std::vector<Uint8> data(size_t(len) * size_t(std::max(cvt.len_mult, 1)));
                std::copy(buf, buf + len, data.begin());
                cvt.buf = data.data();
                if (SDL_ConvertAudio(&cvt) == 0)
                {
                    out = std::make_unique<AudioClip>();
                    out->pcm.resize(size_t(cvt.len_cvt) / 2);
                    std::copy(data.begin(), data.begin() + cvt.len_cvt, reinterpret_cast<Uint8 *>(out->pcm.data()));
                }
            }
            SDL_FreeWAV(buf);
            return out;
        }

        std::string dir_ = "audio";
        SDL_AudioDeviceID dev_ = 0;
        float volume_ = 0.8f, musicVol_ = 1.f, sfxVol_ = 1.f;
        bool musicOn_ = true;
        int music_ = -1;
        Voice musicVoice_;
        std::array<Voice, kVoices> voices_{};
        std::map<std::string, std::unique_ptr<AudioClip>> clips_;
        std::unique_ptr<rau::RomAudio> romAudio_;
    };

    // The one mixer for the whole program.
    inline Audio &audio()
    {
        static Audio a;
        return a;
    }

} // namespace gp
