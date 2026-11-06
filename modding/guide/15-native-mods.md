# Native mods (C++)

A native mod is a `.dll` that the game loads into itself. It can replace **any function of the game by its name**, read and write the game's own data, and do whatever a program can. It is the way to change something no record, hook or system reaches.

It is also not sandboxed at all. That is why it never loads without the player saying yes (see "What the player sees").

## A first native mod

`modding/examples/native_demo/` replaces two functions: the window border colour, and how often the sick thought bubble comes. Its manifest names the dll:

```json
{ "id": "native_demo", "name": "Native Demo", "version": "1.0", "game_api": "1", "native": "native/demo.dll" }
```

and `native/demo.cpp` is the whole mod:

```cpp
#include "mods/native_mod.hpp"      // GpNativeApi, GP_NATIVE_MOD_ABI
#include "game/pet/thought_bubble.hpp"  // gp::ThoughtTimer

static bool (*origSick)(gp::ThoughtTimer &, bool, bool, bool);
static bool mySick(gp::ThoughtTimer &t, bool sick, bool walking, bool bubbleUp)
{
    t.accum += 29;                                   // 30 steps for each one
    return origSick(t, sick, walking, bubbleUp);     // then the game's own rule
}

GP_NATIVE_MOD_ABI                                    // which version of the game this was built for
extern "C" __declspec(dllexport) void gp_mod_init(const GpNativeApi *api)
{
    api->detour("_ZN2gp14sickBubbleStepERNS_12ThoughtTimerEbbb", (void *)mySick, (void **)&origSick);
}
```

`native/build.bat` builds it with g++ against the game's `src` folder.

## A second example: the mouse

`modding/examples/native_mouse/` adds mouse control with one replaced function. (The game now has mouse and controller support of its own, see Controllers, the mouse and input; this example is how it could be done from a mod, and how to add a device the game does not handle.) It was written when the game only read the keyboard. The game reads held keys through `gp::KeyBinds::held(state, action)`; the mod answers "yes" when the keyboard says so **or** the mouse asks for that action: left button = Z, middle button = X, and holding the right button walks toward the cursor.

```cpp
static bool (*origHeld)(const gp::KeyBinds *, const Uint8 *, gp::Action);
static bool myHeld(const gp::KeyBinds *self, const Uint8 *state, gp::Action a)
{ return origHeld(self, state, a) || mouseWants(a); }      // a member function takes the object as its first argument
...
api->detour("_ZNK2gp8KeyBinds4heldEPKhNS_6ActionE", (void *)myHeld, (void **)&origHeld);
```

Tested by holding the right button toward a door: the pet walked out of its home. A controller works the same way (`SDL_GameController` in place of `SDL_GetMouseState`), and the built-in one is the better place to start.

## The API (`GpNativeApi`)

| Call | What it does |
|---|---|
| `detour(symbol, replacement, &original)` | Replace the function with this **mangled name**. `original` becomes a pointer to the game's own version, so your function can call it, change its arguments, or ignore it. Returns false (and logs) if there is no such function. |
| `symbol(name)` | The address of any function or variable the game exports, or null. |
| `log(text)` | Writes to the game's log, tagged with the mod. |

Find a function's mangled name with `nm build/gigapets-native.exe | grep sickBubbleStep` (or `nm -C` to read them back). Two mods that replace the same function chain: the second one's `original` leads to the first one's replacement.

## Building against the game

- Use **the same g++** as the game was built with, and the game's `src` folder as the include path. A dll built with another compiler version can disagree about the layout of standard types.
- The game has to be built with `make NATIVE=1` (padding at the start of every function, and its symbols exported). The released game is built this way, and without inlining (`-fno-inline`), so nearly every function has an address of its own to replace.
- The exceptions are the audio and MIDI synthesis headers, which are always optimised and may fold small helpers into their callers. A function that was inlined cannot be replaced by name; replace its caller instead.
- Constants (`constexpr`) are baked into the code that uses them. To change one, replace the function that uses it.

## Versions

`GP_NATIVE_MOD_ABI` puts the game's native interface version (`GP_NATIVE_ABI`) in your dll. It goes up whenever a structure a mod can see changes. The game refuses a dll with another number and says so, instead of crashing. Rebuild the mod against the new source.

## What the player sees

- On the **Mods screen** a native mod is marked `*`, and its info box says whether its code is allowed.
- The first time the player switches it on, the game shows a red full-screen warning (it runs code nothing checks, it can do anything they can) with **No** selected. Only **Allow it** lets it load.
- The game remembers *which dll* was allowed (a hash of its bytes). A new version of the dll has to be allowed again.
- If a game ends any way but normally (a crash), the native mods stay off for the next start, and the Mods screen says so. `GP_SAFE=1` does the same by hand.
- Native mods apply to the game itself. The title screen and the Mods screen run without them.

## Limits

- Windows 64-bit only.
- A bug in a native mod can crash the game, damage a save, or worse. Test with a copy of your saves.
- A mod that replaces a function the audio thread runs can crash while loading: native mods load before the game starts, but keep replacements of audio code small and quick.
