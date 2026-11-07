# Project IGI Natives SDK

An SDK for single-player mod development for `IGI`. It provides a menu model, logging, and game natives from original game to create simple mod menu using internal and external libaries.
Right now only Internal mod menu is implemented , external witll use ImGUI for Dx7 will be in future releases more robust and customisable.

## Implemented features are:

- In-game menu
- Logging
- Game natives for weather, ammo, health, money, weapons, frame limit, etc.

Target: the EU/Chinese Windows x86 IGI 1 executable. Native Windows is
supported directly. On macOS and Linux, run the same Windows x86 game and DLL
through Wine using the included Wine scripts.
Out of scope: multiplayer, trainers, memory editors, process scanners.

## Requirements

- CMake 3.20+
- A 32-bit mingw-w64 toolchain (`i686-w64-mingw32-g++`), or a 32-bit MSVC toolset
- Python 3 (used to regenerate the hash table at build time)

## Build

Everything is built by `tools/build-win32-dll.sh`. It sets the C, C++ and RC
cross-compilers explicitly and discards any stale build tree that still points
at a host compiler.

### Internal menu

```sh
# The mod menu DLL (default target)
tools/build-win32-dll.sh --release

# The injector and the keypress helper
tools/build-win32-dll.sh --release --target igi1_injector
tools/build-win32-dll.sh --release --target igi1_keypress
```

Build the simple menu demo:

```sh
tools/build-win32-dll.sh --debug --target igi_simple_mod_menu
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

`project-igi-sdk-config.ini` sits beside `igi.exe` and is read on startup.

```ini
[mod_menu]
enabled=true
start_key=F3              ; opens the menu and toggles it closed again
end_key=F12               ; closes the menu and unloads the SDK
native_mode=hooked        ; required for internal menus
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

## Create, build and run a menu

Use `demos/simple-menu/simple_menu.cpp` as the menu template. Edit its items
and typed `IGI::` native actions, then build `igi_simple_mod_menu`.

On native Windows, launch `igi.exe` normally and inject the built DLL with
`IGI1-Injector.exe`. On macOS/Linux, use the Wine workflow below.

On macOS/Linux, set the Wine paths once. Copy the SDK config next to `igi.exe`;
its `start_key` toggles the menu and its `end_key` closes the menu and unloads
the DLL.

```sh
export IGI_GAME_DIR=/path/to/IGI
export IGI_WINEPREFIX=/path/to/wine-prefix
export WINE_BIN=/path/to/wine
cp demos/internal-menumanager/config.ini "$IGI_GAME_DIR/project-igi-sdk-config.ini"
```

Build and check the exact menu source and DLL:

```sh
tools/build-win32-dll.sh --debug --target igi_simple_mod_menu
IGI_RE_ROOT=/path/to/IGI-RE python3 tools/igi_runtime_harness.py \
  --path demos/simple-menu/simple_menu.cpp --exe "$IGI_GAME_DIR/igi.exe" \
  --dll build-win32-debug/igi-simple-mod-menu.dll
```

Launch, inject and open the menu automatically:

```sh
python3 tools/run_igi_wine.py level9 build-win32-debug/igi-simple-mod-menu.dll \
  --attempts 10 --press-menu-key
```

The launcher sends Escape, injects the DLL, presses the configured start key,
and stops on its first successful injection. It retries failed launches up to
`--attempts`. Logs are in `/private/tmp/igi-live-validation/` and
`$IGI_GAME_DIR/project-igi-sdk.log`.

For an already-running game, inject without launching a new level:

```sh
WINEPREFIX="$IGI_WINEPREFIX" tools/wine_igi_inject.sh \
  build-win32-debug/igi-simple-mod-menu.dll igi.exe
```

## Verification

Before injecting a mod menu, run the Unicorn x86 emulator with the source and
the exact DLL you built:

```sh
IGI_RE_ROOT=/path/to/IGI-RE python3 tools/igi_runtime_harness.py --path demos/simple-menu/simple_menu.cpp --exe /path/to/igi.exe --dll build-win32-debug/igi-simple-mod-menu.dll
```

The scan maps `IGI::DOMAIN::NAME` calls to their declared native signatures and
retail addresses, then executes supported calls in synthetic emulator state.
Pickup uses an initialized player fixture with the weapon creation and sound
calls stubbed and checked. `DEFER` means a call needs live game state and was
not executed. Raw address calls are not found by the scanner. A passing scan
does not prove DLL initialization, menu rendering, actual game effects, or
that every signature is correct; test those in a running game as well.

```sh
tools/verify_igi_re_hook_source.sh /path/to/IGI-RE
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
