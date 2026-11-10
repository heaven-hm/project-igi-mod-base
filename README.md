# Project IGI Mod Base

Project IGI Mod Base is a foundation for creating single-player mods and in-game mod menus It provides an in-game menu interface, game native bindings, runtime hooking and memory support, logging, and examples to build on.

## Included mod menus

### Simple Mod Menu

![Simple mod menu running in Project I.G.I.](assets/simple-mod-menu.png)

- Live rain, snow, or off selection and frame-rate selection (30, 60, or 120 FPS)
- Weapon list and pickup for Glock 17, MP5SD, UZI, M16A2, AK-47, Dragunov SVD, Minimi, and Spas 12
- First-person or third-person camera view
- Level selection for levels 1–14
- SFX volume, screen-flash clearing, and engine performance logging

### Advanced Mod Menu

![Advanced mod menu running in Project I.G.I.](assets/advanced-mod-menu.png)

- Everything in Simple Mod Menu
- Unlimited ammo
- Music volume control
- Player team/alive-state display
- Display refresh command

All controls run through the game-thread menu runtime. The mod menu pauses the
game while open and restores input when it closes.

### ImGui Advanced Menu (`feature/imgui-mod` branch)

![ImGui Advanced Menu running in Project I.G.I.](assets/imgui-advanced-mod-menu.png)

This Direct3D 7/Dear ImGui implementation is hosted on the `feature/imgui-mod` branch.
It hooks the game's Direct3D 7 render pipeline via [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui),
rendering a modern graphical overlay with interactive controls, theme selection, sliders,
combo boxes, and buttons alongside the retail game display.

### Heaven Mod Menu (Production)

Heaven's Mod is the flagship production menu shipping 73 verified native gameplay controls across Weapons, Player, Human, Camera, World, Vehicles, AI, and Live Target Selection. Available in both internal QVM and external DirectX 7 ImGui frontends, featuring the IGI-RE camera-override free camera with WASD flight, live Object/landmark teleport targets, Send Player Position destinations and custom XYZ, humanplayer.qvm jump speed/height editing with a Save and reload path, independent world/mission clocks, 3D NPC movement, car/tank and helicopter controls (stop reverse, drive, yaw/pitch/roll), weather control, an aligned third-person crosshair, and an Exit Mod eject action in both frontends. The internal menu can also chain onto an already-resident external menu's display hooks, so switching menus does not require restarting the game. The retail menu frame grows with the control set and pages the remaining rows.

| ![AI & NPC Controls](assets/heaven-mod-npc-controls.png) | ![Vehicle Controls](assets/heaven-mod-vehicle-controls.png) | ![World Controls](assets/heaven-mod-world-controls.png) |
|:---:|:---:|:---:|
| **AI & NPC Controls** | **Vehicle & Fleet Controls** | **World & Weather Controls** |

Target: the `European` version Windows x86 (32-bit only; x64/64-bit is forbidden and unsupported) IGI 1 executable. 
```sh
SHA256: dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01
MD5: 9d8ebb7f28a1be56a8626dd5948826e4
```
Native Windows is supported directly. On macOS and Linux, run the same Windows x86 game and DLL
through Wine using the included Wine scripts.

## How to Create a Mod Menu

The mod base provides two distinct menu architectures:
1. **Internal Mod Menu (develop & feature/imgui-mod branches)**: Uses Project I.G.I.'s native retail QVM `MenuManager` engine.
2. **External ImGui Mod Menu (feature/imgui-mod branch)**: Uses Dear ImGui hooked into Direct3D 7 via [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui).

### 1. Creating an Internal Mod Menu (IGI Game Menu UI)

### Creating a new menu

The shared `src/mod_base/menu_host.cpp` calls these two functions:

```cpp
bool configureModMenu(igi::MenuConfig& config);
void runModMenu(const igi::MenuConfig& config,
                       igi::Runtime& runtime,
                       std::size_t item,
                       std::string_view value);
```

To create a new menu, implement both functions in your menu source file and
link that file with `src/mod_base/menu_host.cpp` and `src/platform/windows_dll.cpp`.

`configureModMenu()` defines the menu items and values for your mod. </br>
`runModMenu()` handles the selected item interact with game natives.

Your new menu should follow the same two-function pattern as
`demos/internal/simple-mod/simple_menu.cpp` or `demos/internal/basic-mod/basic_menu.cpp`.


