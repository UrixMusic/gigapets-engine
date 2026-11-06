// native_mod.hpp - native code mods: a .dll in the mod's folder (manifest "native") that the game loads into itself.
//
// A native mod can do anything the program can: it is not sandboxed at all. So it never loads by itself; the caller passes only the
// mods the player has allowed (see NativeAllow). The game exports its symbols (build with -Wl,--export-all-symbols) and every function
// starts with 16 bytes of nops (-fpatchable-function-entry=16), so a mod can replace any function by its (mangled) name: the nops take
// a jump to the mod's function. The original keeps working: `original` points just past the nops. Two mods replacing the same function chain: the second's
// `original` leads to the first's replacement.
//
// The mod's entry point:  extern "C" __declspec(dllexport) void gp_mod_init(const GpNativeApi *api);
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

// The version of the interface and of the structures a native mod is built against. A mod's dll says which one it was built with (GP_NATIVE_MOD_ABI
// below, once in its source); the game refuses a dll built for another, because its idea of the game's structures would be wrong and it would crash.
// Raise it whenever a struct a mod can see changes.
#define GP_NATIVE_ABI 1
#define GP_NATIVE_MOD_ABI extern "C" __declspec(dllexport) int gp_mod_abi() { return GP_NATIVE_ABI; }

extern "C"
{
    struct GpNativeApi
    {
        uint32_t version; // 1
        // Replace the function with this mangled name (as nm / the compiler writes it) by `replacement`; *original gets the way to call the old
        // one. Returns false (and logs) if the game has no such function, or it was not built patchable.
        bool (*detour)(const char *symbol, void *replacement, void **original);
        // The address of any function or variable the game exports, or null.
        void *(*symbol)(const char *name);
        void (*log)(const char *message); // to the game's log (stderr), tagged with the mod
    };
}

namespace gp
{
    namespace native_detail
    {
        inline std::string &currentMod()
        {
            static std::string s;
            return s;
        }
#ifdef _WIN32
        inline void *symbol(const char *name) { return reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA(nullptr), name)); }
        inline bool detour(const char *name, void *repl, void **orig)
        {
            unsigned char *p = static_cast<unsigned char *>(symbol(name));
            if (!p || !repl || !orig)
            {
                std::fprintf(stderr, "native mod [%s]: no function '%s' to replace\n", currentMod().c_str(), name ? name : "(null)");
                return false;
            }
            // a function we can patch starts with 16 padding bytes (nops). The first 6 become `jmp [rip+2]`, which jumps to the address held in
            // bytes 8..15. The old code is untouched after the padding: the way to the original is entry + 16, or, if another mod got there first,
            // the address it put in the slot.
            DWORD old;
            if (!VirtualProtect(p, 16, PAGE_EXECUTE_READWRITE, &old))
                return false;
            const bool already = p[0] == 0xFF && p[1] == 0x25; // another mod replaced it: chain through its replacement
            void *next = already ? *reinterpret_cast<void **>(p + 8) : static_cast<void *>(p + 16);
            *reinterpret_cast<void **>(p + 8) = repl; // the slot first: nothing reads it as code once the jump is in
            if (!already)
            {
                const unsigned char jmp[6] = {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00};
                std::memcpy(p, jmp, 6);
            }
            VirtualProtect(p, 16, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 16);
            *orig = next;
            return true;
        }
#else
        inline void *symbol(const char *) { return nullptr; }
        inline bool detour(const char *, void *, void **) { return false; }
