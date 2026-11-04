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
#include "audio_clip.hpp"
#include "audio_registry.hpp"
#include "midi.hpp"
#include "rom_audio.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace gp
{

    class Audio
    {
    public:
        static constexpr int kRate = 44100, kVoices = 12;

        ~Audio()
        {
            close();
            stopWorker();
            stopScan();
        }

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
        void setRom(const Rom &rom)
        {
            romAudio_ = std::make_unique<rau::RomAudio>(rom);
            romAudio_->nearest = retro_;
        }

        // Retro audio: the ROM's sounds played the way the chip does, with no blending between samples (rom_audio.hpp). The clips
        // are kept apart by mode, so what is playing is never freed; the music in progress starts again in the new mode.
        void setRetro(bool on)
        {
            if (on == retro_)
                return;
            retro_ = on;
            if (romAudio_)
                romAudio_->nearest = on;
            if (music_ >= 0)
            {
                const int m = music_;
                music_ = -1;
                playMusic(m);
            }
        }
        bool retro() const { return retro_; }

        // Live music: a song that is notes (the ROM's, or a mod's MIDI file) is played as it goes, a few hundred frames at a time, instead of
        // being rendered whole first. It starts at once and needs no memory for the finished song; a song that is a sound file (Ogg, WAV), or
        // a MIDI one with a pitch change or loop points set in its record, is played from the baked clip as before. The music in progress
        // starts again in the new mode.
        void setLive(bool on)
        {
            if (on == live_)
                return;
            live_ = on;
            if (music_ >= 0)
            {
                const int m = music_;
                music_ = -1;
                playMusic(m);
            }
            else if (!on)
            {
                lock();
                musicLive_.reset();
                unlock();
            }
        }
        bool live() const { return live_; }
        // Is the music playing now made as it goes? (For the tests.)
        bool playingLive() const { return musicLive_ != nullptr; }
        // How far the live song is turned down so it does not clip (0 until its peak is known). For the tests.
        float liveScale() const { return liveGoal_.load(); }

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
            adoptAsync();
            const AudioClip *c = sfxClipFor(id);
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

        // The clip a sound number plays: a mod's file (its own sound, or one that replaces the ROM's), else the ROM's. Null when there is none.
        // (Public so it can be tested without a sound device.)
        const AudioClip *sfxClipFor(int id)
        {
            if (const ModSound *m = soundFileFor(id))
                if (const AudioClip *c = modClip(*m, false))
                    return c;
            if (id >= kModSoundBase)
                return nullptr; // a mod's sound whose file would not load: nothing to fall back to
            return clip(dir_ + "/sfx", id);
        }
        // Like musicClipFor, but a mod's song that is not made yet is made on another thread (a long MIDI file takes seconds to render, an Ogg to
        // decode): returns null until it is, and asking again then gives the clip. A song that cannot be made stays null.
        const AudioClip *musicClipAsync(int n)
        {
            adoptAsync();
            const ModSound *m = songFileFor(n);
            if (!m)
                return musicClipFor(n);
            const auto it = clips_.find(modKey(*m, true));
            if (it != clips_.end())
                return it->second.get();
            startAsync(n, *m);
            return nullptr;
        }
        bool renderingMusic() const { return workerBusy_.load(); }

        const AudioClip *musicClipFor(int n)
        {
            if (const ModSound *m = songFileFor(n))
                if (const AudioClip *c = modClip(*m, true))
                    return c;
            if (n >= kModMusicBase)
                return nullptr;
            return clip(dir_ + "/music", n);
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
            if (live_ && startLive(n))
                return;
            const AudioClip *c = musicClipAsync(n);
            lock();
            music_ = n;
            musicLive_.reset();
            pendingMusic_ = !c && workerBusy_.load() && songFileFor(n) ? n : -1; // a mod's song still being made: silence until it is, then it starts by itself
            musicVoice_ = c ? Voice{c, 0, true, true, n} : Voice{};
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
            pendingMusic_ = -1;
            musicLive_.reset();
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
            if (pendingMusic_ >= 0 && workerReady_.load(std::memory_order_acquire) && asyncSong_ == pendingMusic_ && music_ == pendingMusic_ && asyncClip_)
            {
                musicVoice_ = Voice{asyncClip_.get(), 0, true, true, music_}; // the song that was being made is ready
                pendingMusic_ = -1;
            }
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
            if (musicLive_ && musicOn_)
            {
                std::vector<float> f(frames * 2, 0.f);
                musicLive_->render(f.data(), frames);
                const float goal = liveGoal_.load(std::memory_order_acquire); // 0: the scan for how loud the song gets is still running
                const float k = 1.f / (kRate * 0.7f);
                const float level = volume_ * musicVol_ * liveBase_ * 32000.f;
                for (size_t i = 0; i < frames; ++i)
                {
                    if (goal > 0)
                        liveGain_ += (goal - liveGain_) * k; // eases to the right level (the baked song is turned down as a whole when it would clip)
                    for (int c = 0; c < 2; ++c)
                        acc[i * 2 + size_t(c)] += int32_t(std::clamp(f[i * 2 + size_t(c)] * liveGain_, -1.f, 1.f) * level);
                }
            }
            for (size_t i = 0; i < frames * 2; ++i)
                out[i] = int16_t(std::clamp(acc[i], -32768, 32767));
        }

        // What makes a mod's clip different from another made of the same file: kept in the cache under this.
        std::string modKey(const ModSound &m, bool music) const
        {
            char key[512];
            std::snprintf(key, sizeof key, "mod:%s|%g|%g|%g|%g|%d|%d", m.path.c_str(), double(m.gain), m.semitones, m.loopStart, m.loopEnd, music ? 1 : 0, m.midi && retro_ ? 1 : 0);
            std::string k = key;
            k += m.gmNumbers ? "|gm" : "|rom";
            for (const auto &kv : m.programs)
                k += "|p" + std::to_string(kv.first) + "=" + std::to_string(kv.second);
            for (const auto &kv : m.drums)
                k += "|d" + std::to_string(kv.first) + "=" + std::to_string(kv.second);
            for (const auto &kv : m.channels)
                k += "|c" + std::to_string(kv.first) + "=" + std::to_string(kv.second);
            for (int c : m.slideChannels)
                k += "|s" + std::to_string(c);
            return k;
        }

        // The clip of a mod's sound file (or null, with a message): volume, pitch and loop points applied. Safe to run on another thread.
        std::unique_ptr<AudioClip> buildModClip(const ModSound &m, bool music, const std::atomic<bool> *cancel = nullptr) const
        {
            PcmData pcm;
            std::string why;
            const bool loaded = m.midi ? renderMidi(m, pcm, why, cancel) : loadSoundFile(m.path, pcm, why);
            if (cancel && cancel->load())
                return nullptr;
            if (!loaded)
            {
                std::printf("audio: %s (%s)\n", why.c_str(), m.id.c_str());
                return nullptr;
            }
            auto c = std::make_unique<AudioClip>(makeClip(pcm, kRate, m.gain, m.semitones, music ? m.loopStart : 0, music ? m.loopEnd : 0));
            if (c->frames() == 0)
                return nullptr;
            if (!music)
                fadeEnds(*c);
            return c;
        }

        // A mod's sound file as a clip at the mixer's rate with its volume, pitch and loop points; read once, kept by what it was made with.
        const AudioClip *modClip(const ModSound &m, bool music)
        {
            const std::string key = modKey(m, music);
            auto it = clips_.find(key);
            if (it != clips_.end())
                return it->second.get();
            std::unique_ptr<AudioClip> c = buildModClip(m, music);
            const AudioClip *p = c.get();
            clips_[key] = std::move(c);
            return p;
        }

        // ---- a song made on another thread ----
        // One job at a time. The worker leaves its clip in asyncClip_ and sets workerReady_; the mixer starts the song from there (under the
        // device lock, so nothing else touches it), and the next call from the game thread (adoptAsync) files the clip in the cache and starts
        // the next job if a different song was asked for meanwhile.
        void startAsync(int n, const ModSound &m)
        {
            if (workerBusy_.load())
            {
                wantSong_ = n;
                return;
            }
            stopWorker(false);
            asyncKey_ = modKey(m, true);
            asyncSong_ = n;
            asyncClip_.reset();
            workerReady_.store(false);
            cancel_.store(false);
            workerBusy_.store(true);
            worker_ = std::thread([this, m]
                                  {
                                      const auto t0 = std::chrono::steady_clock::now();
                                      std::unique_ptr<AudioClip> c = buildModClip(m, true, &cancel_);
                                      if (c)
                                          std::printf("audio: made %s in %.1f s\n", m.id.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
                                      asyncClip_ = std::move(c);
                                      workerReady_.store(true, std::memory_order_release);
                                  });
        }
        void adoptAsync()
        {
            if (!workerReady_.load(std::memory_order_acquire))
                return;
            if (worker_.joinable())
                worker_.join();
            lock();
            clips_[asyncKey_] = std::move(asyncClip_); // (the mixer may already be playing it: the object stays where it is)
            if (pendingMusic_ == asyncSong_)
                pendingMusic_ = -1;
            workerReady_.store(false);
            workerBusy_.store(false);
            unlock();
            if (wantSong_ >= 0)
            {
                const int n = wantSong_;
                wantSong_ = -1;
                if (const ModSound *m = songFileFor(n))
                    if (clips_.find(modKey(*m, true)) == clips_.end())
                        startAsync(n, *m);
            }
        }
        void stopWorker(bool cancel = true)
        {
            if (cancel)
                cancel_.store(true);
            if (worker_.joinable())
                worker_.join();
            workerBusy_.store(false);
            workerReady_.store(false);
        }

        // ---- live music ----
        // Starts song n as it goes if it can be (see setLive). The baked song is turned down as a whole when its loudest moment would clip,
        // and a song played as it goes cannot know that beforehand: it starts at a middle level while another thread plays it through once to
        // find the peak, then eases to the right one. The peak is kept, so the next time the song comes round it starts right.
        bool startLive(int n)
        {
            if (!romAudio_)
                return false;
            rau::Song own;
            const rau::Song *song = nullptr;
            float base = 1.f;
            std::string key;
            if (const ModSound *m = songFileFor(n))
            {
                std::string why;
                if (!m->midi || m->semitones != 0 || m->loopStart != 0 || m->loopEnd != 0)
                    return false;
                if (!midiSong(*m, own, why))
                {
                    std::printf("audio: %s (%s)\n", why.c_str(), m->id.c_str());
                    return false;
                }
                song = &own;
                base = m->gain;
                key = modKey(*m, true) + "|live";
            }
            else
            {
                if (n < 0 || n >= kModMusicBase || size_t(n) >= romAudio_->songCount())
                    return false;
                char hex[32];
                std::snprintf(hex, sizeof hex, "/0x%x.wav", n);
                std::error_code ec;
                if (std::filesystem::exists(dir_ + "/music/" + std::to_string(n) + ".wav", ec) || std::filesystem::exists(dir_ + "/music" + hex, ec))
                    return false; // a wav file in the audio folder wins over the ROM's song
                song = &romAudio_->songs()[size_t(n)];
                key = "rom:" + std::to_string(n) + (retro_ ? "r" : "");
            }
            auto player = std::make_unique<rau::SongPlayer>(*romAudio_, *song, kRate);
            if (!player->playable())
                return false;
            stopScan();
            float known = 0.f;
            {
                std::lock_guard<std::mutex> g(peakLock_);
                const auto it = peaks_.find(key);
                if (it != peaks_.end())
                    known = it->second;
            }
            lock();
            music_ = n;
            pendingMusic_ = -1;
            musicVoice_ = Voice{};
            musicLive_ = std::move(player);
            liveBase_ = base;
            liveGain_ = known > 0 ? known : 0.5f;
            liveGoal_.store(known, std::memory_order_release);
            unlock();
            if (known <= 0)
            {
                scanCancel_.store(false);
                scanThread_ = std::thread([this, s = *song, key] { scanPeak(s, key); });
            }
            return true;
        }

        // Plays the song through once (without sound) to find how far to turn it down; leaves the answer for the mixer.
        void scanPeak(const rau::Song &song, const std::string &key)
        {
            rau::SongPlayer p(*romAudio_, song, kRate);
            const size_t body = size_t(song.end / rau::kTicksPerSecond * kRate), total = body + size_t(4.0 * kRate), from = std::min<size_t>(body, size_t(4.0 * kRate));
            std::vector<float> buf(4096 * 2);
            float pk = 0;
            for (size_t pos = 0; pos < total;)
            {
                if (scanCancel_.load(std::memory_order_relaxed))
                    return;
                const size_t n = std::min<size_t>(4096, total - pos);
                std::fill(buf.begin(), buf.begin() + std::ptrdiff_t(n * 2), 0.f);
                p.render(buf.data(), n);
                for (size_t i = 0; i < n; ++i)
                    if (pos + i >= from) // the first seconds of the first time round lack the tail the loop folds onto them: the second time's count
                        pk = std::max(pk, std::max(std::fabs(buf[i * 2]), std::fabs(buf[i * 2 + 1])));
                pos += n;
            }
            const float scale = pk > 0.98f ? 0.98f / pk : 1.f;
            {
                std::lock_guard<std::mutex> g(peakLock_);
                peaks_[key] = scale;
            }
            liveGoal_.store(scale, std::memory_order_release);
        }
        void stopScan()
        {
            scanCancel_.store(true);
            if (scanThread_.joinable())
                scanThread_.join();
        }

        // A MIDI file as a song of the ROM's kind (notes on the ROM's instruments). Needs the ROM (setRom).
        bool midiSong(const ModSound &m, rau::Song &song, std::string &why) const
        {
            if (!romAudio_)
                return why = "a MIDI file needs the ROM's instruments (no ROM given)", false;
            std::vector<uint8_t> bytes;
            if (!readFileBytes(m.path, bytes))
                return why = "cannot read " + m.path, false;
            midi::Options o;
            o.programs = m.programs;
            o.drums = m.drums;
            o.slideChannels = m.slideChannels;
            o.channels = m.channels;
            o.gmPrograms = o.gmDrums = m.gmNumbers;
            o.programCount = int(romAudio_->programCount()) - 1; // the last group is the drum kit
            o.drumCount = int(romAudio_->drumCount());
            midi::Result r;
            if (!midi::toSong(bytes, o, r, why))
                return false;
            song = std::move(r.song);
            return true;
        }

        // A MIDI file played with the ROM's instruments, as sound at the mixer's rate. Needs the ROM (setRom).
        bool renderMidi(const ModSound &m, PcmData &out, std::string &why, const std::atomic<bool> *cancel = nullptr) const
        {
            midi::Result r;
            if (!midiSong(m, r.song, why))
                return false;
            const std::vector<float> f = romAudio_->renderSong(r.song, kRate, cancel);
            if (f.empty())
                return why = "the MIDI file renders to nothing", false;
            out.rate = kRate;
            out.channels = 2;
            out.pcm.resize(f.size());
            for (size_t i = 0; i < f.size(); ++i)
                out.pcm[i] = int16_t(std::clamp(f[i], -1.f, 1.f) * 32000.f);
            return true;
        }

        // Loads (once) audio/<kind>/<id>.wav, trying the decimal and the 0x name. Null if there is none.
        const AudioClip *clip(const std::string &folder, int id)
        {
            const std::string key = (retro_ ? "retro:" : "") + folder + "/" + std::to_string(id);
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
        int pendingMusic_ = -1;                 // a song being made on the worker thread that should start when it is done
        std::atomic<bool> workerBusy_{false}, workerReady_{false}, cancel_{false};
        std::string asyncKey_;
        std::unique_ptr<AudioClip> asyncClip_;
        int asyncSong_ = -1, wantSong_ = -1;
        std::thread worker_;
        Voice musicVoice_;
        bool live_ = false;
        std::unique_ptr<rau::SongPlayer> musicLive_; // the music playing as it goes (setLive), else null
        float liveGain_ = 1.f, liveBase_ = 1.f;      // how far it is turned down now, and the record's own gain
        std::atomic<float> liveGoal_{0.f};           // how far it should be turned down; 0 = not known yet
        std::thread scanThread_;
        std::atomic<bool> scanCancel_{false};
        std::mutex peakLock_;
        std::map<std::string, float> peaks_;
        std::array<Voice, kVoices> voices_{};
        std::map<std::string, std::unique_ptr<AudioClip>> clips_;
        std::unique_ptr<rau::RomAudio> romAudio_;
        bool retro_ = false;
    };

    // The one mixer for the whole program.
    inline Audio &audio()
    {
        static Audio a;
        return a;
    }

} // namespace gp