```cpp
#include "config/menu_config.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/mvp_natives.hpp"
#include "demos/internal/helpers/menu_helpers.hpp"

// 1. Define menu title, items, and initial choices
bool configureModMenu(igi::MenuConfig& config) {
    config.title = "My Custom Mod Menu";
    config.items = {
        {"godmode", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}},
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "FPS Limit", "", {"30", "60", "120"}}
    };
    return true;
}

// 2. Handle player interaction (invoked when items are clicked or changed)
void runModMenu(const igi::MenuConfig& config,
                igi::Runtime& runtime,
                std::size_t item,
                std::string_view value) {
    if (item >= config.items.size()) return;
    const auto& id = config.items[item].id;

    if (id == "godmode") {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
    } else if (id == "weather") {
        igi::setWeather(runtime, value);
    } else if (id == "fps") {
        igi::setFps(runtime, value);
    }
}
```

Link your menu source with `mod_base` and `src/platform/windows_dll.cpp`.
Existing examples:
- `demos/internal/basic-mod/basic_menu.cpp`
- `demos/internal/simple-mod/simple_menu.cpp`
- `demos/internal/advanced-mod/advanced_menu.cpp`

### 2. Creating an External ImGui Mod Menu (Dear ImGui Direct3D 7)

External menus (on branch `feature/imgui-mod`) use `src/mod_base/imgui_menu_host.cpp` and [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui) to hook `IDirect3DDevice7::EndScene` and render Dear ImGui draw calls directly into the game's backbuffer.

External menus share the exact same configuration and action interface as internal menus (`configureModMenu` and `runModMenu`), making custom logic 100% portable between both UI backends:

```cpp
#include "config/menu_config.hpp"
#include "mod_base/menu_host.hpp"
#include "natives/mvp_natives.hpp"
#include "demos/internal/helpers/menu_helpers.hpp"

// 1. Define ImGui window title, options, and items (rendered via Dear ImGui D3D7)
bool configureModMenu(igi::MenuConfig& config) {
    config.title = "My Custom ImGui Mod Menu";
    // Optional ImGui overlay window configuration
    config.imgui.theme = "Dark";       // "Dark", "Light", or "Classic"
    config.imgui.fontSize = 16.0f;
    config.imgui.collapsible = true;

    // 3 Menu Options (rendered as buttons and combo dropdowns in Dear ImGui)
    config.items = {
        {"godmode", igi::ModMenuItemKind::Action, "Unlimited Ammo", "", {}},
        {"weather", igi::ModMenuItemKind::List, "Weather", "", {"Off", "Rain", "Snow"}},
        {"fps", igi::ModMenuItemKind::List, "FPS Limit", "", {"30", "60", "120"}}
    };
    return true;
}

// 2. Handle player interaction (invoked when ImGui buttons are clicked or combo items selected)
void runModMenu(const igi::MenuConfig& config,
                igi::Runtime& runtime,
                std::size_t item,
                std::string_view value) {
    if (item >= config.items.size()) return;
    const auto& id = config.items[item].id;

    if (id == "godmode") {
        IGI::PLAYER::SET_UNLIMITED_AMMO();
    } else if (id == "weather") {
        igi::setWeather(runtime, value);
    } else if (id == "fps") {
        igi::setFps(runtime, value);
    }
}
```

Link your menu source with `src/mod_base/imgui_menu_host.cpp`, `igi_imgui_dx7`, and `mod_base`.
Existing examples:
- `demos/external/basic-mod/basic_menu.cpp`
- `demos/external/simple-mod/simple_menu.cpp`
- `demos/external/advanced-mod/advanced_menu.cpp`

---

## How to Build a Mod Menu

All Win32 targets are cross-compiled using `tools/build-win32-dll.sh` (or Visual Studio / MSVC on native Windows). Build artifacts remain in the ignored `build-win32-*` directory; publish binaries only when creating a GitHub Release.

### 1. Building Internal Mod Menus

```sh

# Advanced Internal Menu -> build-win32-release/igi-advanced-mod-internal.dll
tools/build-win32-dll.sh --release --target igi_advanced_mod_menu
```

### 2. Building External ImGui Mod Menus (`feature/imgui-mod` branch)

```sh

# Advanced ImGui External Menu -> build-win32-release/igi-advanced-mod-external.dll
tools/build-win32-dll.sh --release --target igi_advanced_mod_menu_external
```

### 3. Building Injector & Helpers

```sh
# Injector CLI -> build-win32-release/IGI-Injector.exe
tools/build-win32-dll.sh --release --target igi_injector

# Virtual Keypress helper -> build-win32-release/IGI-Keypress.exe
tools/build-win32-dll.sh --release --target igi_keypress
```