#endif
        inline void log(const char *m) { std::fprintf(stderr, "native mod [%s]: %s\n", currentMod().c_str(), m ? m : ""); }
    } // namespace native_detail

    // ---- the player's say-so ----
    // A native mod runs only if the player allowed this very dll: native_allowed.txt holds "allow <id> <hash of the dll's bytes>". Change the dll
    // and the hash no longer matches, so it has to be allowed again.
    inline std::string nativeHash(const std::string &path)
    {
        std::FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
            return "";
        uint64_t h = 1469598103934665603ull; // FNV-1a, 64 bit
        static unsigned char buf[65536];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
            for (size_t i = 0; i < n; ++i)
                h = (h ^ buf[i]) * 1099511628211ull;
        std::fclose(f);
        char out[17];
        std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
        return out;
    }
    struct NativeAllow
    {
        std::map<std::string, std::string> hash; // mod id -> the hash of the dll the player allowed
        // allowed: this dll is the one the player allowed. changed: they allowed another dll of this mod before.
        bool allowed(const std::string &id, const std::string &dllPath) const
        {
            auto it = hash.find(id);
            if (it == hash.end())
                return false;
            const std::string h = nativeHash(dllPath);
            return !h.empty() && h == it->second;
        }
        bool changed(const std::string &id, const std::string &dllPath) const { return hash.count(id) && !allowed(id, dllPath); }
        bool allow(const std::string &id, const std::string &dllPath)
        {
            const std::string h = nativeHash(dllPath);
            if (h.empty())
                return false;
            hash[id] = h;
            return true;
        }
    };
    inline NativeAllow loadNativeAllow(const std::string &path)
    {
        NativeAllow a;
        std::FILE *f = std::fopen(path.c_str(), "r");
        if (!f)
            return a;
        char line[512], id[256], h[64];
        while (std::fgets(line, sizeof line, f))
            if (std::sscanf(line, "allow %255s %63s", id, h) == 2)
                a.hash[id] = h;
        std::fclose(f);
        return a;
    }
    inline bool saveNativeAllow(const NativeAllow &a, const std::string &path)
    {
        std::FILE *f = std::fopen(path.c_str(), "w");
        if (!f)
            return false;
        for (const auto &[id, h] : a.hash)
            std::fprintf(f, "allow %s %s\n", id.c_str(), h.c_str());
        std::fclose(f);
        return true;
    }
    // ---- safe mode ----
    // While native mods are running, a marker file exists; it goes away when the game ends normally. If it is still there at the next start, the
    // last game ended some other way (a crash, most likely in native code): the native mods stay off for that start, and the marker is cleared.
    struct NativeRunMarker
    {
        std::string path;
        bool armed = false;
        void arm(const std::string &p)
        {
            path = p;
            if (std::FILE *f = std::fopen(p.c_str(), "w"))
            {
                std::fputs("native mods are running\n", f);
                std::fclose(f);
                armed = true;
            }
        }
        ~NativeRunMarker()
        {
            if (armed)
                std::remove(path.c_str());
        }
    };
    inline bool fileExists(const std::string &p)
    {
        std::FILE *f = std::fopen(p.c_str(), "rb");
        if (f)
            std::fclose(f);
        return f != nullptr;
    }

    // Load these mods' dlls (mod id -> dll path), in this order, and run each one's gp_mod_init. Returns the problems.
    inline std::vector<std::string> loadNativeMods(const std::vector<std::pair<std::string, std::string>> &mods)
    {
        std::vector<std::string> errs;
#ifdef _WIN32
        static GpNativeApi api{1, native_detail::detour, native_detail::symbol, native_detail::log};
        for (const auto &[id, path] : mods)
        {
            native_detail::currentMod() = id;
            HMODULE h = LoadLibraryA(path.c_str());
            if (!h)
            {
                errs.push_back("[" + id + "] could not load " + path + " (error " + std::to_string(GetLastError()) + ")");
                continue;
            }
            auto abi = reinterpret_cast<int (*)()>(GetProcAddress(h, "gp_mod_abi"));
            if (!abi || abi() != GP_NATIVE_ABI)
            {
                errs.push_back("[" + id + "] " + path + " was built for another version of the game (it says " + (abi ? std::to_string(abi()) : std::string("nothing")) +
                               ", this game is " + std::to_string(GP_NATIVE_ABI) + "); rebuild it with this game's source");
                FreeLibrary(h);
                continue;
            }
            auto init = reinterpret_cast<void (*)(const GpNativeApi *)>(GetProcAddress(h, "gp_mod_init"));
            if (!init)
            {
                errs.push_back("[" + id + "] " + path + " has no gp_mod_init");
                continue;
            }
            init(&api);
        }
#else
        for (const auto &m : mods)
            errs.push_back("[" + m.first + "] native mods need Windows");
#endif
        return errs;
    }
} // namespace gp
