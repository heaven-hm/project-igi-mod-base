# Project IGI Natives SDK

An SDK for single-player mod development for `IGI`. It provides a menu model, logging, and game natives from original game to create simple mod menu using internal and external libaries.
Right now only Internal mod menu is implemented , external witll use ImGUI for Dx7 will be in future releases more robust and customisable.

## Implemented features are:

- In-game menu
- Logging
- Game natives for weather, ammo, health, money, weapons, frame limit, etc.

Target: the EU/Chinese Windows x86 IGI 1 executable.
Out of scope: multiplayer, trainers, memory editors, process scanners.

## Requirements

- CMake 3.20+
- A 32-bit mingw-w64 toolchain (`i686-w64-mingw32-g++`), or a 32-bit MSVC toolset
- Python 3 (used to regenerate the hash table at build time)

## Build

Everything is built by `tools/build-win32-dll.sh`. It sets the C, C++ and RC
cross-compilers explicitly and discards any stale build tree that still points
at a host compiler.

# Build Internal Menu

```sh
# The mod menu DLL (default target)
tools/build-win32-dll.sh --release

# The injector and the keypress helper
tools/build-win32-dll.sh --release --target igi1_injector
tools/build-win32-dll.sh --release --target igi1_keypress
```

Outputs land in `build-win32-release/` (`build-win32-debug/` with `--debug`):

| Artifact                       | Role                                                |
| ------------------------------ | --------------------------------------------------- |
| `IGI-MenuManager-Internal.dll` | The mod menu                                        |
| `IGI1-Injector.exe`            | Loads the DLL into a running `igi.exe`              |
| `IGI1-Keypress.exe`            | Sends keys to the Wine/Windows desktop during tests |

Debug builds allocate a console; Release builds log to file only.

## Natives

All wrappers live in `include/igi_sdk/natives/mvp_natives.hpp` and use the
upstream `IGI::DOMAIN::NAME` style. Include the umbrella header and call them:

```cpp
#include "igi_sdk/IGI.hpp"

IGI::GAME::SET_RAINING("rain");        // weather: "rain" | "snow" | "clear"
IGI::MISC::FRAMES_SET(30);             // frame limit
IGI::PLAYER::SET_UNLIMITED_AMMO();     // player
IGI::AUDIO::MUSIC_SET_VOLUME(0.5f, 1.0f);
IGI::GAME::SET_DISPLAY_MODE();

const bool paused = IGI::GAME::IS_PAUSED() != 0;
const auto event0 = IGI::GAME::GET_EVENT0();
void* world       = IGI::GAME::GET_WORLD();
```

Void natives are queued onto the game thread. Value-returning natives must be
called from the game thread, otherwise they log a warning and return zero.
The full symbol table is also available as `IGI::HASH::<Name>` constants.

## Config

`config.ini` sits beside `igi.exe` and is read on startup.

```ini
[mod_menu]
enabled=true
start_key=F3              ; opens the menu and toggles it closed again
end_key=F12               ; closes the menu and unloads the SDK
native_mode=hooked        ; hooked = per-frame tick, direct = attach once
screen_id=990
x=95
y=185
width=755
height=500
hook=display_add_frame_callback
log_level=debug           ; trace|debug|info|warning|error|critical
console=true
```

`start_key` and `end_key` accept `F3`–`F12`, `Home`, `End`, `PageUp`,
`PageDown`, `Insert` and `Delete`. The DLL, the injector and the scripts all
read these keys from this file; nothing hardcodes a hotkey.

Menu items are data-driven:

```ini
[mod_menu.items]
weather=list,Rain,IGI_SDK_SetWeather(this.value),off,on
ammo=action,Enable Unlimited Ammo,IGI_SDK_EnableUnlimitedAmmo()
```

## Loading the internal demo

`IGI-MenuManager-Internal.dll` is an in-process module, not a standalone
program. A loader must map it into the 32-bit `igi.exe` process and call the
exports from the normal plugin lifecycle:

```cpp
using Initialize = bool (*)();
using Toggle     = bool (*)();
using Shutdown   = void (*)();

// Resolved from IGI-MenuManager-Internal.dll after the game is ready.
Initialize initialize = /* symbol lookup */;
Toggle     toggle     = /* symbol lookup */;
Shutdown   shutdown   = /* symbol lookup */;

initialize();   // stage the menu, install the frame hook, write the menu QVM
toggle();       // show the menu (start_key does this automatically in game)
shutdown();     // close the screen and unload
```

Never call `initialize()` from `DllMain`. The default startup thread waits for
`start_key`, then initializes, installs the hook and opens the menu.

Exports: `IGI_MenuManager_Internal_Initialize`,
`IGI_MenuManager_Internal_Toggle`, `IGI_MenuManager_Internal_Reopen`,
`IGI_MenuManager_Internal_RequestShutdown`,
`IGI_MenuManager_Internal_CanUnload`, `IGI_MenuManager_Internal_RunFrame`,
`IGI_MenuManager_Internal_Shutdown`.

### A real mod using natives

The internal demo drives its six `config.ini` options through the typed
wrappers. The same pattern works for a mod's own feature code:

```cpp
#include "igi_sdk/IGI.hpp"
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/menu_manager.hpp"

void registerMod(igi::Runtime& runtime) {
    igi::MenuManager menu(runtime.scheduler());
    menu.createMenu("main", "My IGI Mod");

    menu.addToggle("main", "Rain", true, [](bool enabled) {
        // Queued onto the game thread by the scheduler.
        IGI::GAME::SET_RAINING(enabled ? "rain" : "clear");
    });
    menu.addToggle("main", "Frame Limit", true, [](bool enabled) {
        IGI::MISC::FRAMES_SET(enabled ? 30 : 60);
    });
    menu.addAction("main", "Unlimited Ammo", [] {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
    });
    menu.addAction("main", "Log State", [] {
        // Read-only diagnostics.
        (void)IGI::GAME::IS_PAUSED();
        (void)IGI::GAME::GET_EVENT0();
        (void)IGI::GAME::GET_WORLD();
    });
}
```

Menu callbacks are queued with the scheduler; they never run on the input or
renderer thread. Read-only natives such as `IS_PAUSED` must be called from the
game thread, so call them from a scheduled task.

## Injecting

`IGI1-Injector.exe` is a 32-bit console tool:

```sh
IGI1-Injector.exe -l                                              # list processes
IGI1-Injector.exe -n igi.exe -i C:\IGI\IGI-MenuManager-Internal.dll -v
IGI1-Injector.exe -p 1234 -i C:\IGI\IGI-MenuManager-Internal.dll
```

Under Wine, use the wrapper so Unix paths are translated and the injector is
found automatically:

```sh
WINEPREFIX=$HOME/igi-test-prefix ./tools/wine_igi_inject.sh \
  /path/to/IGI-MenuManager-Internal.dll igi.exe
```

The wrapper injects into an already-running game; it does not launch it. Use
the same `WINEPREFIX` you launched the game with.

Runtime logs are written to `igi_sdk.log` in the game directory.

## Verification

```sh
tools/verify_igi_re_hook_source.sh
tools/verify_igi1_hook.sh /path/to/igi.exe
```

The first validates the local registration evidence; the second checks
the supported executable hash, hook prologues, calling convention metadata and
task-table references. `python3 tools/build_inject_mod.py --attempts N` builds,
launches the game under Wine, injects the DLL, presses the configured keys and
reports whether the menu attached.

## License and credits

Read [`CREDITS.md`](CREDITS.md), [`NOTICE`](NOTICE) and `LICENSES/` before
redistributing. metadata is attributed and kept as address/signature
research only.