Use `--debug` instead of `--release` to enable verbose console logging and trace logs.

---

## How to Run and Test a Mod Menu

### 1. Verification in Unicorn x86 Emulator

Before injecting into live game processes, verify DLL native signatures and ABI stability:

```sh
tools/build-win32-dll.sh --debug --target igi_advanced_mod_menu
python3 tools/igi_runtime_harness.py \
  --path demos/internal/advanced-mod/advanced_menu.cpp \
  --exe /path/to/igi.exe \
  --dll build-win32-debug/igi-advanced-mod-internal.dll
```

### 2. Running Live under Wine / Proton (macOS / Linux)

Set environment variables once:

```sh
export IGI_GAME_DIR=/path/to/IGI
export IGI_WINEPREFIX=~/.wine
export WINE_BIN=wine
cp config/project-igi-mod-config.ini "$IGI_GAME_DIR/project-igi-mod-config.ini"
```

Launch the game and auto-inject:

```sh
# Internal Menu
python3 tools/run_igi_wine.py level1 build-win32-release/igi-advanced-mod-internal.dll --attempts 10 --press-menu-key

# External ImGui Menu
python3 tools/run_igi_wine.py level1 build-win32-release/igi-advanced-mod-external.dll --attempts 10 --press-menu-key
```

### 3. Running Natively on Windows

1. Copy `config/project-igi-mod-config.ini` into your game directory next to `igi.exe`.
2. Start `igi.exe`.
3. Use `IGI-Injector.exe` to inject the DLL into the running `igi.exe` process:
```sh
binaries\IGI-Injector.exe <PID> igi-advanced-mod-internal.dll
```

**In-Game Controls:**
- Press **F5** (default `start_key`) to display and navigate the in-game native menu.
- The game pauses automatically while the menu is active.
- Press **F12** (default `end_key`) to safely unhook and detach the menu host.


## Verification

Before injecting a mod menu, run the Unicorn x86 emulator with the source and
the exact DLL you built:

```sh
IGI_RE_ROOT=/path/to/IGI-RE python3 tools/igi_runtime_harness.py --path demos/internal/simple-mod/simple_menu.cpp --exe /path/to/igi.exe --dll build-win32-debug/igi-simple-mod-internal.dll
```

The scan maps `IGI::DOMAIN::NAME` calls to their declared native signatures and
retail addresses, then executes supported calls in synthetic emulator state.


```sh
tools/verify_igi_re_hook_source.sh /path/to/IGI-RE
tools/verify_igi_hook.sh /path/to/igi.exe
```

The first validates the local registration evidence; the second checks
the supported executable hash, hook prologues, calling convention metadata and
task-table references. The injected DLL repeats the two hook-prologue checks in
memory and does not run CryptoAPI file hashing inside Wine. `python3 tools/build_inject_mod.py --attempts N` builds,
launches the game under Wine, injects the DLL, presses the configured keys and
reports whether the menu attached.

## Menu execution flow

All three menu DLLs start through the same Windows entry point. They then select
their own menu implementation.

```mermaid
flowchart TD
    A[DLLMain] --> B[igiSdkMainThread]
    B --> C[Common menu runtime]
    C --> D{Menu DLL}
    D --> E[igi-basic-mod-internal]
    D --> F[igi-simple-mod-internal]
    D --> G[igi-advanced-mod-internal]
    E --> H[Basic menu actions]
    F --> I[Weather, FPS, weapons]
    G --> J[Player, weapon and game actions]
    H --> K[Game frame callback]
    I --> K
    J --> K
    K --> L[Create menu UI in game]
    L --> M[Menu actions call IGI natives]
    M --> N[Toggle or close with configured keys]
```

`igiSdkMainThread` handles configuration, hotkeys and shutdown. The shared
runtime creates the visible UI from the game frame callback; each DLL only
provides its own menu items and actions.


## License and credits

Read [`CREDITS.md`](CREDITS.md) and `LICENSES/` before
redistributing. Metadata is attributed and kept as address/signature
research only.
- SlickDaddy and BlankName — Project I.G.I. native research and information
- Cukier for ImGUI DirectX 7 Wrapper project [ImGUI Dx7](https://github.com/AndenDev/D3D7Imgui)
- Artiom — QVM tools and compiler work
- Inspired by BigBase V2 from GTA V modding community [BigBase-V2](https://github.com/BigBase/BigBase-V2)
